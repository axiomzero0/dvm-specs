# DVM Performance Engineering Priorities

**Status:** Draft
**Subsystem:** Cross-cutting / Performance Strategy
**Depends On:** `DVM-CRB`, `DVM-Hybrid-Tracing-Architecture`, `DGW-Core-IR`, `DVM-CR-PEA`, `DVM-Compiler-Laws`
**Owner:** DVM Systems Dev Team
**Last Updated:** 2026-09-09

> The DVM architecture is already pointed in the right direction: register
> CRB, direct-threaded Tier 0, trace-centric Tier 2, DGW SoA, PEA,
> speculative guards, async compilation, and static Tier 3.
>
> If the objective is **absolute runtime speed**, this document defines the
> implementation priorities that make the VM disappear from hot code.

---

## The Core Principle

> **DVM shouldn't try to make the VM itself incredibly fast. It should make
> the VM disappear.**

The CRB is the semantic anchor. The trace is the specialization boundary.
DGW gives a place to erase dynamic-language overhead wholesale. The hot
loop should ideally become something embarrassingly simple:

```asm
.Lloop:
    ; values already in registers
    ; no VM register array
    ; no opcode decode
    ; no dynamic dispatch
    ; no allocation
    ; no bounds check
    ; no shape check
    ; no call
    ; vectorized arithmetic
    ...
    jmp .Lloop
```

That's where the real speed comes from. Not another 17-pass optimizer
named something sufficiently ominous.

---

## 1. CRB Should Be Optimized Specifically for Decoding

The fixed 64-bit instruction format is nice for interpretation, but every
instruction does not need 64 bits of information forever.

Consider common instructions:

```text
ADD r3, r1, r2
MOV r3, r1
RET r3
JMP +12
```

They don't need 64 bits.

**Consider two CRB encodings:**

```text
CRB-C   canonical (64-bit, for cold/deopt)
CRB-F   compact/fetch-optimized (32-bit, for hot code)
```

**Important tradeoff:** Don't sacrifice the beautifully simple decoder
merely to save a few bytes. Benchmark 32-bit fixed vs 64-bit fixed vs
variable-length on actual workloads. Branchless/simple decoding is
extremely valuable for Tier 0.

---

## 2. Kill the Virtual Register File as Early as Possible

Don't let Tier 0 or Tier 1 continually materialize `regs[r3]`, `regs[r1]`,
`regs[r2]`.

Make the baseline JIT aggressively map hot virtual registers onto physical
registers:

```text
CRB r3 → machine register RAX
```

rather than:

```text
CRB r3 → load VM register array → operate → store VM register array
```

Once Tier 2 takes over, CRB register identity should cease to exist in
the generated machine code. This is where the register-based design
pays off.

---

## 3. Make Trace Recording Almost Free

Use patchpoints / counters embedded directly in hot code:

```asm
test counter, counter
jz   start_trace
```

Avoid `trace_controller->increment()` / `check()` on every iteration.

Ideal steady state:

```text
cold: profiling machinery exists
hot:  tiny counter/check
very hot: counter disappears entirely
```

Once the trace is stable, patch the profiling instruction out.

---

## 4. Make Specialization Persistent

Build a dependency:

```text
Trace
 ├── Shape(Point, version=17)
 ├── Type(i64)
 └── Method(foo, version=42)
```

Then the generated code simply assumes `shape == Point@17` with one guard.
If the dependency remains valid, everything downstream gets to pretend
the dynamic world doesn't exist.

The specs already have dependency tracking and guards, which is the
right foundation. Push this much harder.

---

## 5. Guard Hoisting Should Be Absolutely Vicious

Suppose:

```text
for (...) {
    check shape(obj)
    check type(x)
    check bounds(a, i)
    x = obj.x
}
```

Want:

```text
check shape(obj)
check type(x)

for (...) {
    x = load [obj + OFFSET_X]
}
```

And eventually, if proven:

```text
for (...) { ... }
```

The trace architecture is particularly well suited to this because the
trace already represents the frequently executed path.

---

## 6. PEA + Scalar Replacement Should Be One of the Crown Jewels

The Region/Ref model makes aliasing and escape analysis structural
rather than pointer-analysis hell. Exploit that aggressively.

Example:

```python
def f(x):
    p = Point(x, x + 1)
    return p.x + p.y
```

Don't generate:

```text
allocate Point → store x → store x+1 → load x → load x+1 → free Point
```

Turn it into:

```text
p.x → SSA value
p.y → SSA value
return x + (x + 1)
```

Ideally:

```text
return 2*x + 1
```

after algebraic simplification.

CR-PEA makes this even more interesting across inline boundaries.

