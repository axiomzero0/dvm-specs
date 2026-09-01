# DVM Subsystem Specification: CR-PEA — Cross-Region Partial Escape Analysis

**Status:** Draft
**Subsystem:** Tier 2 / Tier 3 Optimizing Pass Pipeline
**Depends On:** `DGW-Core IR`, `DVM Hybrid Tracing Architecture`, `DVM Deopt & FrameState Machinery`, `DVM Compiler Laws`
**Owner:** DVM Compiler Working Group
**Last Updated:** 2026-09-02

> CR-PEA is a context-sensitive, region-based, lazy-materialization escape
> analysis pass operating over DGW/Trace graphs after inlining or trace
> formation. It is the cross-region evolution of the PEA design already
> specified in `DGW-Core-IR.md` Part 3.4.
>
> This document defines CR-PEA as a first-class, capability-aware, budgeted,
> verifier-checked DVM optimization — not as an ad-hoc Java-specific PEA
> extension. DVM is multi-language; the optimization must be expressed in
> terms of guest capabilities, not Java specifically.

---

## 1. The Correct DVM Framing

Do not call this "Java cross-method PEA." Call it:

> **Cross-Region Partial Escape Analysis with Lazy Materialization**

Because DVM is multi-language, the optimization must be expressed in terms
of guest capabilities, not Java specifically.

For a Java-like guest, CR-PEA targets:

- builder patterns
- stream pipelines
- record construction
- iterator objects
- closure boxes
- temporary tuples
- lambda capture objects
- exception objects that do not escape observably
- monitor objects that are local and uncontended

For other guests, it can apply to:

- temporary structs
- iterator objects
- option/result wrappers
- coroutine state objects
- closure environments
- parser AST node temporaries
- container view objects
- protocol buffer builder temporaries

The core principle is language-neutral:

> If an allocation is only observable within a limited region/context,
> DVM may virtualize it and materialize it only at the true boundary where
> observation becomes possible.

---

## 2. Where It Belongs in the Compiler

CR-PEA belongs in **Tier 2** and **Tier 3**, not Tier 0 or Tier 1.

### 2.1 Tier 0 — Interpreter

No. Tier 0 only executes CRB.

### 2.2 Tier 1 — Meta-traced / Baseline Traces

Only a simplified form. Tier 1 may use:

- local scalar replacement
- trivial allocation sinking
- obvious box elimination

But full cross-region lazy PEA is too expensive and too semantically
delicate for Tier 1.

### 2.3 Tier 2 — Optimizing Trace/Method Compiler

Yes. This is the primary home.

CR-PEA should run after:

1. call graph construction
2. PGO-driven inlining
3. trace formation or method graph construction
4. receiver/type specialization
5. initial guard insertion

and before:

1. final memory optimization
2. final GVN/DCE
3. vectorization
4. machine lowering

### 2.4 Tier 3 — AOT / Static Compiler

Yes, but only where static proofs allow it. In Tier 3, CR-PEA can remove
allocations without guards if it can prove:

- no dynamic escape
- no identity observation
- no monitor escape
- no weak-reference observation
- no finalizer observation
- no native escape
- no introspection escape

---

## 3. Why This Fits DGW Extremely Well

Traditional PEA often struggles because the IR does not naturally represent
object identity and memory regions. DGW already does.

In DGW, an allocation is not "just a pointer value." It is:

```text
ALLOC --> Region R17
```

References are projections:

```text
REF { region = R17, offset = 0 }
REF { region = R17, offset = 8 }
```

Stores and loads operate through those refs. This makes cross-region PEA
much cleaner. Instead of asking:

> "Does this SSA value alias something?"

DVM can ask:

> "Does any reference derived from Region R17 reach an escape sink?"

That is a much better question.

---

## 4. The Escape Lattice

CR-PEA needs a formal escape lattice. DVM should not use only
`NoEscape / Escape` — that is too weak. Use a richer lattice.

### 4.1 Escape Classes

```cpp
enum class EscapeClass : uint8_t {
    Unknown,
    NonEscape,
    LocalEscape,
    CallerEscape,
    ArgEscape,
    ReturnEscape,
    StoreEscape,
    GlobalEscape,
    NativeEscape,
    IdentityEscape,
    MonitorEscape,
    WeakRefEscape,
    FinalizerEscape,
    IntrospectionEscape,
    SuspensionEscape,
    ThrowableEscape,
    BottomEscape
};
```

More precisely, an allocation has an **escape set**, not a single class.

Example:

```text
Region R17 escapes to:
    CallerContext C3
    Store into Region R9 offset 16
```

This is better than saying simply "it escapes."

### 4.2 Escape Sinks

An escape sink is anything that can observe the object. DVM should define
sinks such as:

```cpp
enum class EscapeSinkKind {
    Return,
    GlobalStore,
    EscapingStore,
    NativeCall,
    FFITransition,
    IdentityObservation,
    MonitorEnter,
    WeakReferenceRegistration,
    FinalizerRegistration,
    DebuggerObservation,
    ProfilerObservation,
    FrameIntrospection,
    SerializationHook,
    SuspensionState,
    ExceptionObject,
    DynamicDispatchEscape,
    OpaqueRuntimeCall,
};
```

A region is virtualizable only if every reachable sink is either:

1. absent
2. dominated by a materialization point
3. proven non-observable by the Guest Language Profile

---

## 5. Cross-Method PEA Model

The key idea is correct: objects that escape only to specific callers can
still be scalarized if those callers participate in the analysis.

In DVM terms: an allocation may be virtual across an inlined call graph
region.

Example:

```text
caller():
    b = Builder()
    b.setA(1)
    b.setB(2)
    x = b.build()
    use(x)
```

Traditional single-method PEA may fail because `Builder` escapes into
`build()`. But if `build()` is inlined, DGW sees:

```text
ALLOC Builder R1
STORE R1.a = 1
STORE R1.b = 2
CALL build(receiver=R1)
    LOAD R1.a
    LOAD R1.b
    RETURN result
```

Now `R1` does not truly escape. It only flows into an inlined callee that
understands it. CR-PEA can virtualize `R1`.

Materialization happens only if:

- `build()` stores `R1` somewhere escaping
- `R1` is returned
- `R1` is passed to opaque code
- `R1` becomes observable via identity/monitor/weakref/etc.
- a side exit requires the real object

---

## 6. The Real Boundary Is Not the Method Boundary

This is the core architectural correction.

Old model:

```text
method boundary = escape boundary
```

CR-PEA model:

```text
escape boundary = first point where the object becomes observable
```

That boundary may be:

- a store into an escaping region
- a native call
- a return to un-inlined code
- a dynamic dispatch that cannot be analyzed
- a monitor enter
- an identity hash request
- a weak reference creation
- a finalizer registration
- an exception being thrown to an unknown handler
- a suspension point that stores the object in coroutine state
- a debugger-visible local at a forced materialization point
- a trace side exit whose FrameState expects the object

This is exactly what lazy materialization should do.

---

## 7. Lazy Materialization

Lazy materialization means: keep the object virtual until the last possible
point where a real object is required. This is powerful, but it requires
exact materialization placement.

### 7.1 Materialization Point

A materialization point is a DGW node:

```text
MATERIALIZE Region R17
```

It consumes:

- the virtual state of `R17`
- current control edge
- current memory state
- current FrameState context

and produces:

- a real object reference
- updated memory state
- updated GC/barrier state

### 7.2 Placement Rules

A materialization point must be inserted before any sink that requires a
real object. It should be placed as late as possible, but not later.

For example:

```text
if rare_path:
    escape(R1)
else:
    use_fields_only(R1)
```

Correct CR-PEA materializes only on `rare_path`. Not at the merge. Not at
the allocation. This is where PEA becomes PEA instead of simple scalar
replacement.

---

## 8. Cost-Driven Materialization

Materialization placement is not just a correctness problem. It is also a
cost problem. DVM should treat it as a cost-driven optimization problem.

### 8.1 Benefit Terms

Virtualizing a region saves:

- allocation cost
- GC registration cost
- write barriers
- initialization stores
- heap pressure
- cache pressure
- finalizer registration, if avoided
- monitor overhead, if elided

### 8.2 Cost Terms

Materialization costs:

- object allocation on escape path
- field stores during materialization
- GC barrier execution
- FrameState growth
- deopt-time materialization cost
- register pressure
- compile-time analysis cost
- possible code size growth

### 8.3 Cost Formula Conceptually

For each candidate region:

```text
net_benefit(R) =
    allocation_savings(R)
  + barrier_savings(R)
  + lock_elision_savings(R)
  + gc_pressure_reduction(R)

  - materialization_probability(R) * materialization_cost(R)
  - deopt_materialization_cost(R)
  - compile_cost(R)
  - frame_state_cost(R)
```