---

## 7. Inline Aggressively, but with a Code-Size Budget

```text
tiny function:          always inline
hot monomorphic:        aggressively inline
cold function:          don't inline
huge function:          only partial inline
recursive:              bounded inline
```

Make the budget profile-dependent. A function called 500 million times
can justify ridiculous amounts of code growth.

---

## 8. Specialize Calls into Direct Calls

```text
call_indirect obj.method
```

Turn into:

```text
guard shape(obj) == Foo
direct_call Foo::method
```

Then inline it. Then:

```text
guard shape(obj) == Foo
<method body>
```

Then if static certification proves it:

```text
<method body>
```

Tier 3 should eliminate essentially all of this where the language
permits it.

---

## 9. Memory SSA Needs to Become Very Cheap

Don't make every optimization pass walk gigantic memory chains.

Introduce explicit memory partitions:

```text
MemoryToken
 ├── heap
 ├── globals
 ├── TLS
 ├── object region R1
 ├── object region R2
 └── unknown
```

Then a load from `Region(Point)` doesn't need to conservatively depend
on some unrelated array store. The Region system already gives the raw
material.

---

## 10. Don't Use One Giant Optimization Pipeline

Make the runtime pipeline extremely profile-dependent:

### Tier 1

```text
canonicalize → copy-prop → const-prop → simple inline →
simple type specialize → lower
```

### Tier 2 normal hot loop

```text
canonicalize → inline → type specialization → GVN → range analysis →
bounds elimination → LICM → PEA → scalar replacement → devirtualization →
load/store optimization → vectorization → lower
```

### Tier 2 pathological dynamic code

```text
minimal specialization → guards → trace compilation → lower
```

### Tier 3

```text
everything, with absurd optimization budgets
```

---

## 11. Add a Dedicated Range-Analysis Engine

Prove `0 <= i < length(a)` once. Then `a[i]` becomes a raw load.

Because the trace is path-specific, range information can be dramatically
stronger than ordinary global analysis:

```text
if (i >= 0 && i < n) {
    hot loop  // i ∈ [0,n-1]
}
```

This feeds into: bounds-check elimination, loop unrolling, vectorization,
strength reduction, induction-variable elimination.

---

## 12. Build a Real Loop Optimizer Around the Trace Representation

For numeric workloads:

```text
for i in range(n):
    c[i] = a[i] + b[i]
```

Discover: induction variable, known trip count, contiguous accesses, no
alias, simple scalar operation → produce vector loads/adds/stores
(AVX2 / AVX-512 / SVE / NEON).

The important part is alias + range + alignment analysis before
vectorization. The Region/Ref architecture can make this substantially
easier.

---

## 13. Add Alignment Proofs

Don't stop at "a and b don't alias." Try to prove:

```text
a % 32 == 0
length % vector_width == 0
```

Dynamic languages usually don't hand these for free, so trace guards
can create them:

```text
guard aligned(a, 32)
guard length % 8 == 0
```

Then: 8-wide vector loop with a side exit for the unusual case.

---

## 14. Specialize Values, Not Just Types

Don't merely infer `x : i64`. Potentially infer `x = 4`. Then `x * y`
becomes `4 * y` → `y << 2`.

Same with: array length, object offsets, branch conditions, function
targets, enum values, flags, configuration values. Trace compilation is
uniquely good at this.

---

## 15. Have a "Speculation Ladder"

```text
ANY
 ↓
TYPE
 ↓
SHAPE
 ↓
CONSTANT
 ↓
RANGE
 ↓
EXACT VALUE
```

Only specialize as far as the profile justifies. 95% Point might justify
a shape guard, but not necessarily exact object identity. A trace with
millions of observations justifies much more.

---

## 16. Optimize the Deopt Boundary Rather Than Avoiding Deopt

```text
OPTIMIZE AGGRESSIVELY
        ↓
very precise FrameState
        ↓
cheap side exit
        ↓
CRB
```

The whole point of FrameState machinery is that you can afford to be
reckless while remaining correct. Optimize for cheap speculation
failure, not zero speculation failure.

A guard that costs 1-2 cycles but permits an entire allocation, dispatch
chain, and bounds-check family to disappear is an excellent trade.

---

## 17. Use Separate Hot and Cold Code Sections

```text
.text.hot          hottest traces
.text             normal compiled code
.text.unlikely    deopt, exceptions, uncommon branches, slow helpers
```

A hot trace should not have enormous deopt machinery sitting a few bytes
away from it. It should have: `guard failure → far cold target`.

---

## 18. Make Helpers Disappear