If `net_benefit(R)` is below threshold, DVM should not virtualize. This
prevents pathological cases where PEA makes side exits extremely expensive.

---

## 9. Interaction with Inlining

CR-PEA and inlining are mutually reinforcing. Inlining exposes cross-region
object flow. CR-PEA can then remove allocations that would otherwise make
inlining too expensive.

Therefore DVM should use a limited feedback loop:

```text
inline hot calls
    ↓
build escape graph
    ↓
virtualize cross-region objects
    ↓
reduce allocation/store cost
    ↓
possibly enable more inlining
```

But this must be budgeted. A full fixed point over inlining, cloning,
CR-PEA, guard insertion, and materialization can explode compile time. DVM
should use a bounded iterative loop:

```text
max_inline_pea_iterations = small constant
```

For JIT, probably 2 or 3 iterations. For AOT, maybe more.

---

## 10. Interaction with Tracing

This is especially important for DVM because DVM is trace-centric.

### 10.1 Intra-Trace CR-PEA

Inside a single root trace, cross-method PEA is natural if the trace
inlines callees.

Example:

```text
trace:
    obj = make_temp()
    process(obj)
    consume(obj.field)
```

If `make_temp` and `process` are inlined into the trace, CR-PEA can
virtualize `obj`. This is a huge win for hot loops.

### 10.2 Side Exits

Side exits are escape boundaries. If a side exit's FrameState expects a
real object, CR-PEA must materialize before the exit.

Example:

```text
guard shape == Expected else exit
use_virtual_object(obj)
```

If the exit needs to reconstruct `obj` as a real object for the
interpreter, materialization must occur on the exit path. DVM should
prefer:

```text
exit path:
    MATERIALIZE obj
    jump to exit
```

rather than materializing on the hot path.

### 10.3 Bridge Traces

Bridge traces complicate CR-PEA. A region may be virtual in the root trace
but expected real in a bridge.

Options:

1. materialize before entering the bridge
2. pass a virtual state descriptor to the bridge, if bridge supports it
3. re-virtualize in the bridge using bridge entry state
4. fall back conservatively

For the first implementation, DVM should use option 1: materialize before
transferring to any bridge that does not explicitly support the virtual
region. Later, DVM can support virtual-state-aware bridges.

---

## 11. Guest Semantic Constraints

This is where the Java-specific part must be generalized. CR-PEA must
respect the guest language's observability rules. DVM should use Guest
Language Profile capabilities.

### 11.1 Identity Observation

If the guest can observe object identity:

- `identityHashCode`
- address observation
- `===` on object identity
- object identity in maps
- debugging object IDs

then the object cannot be freely virtualized across points where identity
may be observed. If identity is requested, DVM must materialize first.

### 11.2 Monitor / Lock Semantics

For Java-like guests: `synchronized (obj) { ... }`. If `obj` is virtual
and never escapes, DVM may elide the lock. But if:

- another thread can observe the object
- the object is stored into shared memory
- the lock state can be introspected
- the guest exposes monitor information

then DVM must materialize. For languages without monitors, this capability
is simply absent.

### 11.3 Weak References

If a weak reference can be created to the object, the object cannot remain
virtual past that point. Weak reference registration is an escape sink.

### 11.4 Finalizers

If the object has a finalizer, CR-PEA must be conservative. Possible
policies:

1. do not virtualize finalizable objects
2. virtualize only if finalizer registration is proven absent
3. materialize before any path where finalizer registration would occur

For most dynamic languages, finalizer semantics are dangerous for PEA.

### 11.5 Reference Count Introspection

If the guest exposes reference counts, PEA must preserve
reference-count-visible behavior. If exact preservation is impossible,
virtualization must be disabled for observable objects.

### 11.6 Exception Objects

Exception objects are tricky. An exception object may be virtual until it
is thrown. If thrown into a known handler that only reads fields, DVM may
still virtualize it across the throw in some cases. But if:

- traceback observes it
- user code stores it
- exception chaining exposes it
- native code sees it
- debugger observes it

then it must be materialized. For most guests, thrown exceptions should be
treated as escape sinks unless proven otherwise.

---

## 12. Correctness Requirements

CR-PEA must satisfy the following invariant:

> For every virtual region `R`, every operation that can observe `R` must
> be dominated by a materialization of `R`, unless the Guest Language
> Profile proves that observation is impossible.

This should be a verifier rule.