```text
pure helper     → always inline if small
guarded helper  → inline fast path, cold slow path
opaque helper   → never inline, cold call
```

Dynamic property lookup should become `guard shape → load known offset`
rather than `call property_lookup()`.

---

## 19. Optimize the Compiler Itself with the Same Philosophy

For the most frequently accessed node attributes (`node_kind[]`,
`node_type[]`, `node_flags[]`, `node_first_use[]`), keep them tightly
packed. Don't blindly SoA everything.

Consider an AoSoA / hot-cold split:

```text
NodeHot { NodeKind, NodeFlags, TypeId, ... }
NodeCold { payload, debug info, source location, ... }
```

The CPU should not haul debugging metadata into L1 because a human once
decided every node deserves a complete biography.

---

## 20. Don't Let FWDs Accumulate

```text
N1 → FWD → FWD → FWD → FWD → N2
```

eventually becomes poison. Use O(1) mutation + cheap path compression.

```text
depth <= 1:  fine
depth > 2:   collapse
compile boundary: zero FWD chains
```

---

## 21. Add a Machine-Aware Late IR

DGW should stay target-independent. After DGW, have:

```text
DGW → Target-independent low-level IR → Target-specific machine IR →
register allocation → machine code
```

The low-level IR should be extremely good at representing loads, stores,
branches, calls, vector operations, atomics, address arithmetic, flags,
predication. Target-specific optimization happens after semantic
optimization finishes.

---

## 22. Register Allocation Deserves Obscene Amounts of Attention

For traces: linear scan first, because traces are naturally linear.

For larger compiled regions: PBQP / graph coloring / optimized linear
scan depending on complexity.

Critically: allocate registers **after** trace scheduling, not before.
The scheduler should understand actual machine register pressure.

---

## 23. Treat Trace Length as an Optimization Resource

Longer isn't automatically faster. Optimize for:

```text
benefit / code_size / register_pressure / compile_cost
```

rather than simply maximizing optimized instructions.

The hottest 200 instructions are often better than a theoretically
perfect 4,000-instruction trace.

---

## Priority Table

| Priority | Optimization | Potential Payoff |
|----------|-------------|-----------------|
| **S** | Aggressive inlining | Massive |
| **S** | Type/shape specialization | Massive |
| **S** | Scalar replacement + PEA | Massive |
| **S** | Bounds-check elimination | Massive |
| **S** | Devirtualization | Massive |
| **S** | Trace specialization | Massive |
| **S** | Register allocation | Massive |
| **S** | Loop optimization | Massive |
| **S** | SIMD/vectorization | Massive for numeric workloads |
| **A** | Guard hoisting | Very high |
| **A** | Range analysis | Very high |
| **A** | Load/store elimination | Very high |
| **A** | Constant/value specialization | Very high |
| **A** | Inline caches | Very high |
| **A** | Hot/cold splitting | High |
| **A** | Fast deoptimization | High |
| **B** | Interpreter micro-optimizations | Moderate |
| **B** | CRB compression | Workload-dependent |
| **B** | Fancy graph mutation tricks | Mostly compile-time |

---

## Target Architecture

```text
                  ┌─────────────────────┐
                  │      Guest Code     │
                  └──────────┬──────────┘
                             │
                         CRB frontend
                             │
                    ┌────────▼────────┐
                    │ Tier 0 register │
                    │ direct threaded │
                    └────────┬────────┘
                             │
                       cheap profiling
                             │
                    ┌────────▼────────┐
                    │ Tier 1 baseline │
                    │   register JIT  │
                    └────────┬────────┘
                             │
                     patchpoint/trace
                             │
                 ┌───────────▼───────────┐
                 │     Guest Trace       │
                 └───────────┬───────────┘
                             │
              ┌──────────────▼──────────────┐
              │          DGW-Core           │
              │                              │
              │  inline                     │
              │  specialize                 │
              │  devirtualize               │
              │  range analysis             │
              │  alias analysis             │
              │  PEA / CR-PEA               │
              │  scalar replacement         │
              │  GVN                        │
              │  LICM                       │
              │  BCE                        │
              │  loop optimization          │
              │  vectorization              │
              └──────────────┬───────────────┘
                             │
                       trace scheduling
                             │
                    ┌────────▼────────┐
                    │   Low-level IR  │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │ Machine backend │
                    │ RA + scheduling │
                    └────────┬────────┘
                             │
                         native code
                             │
                    ┌────────▼────────┐
                    │   HOT LOOP      │
                    │                │
                    │ no dispatch    │
                    │ no allocation  │
                    │ no dynamic call│
                    │ no bounds check│
                    │ no shape check │
                    │ vectorized     │
                    └────────────────┘
```