### 12.1 Observability Set

DVM should define an observability set per guest profile. Examples:

```text
CanObserveIdentity
CanObserveAddress
CanRegisterWeakRefs
CanRegisterFinalizers
CanEnterMonitors
CanIntrospectFrames
CanExposeLocalsToDebugger
CanExposeLocalsToProfiler
CanSerializeObjects
CanPassToNative
CanSuspendWithObjectState
CanThrowAndPreserveObject
CanHashByIdentity
CanCompareByIdentity
```

If any of these can occur for a region, CR-PEA must handle it.

---

## 13. Proposed DVM Passes

Add the following passes to the DVM pass catalog.

### P-360: Cross-Region Escape Graph Construction

Builds an interprocedural escape graph after inlining.

Inputs:

- DGW graph
- inlined call tree
- region table
- ref table
- call sites
- access sites
- guest capability profile

Outputs:

- region nodes
- ref edges
- store edges
- call argument edges
- return edges
- sink edges

### P-361: Context-Sensitive Escape Lattice Solver

Computes escape facts per region and per context.

Outputs:

- escape class set for each region
- virtualizable flag
- required materialization sinks
- forbidden virtualization reasons

### P-362: Cross-Region Virtualization

Marks eligible regions as virtual. Replaces stores/loads with virtual
field state. Updates FrameState construction to use virtual object
descriptors.

### P-363: Lazy Materialization Placement

Inserts `MATERIALIZE` nodes at minimal escape boundaries. Uses cost model
and control-flow sensitivity.

### P-364: Monitor and Identity Escape Lowering

Handles Java-like monitor and identity semantics, or equivalent guest
features. Elides locks where legal. Materializes where identity or
monitor observation becomes possible.

### P-365: Cross-Method Materialization Costing

Evaluates whether virtualization is profitable. May disable CR-PEA for
regions with bad benefit/cost ratio.

### P-366: Trace Exit Materialization Sync

Ensures side exits and bridge traces receive correct materialized or
virtual state. This is mandatory for trace-based DVM.

---

## 14. Where It Runs in the Pipeline

A good Tier 2 ordering is:

```text
PGO injection
    ↓
inlining
    ↓
call specialization
    ↓
initial guard insertion
    ↓
P-360 Cross-Region Escape Graph Construction
    ↓
P-361 Escape Lattice Solver
    ↓
P-365 Materialization Costing
    ↓
P-362 Cross-Region Virtualization
    ↓
P-363 Lazy Materialization Placement
    ↓
P-364 Monitor/Identity Escape Lowering
    ↓
P-366 Trace Exit Materialization Sync
    ↓
memory SSA cleanup
    ↓
load/store optimization
    ↓
loop optimization
    ↓
guard optimization
    ↓
final DCE/GVN
```

This ordering matters. If CR-PEA runs too early, it lacks inlined context.
If it runs too late, memory optimization and guard placement may have made
incorrect assumptions.

---

## 15. The Advantage Over Typical JVM PEA

Most JVMs do EA pre-inlining or with limited interprocedural scope. DVM
can do better because:

1. DGW represents regions explicitly.
2. Inlining occurs before CR-PEA.
3. Traces already contain hot cross-method paths.
4. Memory edges make store/load relationships explicit.
5. FrameState machinery supports virtual object materialization.
6. PGO identifies which cross-method paths actually matter.
7. Lazy materialization can be path-sensitive.

This is a real architectural advantage.

---

## 16. Expected Performance Impact

For Java-like workloads, this could indeed reduce allocation heavily.
Realistic expectations:

| Workload Type | Allocation Reduction | Throughput Gain |
|---|---:|---:|
| Builder patterns | 30–60% | 10–30% |
| Stream pipelines | 30–50% | 10–40% |
| Record/temporary object code | 20–50% | 5–25% |
| Iterator-heavy loops | 20–40% | 5–20% |
| Lambda/closure-heavy code | 15–35% | 5–15% |
| Numeric loops with no allocation | 0–5% | 0–2% |
| Highly dynamic/escaping code | 0–10% | maybe negative if poorly costed |

The biggest gains are not just allocation removal. They come from:

- fewer GC barriers
- fewer write barriers
- fewer lock operations
- fewer heap stores
- better scalar optimization
- better vectorization
- reduced GC pressure

---

## 17. Risks

This optimization is powerful, but it has sharp edges.

### 17.1 Risk 1: Compile-Time Explosion

Cross-method escape analysis can become expensive.

Mitigation:

- budgeted inline depth
- budgeted region count
- SCC-based analysis
- summary-based escape facts
- early abort for huge graphs

### 17.2 Risk 2: FrameState Bloat

Virtual objects increase deopt metadata.

Mitigation:

- deduplicate virtual object descriptors
- cost FrameState growth
- disable CR-PEA when deopt metadata exceeds budget

### 17.3 Risk 3: Expensive Side Exits

If side exits frequently materialize large object graphs, performance
can regress.

Mitigation:

- profile side-exit probability
- materialize eagerly on likely-exit paths
- avoid virtualizing objects with high materialization cost

### 17.4 Risk 4: Incorrect Identity Semantics

If identity is observable, virtualization can corrupt semantics.

Mitigation:

- identity observation is an escape sink
- verifier checks identity sinks
- guest profile declares identity capabilities

### 17.5 Risk 5: Monitor Semantics

Lock elision can be wrong if object becomes shared.

Mitigation:

- monitor enter is an escape sink unless object is provably thread-local
- stores to shared memory materialize object

### 17.6 Risk 6: Dynamic Languages

Dynamic features can invalidate assumptions.

Mitigation:

- dependency records
- invalidation on shape/class/module change
- conservative fallback for dynamic introspection

---

## 18. Implementation Strategy

Do not build the full research version first. Use a staged plan.

### 18.1 Phase 1: Single-Region PEA

Already planned (DGW-Core-IR.md Part 3.4). Handles:

- local allocations
- scalar replacement
- simple materialization
- simple lock elision

This is the foundation.

### 18.2 Phase 2: Inlined Cross-Region PEA

Extend PEA across inlined calls. Support:

- caller/callee region flow
- return escape classification
- argument escape classification
- materialization at un-inlined call boundaries

This is the first CR-PEA milestone.

### 18.3 Phase 3: Path-Sensitive Lazy Materialization

Add branch-sensitive materialization. Support:

- materialize only on escaping branch
- materialize at side exits
- materialize before exception sinks
- materialize before suspension sinks

This is where the big wins appear.

### 18.4 Phase 4: Context-Sensitive Escape Lattice

Add richer context sensitivity. Support:

- call-site-specific escape facts
- cloned function specialization
- trace-specific escape facts
- limited polymorphic escape summaries

### 18.5 Phase 5: Cross-Trace Virtual State

Advanced. Allow virtual regions to be understood across root trace and
bridge traces. This is hard. Do it only after Phases 1–4 are stable.

---

## 19. Suggested DVM Law Additions

These are normative rules that should be added to
`DVM-Compiler-Laws.md` as the `PEA-X` family of laws.

### Rule PEA-X1 — Cross-Region PEA Must Be Capability-Gated

CR-PEA may only apply when the Guest Language Profile provides sufficient
observability rules. If identity, monitor, weakref, finalizer, or
introspection semantics cannot be modeled, CR-PEA must be conservative.

### Rule PEA-X2 — Virtual Regions Must Have Escape Proofs

A region may be virtualized only if the escape lattice proves that no
unmaterialized escape sink can observe it.

### Rule PEA-X3 — Materialization Must Dominate Observation

Every operation that can observe a virtual region must be dominated by a
materialization point, unless the guest profile proves observation
impossible.

### Rule PEA-X4 — Lazy Materialization Must Be Costed

Materialization placement must use a cost model. CR-PEA must not
virtualize regions whose expected side-exit materialization cost exceeds
allocation savings.

### Rule PEA-X5 — Trace Exits Are Escape Boundaries

Unless a bridge or exit target explicitly supports virtual state, trace
side exits must materialize required virtual objects.

### Rule PEA-X6 — CR-PEA Must Be Kill-Switchable

CR-PEA must be disableable globally, per tier, per function, and per
guest language.

---

## 20. Final Verdict

CR-PEA should be one of DVM's flagship Tier 2 optimizations. It is exactly
the kind of optimization that DGW/Trace is designed to enable.

The right design is:

- **Region-based**
- **Context-sensitive**
- **Inlining-aware**
- **Trace-aware**
- **Cost-driven**
- **Lazy-materializing**
- **Guest-semantics-constrained**
- **Deopt-safe**
- **Verifier-enforced**
- **Kill-switchable**

If built correctly, this is one of the features that can push DVM from
"fast dynamic VM" into "beats static compilers on hot managed code"
territory.
