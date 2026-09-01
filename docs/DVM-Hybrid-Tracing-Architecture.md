# DVM Hybrid Tracing Architecture

## Core tier model with meta-tracing

DVM now uses four execution modes:

1. **Tier 0 — Direct-threaded register interpreter**
   - Full semantic fallback.
   - Collects profiles.
   - Can be meta-traced.

2. **Tier 1 — Meta-traced interpreter traces**
   - Records interpreter-level execution.
   - Eliminates dispatch overhead.
   - Specializes hot interpreter paths.
   - Still closely tied to interpreter state.

3. **Tier 2 — Normal guest traces + optimizing trace JIT**
   - Records guest-level hot paths.
   - Uses DGW/Trace graphs.
   - Applies full optimization:
     - guard strengthening
     - PEA/EA
     - scalar replacement
     - bounds-check elimination
     - vectorization
     - bridge stitching
     - trace trees

4. **Tier 3 — AOT / static trace specialization**
   - Uses stored trace profiles or static proofs.
   - May precompile trace skeletons.
   - Removes guards only where statically certified.

The important shift is:

> Tier 2 is not “method JIT first, traces later.”  
> Tier 2 is **trace-centric**, with method graphs used as fallback, cold-path provider, and source of static information.

---

# 1. Meta-Tracing vs Normal Tracing in DVM

## 1.1 Meta-tracing

Meta-tracing records what the **interpreter itself** does.

Example interpreter actions:

```text
fetch bytecode
decode opcode
read register r3
read register r7
check shape
dispatch ADD
write register r9
update pc
check backedge counter
```

A meta-tracer records these interpreter-level operations while executing a hot loop.

The optimizer then removes interpreter overhead:

- fold `pc`
- eliminate fetch/decode
- eliminate dispatch table lookup
- inline bytecode handler logic
- promote interpreter registers to trace values
- specialize inline cache checks
- remove dead VM bookkeeping

This gives fast warmup code without writing a full method JIT first.

---

## 1.2 Normal tracing

Normal tracing records the **guest program’s semantic operations**.

Example guest operations:

```text
add i32 r1, r2
load field obj.x
check bounds array[i]
call method foo
branch if less-than
```

Normal traces are higher-level than meta-traces.

They are better for:

- escape analysis
- scalar replacement
- range analysis
- vectorization
- devirtualization
- inlining
- cross-bytecode optimization

---

## 1.3 Why mix them?

Meta-tracing is excellent for:

- removing interpreter dispatch
- fast warmup
- specializing VM mechanics
- tracing languages with very dynamic semantics

Normal tracing is excellent for:

- high-level optimization
- numeric loops
- PEA
- vectorization
- stable hot paths

DVM therefore uses:

- **meta-tracing at lower tiers**
- **normal tracing at higher tiers**
- **method graphs as fallback and glue**

---

# 2. The Central Problem

Meta-tracing alone usually produces traces like:

```text
VM_REG_LOAD r3
VM_REG_LOAD r4
VM_SHAPE_CHECK
VM_DISPATCH ADD
VM_REG_STORE r5
VM_PC_UPDATE
```

That is fast, but still too low-level for serious optimization.

Normal tracing alone can be hard because:

- dynamic languages have many implicit VM operations
- inline caches, shape checks, and barriers must still appear
- interpreter state must be reconstructable
- side exits need exact guest state

Therefore DVM needs a **lifting pipeline**:

```text
Raw meta-trace
    ↓
Interpreter-op normalization
    ↓
Guest projection
    ↓
Guest semantic DGW trace
    ↓
Full DGW optimization
    ↓
Native trace
```

This is the key architectural idea.

---

# 3. DGW/Trace

The IR becomes **DGW/Trace**, not just DGW.

DGW/Trace is still a Dynamic Graph Web:

- nodes
- typed ports
- first-class references
- regions
- memory tokens
- effect tokens
- control tokens
- guard edges

But the primary compilation unit is a **trace fragment**.

---

## 3.1 Trace fragment

A trace fragment is a linear or tree-shaped path through program execution beginning at a trace entry point.

Typical trace entry points:

- loop header
- hot function entry
- hot branch path
- side-exit bridge
- OSR entry
- coroutine resume point
- exception handler entry, if hot

A trace fragment has:

- entry state
- live guest registers
- live VM state
- FrameState
- guard list
- side exits
- dependencies
- exit targets
- optional loop-closing backedge

---

## 3.2 Trace kinds

DVM defines five trace kinds.

### 1. `MetaInterpreterTrace`

Recorded from the interpreter.

Contains interpreter-level operations.

Used mainly for Tier 1.

---

### 2. `GuestTrace`

Contains guest semantic operations.

Used for Tier 2 optimization.

This is the main high-performance trace form.

---

### 3. `BridgeTrace`

Recorded from a side exit.

Example:

```text
root trace assumes branch taken
branch becomes hotly not-taken
DVM records bridge trace from that side exit
```

Bridge traces stitch alternate paths into the trace tree.

---

### 4. `NativeTrace`

Recorded from already-compiled code using patchpoints or instrumentation.

Used when a Tier 1 or Tier 2 compiled fragment wants to promote a hot loop into a better trace.

---

### 5. `StaticTrace`

Built from AOT profiles or static proof artifacts.

Used in Tier 3.

---

# 4. Trace Runtime Components

The DVM trace runtime is divided into the following systems.

---

## 4.1 Trace Controller

The Trace Controller decides:

- when to start tracing
- which trace kind to use
- when to abort
- when to compile
- when to promote a trace
- when to blacklist
- when to invalidate

Inputs:

- loop backedge counters
- function heat
- branch stability
- PGO confidence
- deopt history
- code cache pressure
- memory budget
- compilation concurrency budget

Outputs:

- start trace
- abort trace
- compile trace
- install trace
- invalidate trace
- telemetry events

---

## 4.2 Trace Recorder

The Trace Recorder observes execution and emits trace operations.

It must be:

- low overhead while recording
- precise about guest state
- able to abort safely
- able to close loops
- able to create side exits
- integrated with FrameState generation

Recorder responsibilities:

- capture entry FrameState
- record operations
- record guards
- record branches taken
- record value promotions
- record virtualizable allocations
- detect loop closure
- detect incompatible state
- stop on unsupported operations
- produce a raw trace buffer

---

## 4.3 Trace Lifter

The Trace Lifter converts raw meta-traces into guest DGW traces.

Inputs:

- raw interpreter operations
- interpreter frame layout
- Guest Language Profile
- bytecode metadata
- inline cache state
- PGO data

Outputs:

- guest semantic DGW trace
- FrameStates at guest bytecode boundaries
- dependency records
- guard nodes
- side-exit metadata

---

## 4.4 Trace Optimizer

The Trace Optimizer runs DGW passes over trace graphs.

It performs:

- canonicalization
- guard optimization
- type/shape promotion
- PEA
- scalar replacement
- loop optimization
- vectorization
- side-exit minimization
- bridge integration

---

## 4.5 Trace Linker

The Trace Linker connects compiled traces to:

- root traces
- bridge traces
- baseline code
- interpreter entry
- deopt stubs
- method code
- OSR entries
- runtime helpers

It is responsible for safe patching and atomic publication.

---

## 4.6 Trace Cache

The Trace Cache stores compiled trace fragments.

Each trace entry contains:

- trace ID
- kind
- entry point
- entry state signature
- dependencies
- guard metadata
- side exits
- bridges
- FrameState tables
- GC maps
- telemetry
- compilation version

---

# 5. Interpreter Requirements for Meta-Tracing

This is critical.

A normal C++ switch interpreter is not good enough for serious meta-tracing.

DVM’s interpreter must be **meta-traceable**.

That means the interpreter must expose its operations in a form that can be recorded and optimized.

---

## 5.1 Traceable interpreter handlers

Each bytecode handler must be defined with:

1. normal execution semantics
2. trace-building semantics
3. guard requirements
4. FrameState mapping
5. side-exit mapping
6. promotion hints
7. virtualization hints
8. effect classification

Conceptually:

```cpp
struct BytecodeHandlerDescriptor {
    BytecodeOpcode opcode;

    ExecFn execute;
    TraceBuildFn trace_build;

    EffectClass effects;
    FrameStatePolicy frame_state_policy;
    SmallVector<PromotionHint, 4> promotions;
    SmallVector<VirtualizationHint, 2> virtualizations;
    SmallVector<GuardHint, 4> guards;
};
```

---

## 5.2 Interpreter micro-ops

The interpreter should be expressible in micro-ops.

Examples:

```text
READ_VM_REG
WRITE_VM_REG
READ_GUEST_REG
WRITE_GUEST_REG
READ_PC
WRITE_PC
FETCH_OPCODE
DECODE_OPCODE
DISPATCH_OPCODE
LOAD_SHAPE
CHECK_SHAPE
CHECK_TYPE
CHECK_BOUNDS
IC_LOOKUP
IC_UPDATE
ALLOC_OBJECT
WRITE_BARRIER
CALL_HELPER
ENTER_FRAME
EXIT_FRAME
CHECK_BACKEDGE
UPDATE_PROFILE
```

A meta-trace records these micro-ops.

The trace optimizer then removes the ones that are redundant on the hot path.

---

## 5.3 Interpreter operations must be classified

Every interpreter operation must declare:

- pure
- vm-internal
- guest-visible
- memory effect
- allocation effect
- guard effect
- side-exit possible
- trace-eliminable
- trace-hoistable
- virtualizable
- opaque

Without this, the trace optimizer cannot safely remove interpreter noise.

---

## 5.4 Runtime helpers must be trace-aware

Runtime helpers fall into categories:

### Pure helpers

Can be inlined into traces.

Examples:

- integer overflow check
- float classification
- shape lookup
- small math routines

### Guarded helpers

Can be inlined with guards.

Examples:

- monomorphic field load
- known container access
- known type check

### Opaque helpers

Must cause side exit or call barrier.

Examples:

- arbitrary native FFI
- dynamic code evaluation
- complex reflection
- allocation with user-visible hooks
- finalizer registration
- GC-sensitive operations

---

# 6. Normal Tracing for Higher Tiers

For Tier 2 and above, DVM should prefer **guest semantic tracing**.

There are three ways to obtain normal traces.

---

## 6.1 Interpreter-driven normal tracing

While executing in Tier 0, the interpreter emits guest semantic trace ops instead of raw interpreter ops.

Example:

```text
GUEST_ADD r1, r2, r3
GUEST_LOAD_FIELD obj, field_x, result
GUEST_CHECK_BOUNDS array, index
GUEST_BRANCH_LT a, b, taken
```

This produces clean high-level traces directly from Tier 0.

This is often the best first implementation.

---

## 6.2 Baseline-instrumented tracing

Tier 1 baseline code includes trace-start patchpoints.

When a loop becomes hot, baseline code can transfer to trace recording mode.

The recorder captures:

- live guest registers
- branch directions
- call targets
- type/shape observations
- FrameState

This lets DVM record from faster code than the interpreter.

---

## 6.3 Static trace construction from method graphs

If Tier 2 already has a method graph, DVM can build a trace statically using PGO.

Instead of dynamically recording:

```text
hot path = most probable branch sequence from loop header
```

The trace builder extracts that path from the method DGW and creates a trace graph.

This is effectively **trace specialization from method IR**.

It is very useful for:

- stable hot loops
- AOT
- recompilation after deopt storms
- trace tree expansion

---

# 7. Trace State Signatures

A trace can only close or link if the state at the target matches expectations.

Therefore every trace has a **Trace State Signature**.

---

## 7.1 Signature contents

For each live value at trace entry:

- value kind
- type
- shape/version, if object
- nullness
- integer range class
- float class, if useful
- register class
- GC reference flag
- escaped/virtual status
- dependency set

Example:

```text
TraceStateSignature {
    r0: Object(shape=Point, non_null)
    r1: Int(range=[0, 1023])
    r2: Object(shape=Array_f64, non_null)
    r3: Float(any)
    r4: Closure(cell_count=2)
}
```

---

## 7.2 Signature matching

When a trace reaches a loop header or branch target, DVM checks:

```text
current_state_signature =~ expected_signature
```

If compatible:

- close loop
- link trace
- continue trace tree

If incompatible:

- create side exit
- record bridge trace
- fall back
- abort if too many incompatible states

---

## 7.3 Signature abstraction

Signatures must not be too specific or traces will never link.

DVM uses abstraction levels:

```text
EXACT_CONSTANT
EXACT_SHAPE
TYPE_CLASS
RANGE_CLASS
NULLNESS_CLASS
ANY
```

The trace controller chooses abstraction level using PGO confidence.

---

# 8. DGW/Trace Node Extensions

DGW needs additional trace-specific node kinds.

---

## 8.1 Trace entry nodes

### `TRACE_START`

Marks the beginning of a trace.

Inputs:

- entry control
- entry FrameState
- entry state signature

Outputs:

- trace control
- projected live values

---

### `TRACE_ENTRY_STATE`

Represents a live value entering the trace.

Outputs:

- VALUE for guest register
- VALUE for VM register
- MEMORY state
- EFFECT state

---

## 8.2 Trace loop nodes

### `TRACE_STATE`

Loop-carried value node, similar to `STATE` or phi, but tied to trace header signature.

Inputs:

- initial value from trace entry
- backedge value from trace loop

Outputs:

- current loop-carried value

---

### `TRACE_LOOP_CLOSE`

Checks whether current state matches trace entry signature.

Inputs:

- current control
- current state values
- expected signature

Outputs:

- backedge control if compatible
- side-exit control if incompatible

---

## 8.3 Guard and exit nodes

### `TRACE_GUARD`

A speculative guard inside a trace.

Inputs:

- incoming control
- condition
- dependency set
- FrameState

Outputs:

- success control
- side-exit control

Payload:

```cpp
struct TraceGuardPayload {
    GuardKind kind;
    FrameStateId frame_state;
    DeoptReason reason;
    ExitTargetId exit_target;
    Confidence confidence;
    DependencySetId dependencies;
};
```

---

### `TRACE_EXIT`

Transfers control out of the trace.

Targets:

- interpreter
- baseline code
- method code
- bridge trace
- deopt stub
- runtime helper

Payload:

```cpp
struct TraceExitPayload {
    ExitKind kind;
    FrameStateId frame_state;
    TargetKind target_kind;
    TargetId target_id;
    MaterializationPlanId materialization;
};
```

---

### `TRACE_BRIDGE`

Marks the beginning of a bridge trace from a side exit.

Inputs:

- exit state from parent trace

Outputs:

- bridge control
- bridge state values

---

## 8.4 Meta-trace nodes

These are used before lifting.

### `META_READ_VM_REG`
### `META_WRITE_VM_REG`
### `META_READ_PC`
### `META_WRITE_PC`
### `META_FETCH_OPCODE`
### `META_DISPATCH`
### `META_IC_LOOKUP`
### `META_IC_STATE`
### `META_PROMOTE`
### `META_VIRTUALIZE`
### `META_HELPER_CALL`
### `META_FRAME_SLOT_LOAD`
### `META_FRAME_SLOT_STORE`

The trace lifter eliminates or lowers these into guest DGW nodes.

---

# 9. Meta-Trace Lifting Pipeline

This is the core of the hybrid design.

---

## Stage 1: Raw Meta-Trace Capture

Recorder emits:

```text
META_READ_VM_REG r3
META_READ_VM_REG r4
META_IC_LOOKUP shape=Point
META_GUARD shape
META_DISPATCH ADD
META_WRITE_VM_REG r5
META_WRITE_PC +1
```

---

## Stage 2: Interpreter Noise Elimination

The lifter removes operations that are redundant on this path.

Examples:

- PC increment becomes constant.
- Opcode fetch becomes constant.
- Dispatch becomes direct branch or is removed.
- VM register moves become SSA values.
- Profile counter updates may be batched or removed.

---

## Stage 3: Guest Projection

The lifter maps VM registers to guest registers using the interpreter frame map.

Example:

```text
VM register vm_r12 == guest register r3
VM register vm_r13 == guest register r4
```

Now:

```text
META_READ_VM_REG vm_r12
META_READ_VM_REG vm_r13
```

becomes:

```text
GUEST_READ_REG r3
GUEST_READ_REG r4
```

---

## Stage 4: Bytecode Semantic Reconstruction

The lifter recognizes that a sequence of interpreter micro-ops implements one guest bytecode.

Example raw sequence:

```text
READ r3
READ r4
CHECK_INT r3
CHECK_INT r4
ADD_INT
WRITE r5
```

becomes:

```text
GUEST_ADD r5, r3, r4
```

with a FrameState at that bytecode offset.

---

## Stage 5: Guard Normalization

Interpreter guards become guest guards.

Examples:

- shape check → `GUARD_SHAPE`
- type check → `GUARD_TYPE`
- bounds check → `GUARD_BOUNDS`
- integer class check → `GUARD_INT_RANGE`
- method version check → `GUARD_METHOD_VERSION`

Each guard receives:

- FrameState
- dependency set
- exit target
- confidence

---

## Stage 6: Guest DGW Trace Production

The result is a normal guest trace graph:

```text
TRACE_START
TRACE_ENTRY_STATE r0
TRACE_ENTRY_STATE r1
TRACE_STATE sum
TRACE_STATE i
GUEST_LOAD a[i]
GUEST_MUL
GUEST_ADD sum
GUEST_ADD i, 1
TRACE_GUARD i < n
TRACE_LOOP_CLOSE
```

From here, normal Tier 2 DGW optimization takes over.

---

# 10. Trace Compilation Pipeline

The full trace compilation pipeline is:

```text
Hotness detected
    ↓
Choose trace kind
    ↓
Start recording
    ↓
Raw trace buffer
    ↓
Trace validation
    ↓
Meta lifting if needed
    ↓
Guest DGW trace graph
    ↓
Trace optimization passes
    ↓
FrameState finalization
    ↓
Machine lowering
    ↓
Register allocation
    ↓
Trace emission
    ↓
Side exit stub generation
    ↓
Trace linking
    ↓
Atomic installation
    ↓
Telemetry
```

---

# 11. Detailed Trace Pass Catalog

This is the trace-specific pass registry.

These passes run in addition to the normal DGW optimization passes.

---

## T-001: Trace Validation

Validates raw trace buffer.

Checks:

- entry FrameState exists
- all recorded operations are traceable
- no unsupported opaque operation without side exit
- state values are readable
- trace length is within budget
- loop header is reachable
- no forbidden VM state transitions

If invalid, abort trace safely.

---

## T-002: Trace Entry Binding

Binds trace entry to:

- guest function
- bytecode offset
- interpreter frame layout
- guest register map
- closure map
- exception state
- tracing/profiling state

Produces the initial `TRACE_START` node.

---

## T-003: State Signature Extraction

Builds the entry state signature.

For each live value:

- infer type
- infer shape
- infer nullness
- infer range
- infer dependency versions
- decide abstraction level

This signature controls loop closure and trace linking.

---

## T-004: Meta-Op Normalization

Applies only to meta-traces.

Normalizes interpreter operations:

- canonicalize register names
- remove redundant VM moves
- fold PC arithmetic
- fold opcode fetch constants
- simplify dispatch logic
- normalize inline cache operations

---

## T-005: Interpreter Dispatch Elimination

Removes interpreter dispatch overhead.

For each recorded bytecode:

- replace dispatch table lookup with known opcode
- replace indirect handler jump with direct semantic lowering
- remove unused opcode-case guards

This is one of the main benefits of meta-tracing.

---

## T-006: PC Constant Promotion

The program counter is usually predictable inside a trace.

This pass:

- promotes PC values to constants
- removes PC updates where bytecode sequence is fixed
- converts PC-based branches into trace guards

---

## T-007: VM-to-Guest Register Projection

Maps interpreter registers to guest registers.

This is guided by the interpreter frame map.

If a VM register has no guest-visible meaning, it is either:

- internal trace temporary
- dead
- part of FrameState only

---

## T-008: Guest Bytecode Lifting

Converts normalized interpreter micro-op sequences into guest semantic nodes.

Examples:

```text
interpreter ADD sequence → GUEST_ADD
interpreter LOAD_FIELD sequence → GUEST_LOAD_FIELD
interpreter CHECK_BOUNDS sequence → GUEST_CHECK_BOUNDS
interpreter CALL sequence → GUEST_CALL
```

This pass produces the high-level Guest DGW trace.

---

## T-009: Inline Cache Specialization

Specializes dynamic operations using recorded inline cache state.

Examples:

- monomorphic field load
- monomorphic method call
- stable container layout
- stable type check

Inserts guards for:

- shape version
- type version
- method version
- module/global version

---

## T-010: Guard Extraction

Identifies all assumptions in the trace and converts them to explicit `TRACE_GUARD` nodes.

Sources:

- interpreter checks
- inline cache assumptions
- type promotions
- shape promotions
- range assumptions
- alias assumptions
- branch directions

Every guard must receive a FrameState.

---

## T-011: Trace Canonicalization

Normalizes the trace graph.

Performs:

- identity elimination
- constant folding
- simple strength reduction
- dead branch removal for impossible recorded paths
- effect chain cleanup

---

## T-012: Constant Folding

Folds constants in the trace.

Trace recording often reveals constants that static compilation cannot know.

Examples:

- constant loop bound
- constant object shape
- constant method target
- constant enum-like integer
- constant branch direction

---

## T-013: Redundant Guard Elimination

Removes guards implied by earlier guards.

Examples:

- repeated shape guard on same object
- repeated bounds guard with unchanged index/range
- repeated nullness guard without intervening nulling store

---

## T-014: Guard Coalescing

Merges compatible guards.

Examples:

- shape and type guard on same object
- bounds guards with same array and related indices
- global version and module version guards

This reduces side-exit overhead.

---

## T-015: Type Promotion

Promotes observed types into trace assumptions.

Example:

```text
value usually Int
```

becomes:

```text
GUARD_TYPE value == Int
```

Then operations can be unboxed.

---

## T-016: Shape Promotion

Promotes object shapes.

Example:

```text
obj always has shape Point_v17
```

becomes:

```text
GUARD_SHAPE obj == Point_v17
```

Field accesses can then use direct offsets.

---

## T-017: Range Promotion

Promotes integer ranges.

Example:

```text
i observed in [0, 255]
```

becomes:

```text
GUARD_RANGE i in [0, 255]
```

This enables bounds-check elimination and narrower arithmetic.

---

## T-018: Nullness Promotion

Promotes non-null observations.

Example:

```text
obj never null on hot path
```

becomes:

```text
GUARD_NONNULL obj
```

---

## T-019: Trace Inlining

Inlines small hot callees into the trace.

This is trace-based inlining, not whole-method inlining.

The trace may inline:

- getters
- small arithmetic methods
- hot dispatch targets
- trivial constructors
- closure bodies, if supported

Inlining stops at:

- opaque FFI
- huge callees
- unsupported dynamic features
- budget exhaustion

---

## T-020: Call Specialization

Specializes call sites observed in the trace.

Examples:

- monomorphic receiver
- constant target
- stable signature
- stable closure shape

Inserts appropriate guards.

---

## T-021: Trace PEA

Performs escape analysis within the trace.

Because traces often cover only a hot loop, many allocations do not escape within that loop.

This pass identifies:

- loop-local allocations
- closure-local objects
- temporary containers
- boxed primitives
- iterator objects

---

## T-022: Scalar Replacement

Scalarizes non-escaping objects.

Example:

```text
tmp = Point(dx, dy)
use tmp.x
use tmp.y
```

becomes:

```text
dx
dy
```

No allocation.

---

## T-023: Allocation Sinking

Sinks allocations into paths where they are actually needed.

This is important for traces with side exits.

If an allocation only occurs on a side path, do not allocate on the hot root trace.

---

## T-024: Materialization Placement

Places materialization points for partially escaped virtual objects.

Materialization must occur before:

- side exit that expects the object
- call that may observe the object
- exception path
- suspension point
- native transition
- debug/profiling observation point

---

## T-025: Memory SSA Construction

Builds memory token chains inside the trace.

This ensures stores and loads remain correctly ordered.

Memory SSA is especially important in traces because loop-closing backedges can otherwise create confusing memory dependencies.

---

## T-026: Load Forwarding

Forwards loads from known stores inside the trace.

This works extremely well in loop-local object traces.

---

## T-027: Store Elimination

Removes stores that are overwritten before being read, provided the store is not guest-visible.

---

## T-028: Alias Classification

Classifies references using DGW region identities.

Because DGW references carry `RegionId`, this is cheap.

---

## T-029: Loop Invariant Hoisting

Hoists invariant operations out of the trace loop body.

In trace graphs, hoisting must be careful about:

- guards
- side exits
- memory tokens
- effect tokens

Only operations that are safe on every possible trace iteration may be hoisted.

---

## T-030: Induction Variable Analysis

Detects induction variables in the trace loop.

Examples:

- `i = i + 1`
- pointer increments
- array index recurrences
- reduction accumulators

---

## T-031: Bounds Check Elimination

Uses range analysis and induction variables to remove bounds checks.

Example:

```text
for i in 0..n:
    x = a[i]
```

If trace guards prove `i < a.length`, remove check.

---

## T-032: Trace Unrolling

Unrolls trace loops when profitable.

Trace unrolling is often more effective than method unrolling because the hot path is already known.

---

## T-033: Trace Peeling

Peels initial iterations to simplify guards.

Examples:

- align array accesses
- eliminate first-iteration checks
- stabilize shape after first transition

---

## T-034: Vectorization Analysis

Determines whether trace loop bodies can be vectorized.

Checks:

- aliasing
- contiguous accesses
- reduction safety
- overflow semantics
- exception safety
- guest numeric semantics

---

## T-035: SLP Vectorization

Vectorizes straight-line trace operations.

Traces are often very good for SLP because the hot path is linear.

---

## T-036: Loop Vectorization

Transforms eligible trace loops into vector loops.

Must preserve scalar fallback through side exits or peeled iterations.

---

## T-037: Side Exit Costing

Estimates cost of each side exit.

Factors:

- exit probability
- materialization cost
- FrameState size
- target availability
- bridge potential
- deopt history

High-cost exits may trigger:

- additional guards
- bridge compilation
- trace abort
- weaker optimization

---

## T-038: Bridge Formation

Creates bridge traces for hot side exits.

A bridge trace begins at a side exit and attempts to rejoin:

- root trace
- another bridge
- baseline code
- interpreter

This converts linear traces into trace trees.

---

## T-039: Trace Tree Linking

Links trace fragments into a trace tree.

Links include:

- root loop backedge
- side exit to bridge
- bridge to root
- bridge to bridge
- trace to method fallback
- trace to interpreter

All links must be atomically patchable.

---

## T-040: Trace Layout

Places hot trace code linearly.

Cold paths are moved out-of-line:

- deopt stubs
- bridge entries
- unlikely exceptions
- materialization code

---

## T-041: FrameState Minimization

Compresses FrameStates for trace guards and exits.

Allowed reductions:

- deduplicate identical states
- remove dead but non-introspectable values
- share constant pools
- encode register maps compactly

Forbidden reductions:

- removing values needed for traceback
- removing values needed for debugger
- removing values needed for guest refcount semantics
- removing suspension state

---

## T-042: Deopt Metadata Generation

Produces deopt metadata for trace exits that must fall back to lower tiers.

Not every side exit is a full deopt.

Possible exits:

- bridge
- baseline
- interpreter
- deopt stub
- method continuation

If no lower-tier compiled target exists, the exit must deopt to Tier 0.

---

## T-043: Dependency Registration

Registers dependencies for all trace assumptions.

Examples:

- shape version
- type version
- global version
- module version
- method version
- inline cache version
- profile version
- Guest Language Profile version

If any dependency changes, the trace tree must be invalidated.

---

## T-044: Machine Lowering

Lowers optimized DGW/Trace into machine IR.

This includes:

- instruction selection
- trace-specific guard lowering
- exit stub lowering
- patchpoint lowering
- safepoint insertion

---

## T-045: Register Allocation

Allocates registers for the trace.

Special considerations:

- live trace entry state
- side-exit register maps
- materialization temporaries
- GC reference maps
- bridge entry ABI

---

## T-046: Trace Emission

Emits final native trace code.

Outputs:

- trace body
- guard failure stubs
- side-exit stubs
- bridge entry points
- loop backedge patchpoints
- OSR entry points

---

## T-047: Trace Installation

Installs trace atomically.

Steps:

1. validate metadata
2. publish code with release semantics
3. patch loop entry or call site
4. keep old code alive until quiescence
5. record telemetry

---

# 12. Trace Recording Details

This section defines the runtime recording behavior.

---

## 12.1 Starting a root trace

A root trace usually starts at a loop header.

Conditions:

- backedge count exceeds threshold
- loop header state is stable enough
- PGO confidence sufficient
- function not blacklisted
- code cache has room
- compiler thread available

Recorder captures:

- guest function ID
- loop header bytecode offset
- FrameState
- live guest registers
- closure cells
- exception state
- tooling state
- current dependencies

---

## 12.2 Recording operations

For each executed operation:

- emit corresponding trace op
- update current state signature
- record guards
- record branch direction
- record value observations
- record allocation sites
- record call targets

The recorder must not execute irreversible side effects differently. It observes execution; it does not change semantics.

---

## 12.3 Recording branches

When a branch is encountered:

- record the taken direction
- emit a guard for that direction
- continue along taken path

The untaken path becomes a potential side exit.

If the untaken path later becomes hot, DVM may compile a bridge trace.

---

## 12.4 Recording calls

For calls, the recorder chooses one of:

### Inline

If callee is small and hot:

- trace into callee
- record inlined FrameState
- continue inside callee

### Side exit

If callee is too complex:

- exit to baseline/interpreter at call site

### Guarded direct call

If call target is stable:

- emit target guard
- record direct call

---

## 12.5 Loop closure

The trace closes when execution returns to the starting loop header.

DVM checks:

- bytecode offset matches
- guest function matches
- state signature compatible
- exception state compatible
- suspension state compatible
- tooling state compatible

If compatible:

- create `TRACE_LOOP_CLOSE`
- finalize trace graph

If incompatible:

- abort root trace or create bridge candidate

---

## 12.6 Trace abort conditions

Abort if:

- unsupported opcode
- opaque native helper
- too many side exits
- trace too long
- memory budget exceeded
- state signature unstable
- exception in flight where unsupported
- active tooling incompatible
- GC state unsafe
- deopt history poor

Abort must fall back safely.

---

# 13. Side Exits and Bridges

Side exits are the heart of tracing performance.

---

## 13.1 Side exit targets

A side exit may target:

1. **Interpreter**
   - safest fallback

2. **Baseline code**
   - faster than interpreter

3. **Method compiled code**
   - if a method JIT exists for the function

4. **Bridge trace**
   - specialized alternate path

5. **Deopt stub**
   - reconstruct lower-tier state when no direct continuation exists

---

## 13.2 Exit state

Every side exit must provide:

- FrameState
- materialization plan
- register map
- GC map
- exception state
- tooling state

---

## 13.3 Bridge recording

When a side exit becomes hot:

1. Trace Controller starts bridge recording from that exit.
2. Recorder begins with exit state.
3. Bridge executes until it:
   - rejoins root trace
   - reaches another trace
   - reaches a stable loop
   - falls back to interpreter
4. Bridge trace is compiled.
5. Side exit is patched to point to bridge.

---

## 13.4 Trace tree limits

Trace trees can explode.

DVM must limit:

- bridges per root trace
- total trace code size per function
- trace recompilation count
- bridge depth
- exit materialization cost

If limits are exceeded:

- stop trace expansion
- fall back to method JIT if available
- fall back to interpreter
- emit telemetry

---

# 14. Mixing Traces with Method JIT

Even with strong tracing, DVM should retain method-level compilation.

---

## 14.1 Method graph as fallback

Method DGW graphs provide:

- cold paths
- exception paths
- rare branches
- full function coverage
- stable fallback for trace exits

---

## 14.2 Trace entry from method code

A compiled method can jump into a trace at a hot loop.

The method code must prepare:

- trace entry state signature
- stack frame compatibility
- GC maps
- return continuation

---

## 14.3 Trace exit to method code

A trace side exit can return to method code at:

- loop exit
- call continuation
- exception handler
- function return

This requires precise mapping from trace state to method frame state.

---

## 14.4 Method graph as trace source

Tier 2 can use PGO to extract traces from method DGW without dynamic recording.

This is useful when:

- dynamic tracing is too expensive
- code is already compiled
- profile is very stable
- AOT is being performed

---

# 15. OSR with Traces

OSR is essential for long-running loops.

---

## 15.1 OSR into trace

When interpreter or baseline code detects a hot loop, DVM can OSR directly into compiled trace code.

Requirements:

- map current interpreter/baseline state to trace entry signature
- materialize virtual objects if trace expects real objects
- preserve exception state
- preserve tooling state
- preserve suspension state

---

## 15.2 OSR out of trace

If a trace guard fails and no bridge exists, execution may OSR back to:

- interpreter loop header
- baseline loop header
- method loop header

State must be exact.

---

# 16. Deopt and FrameState in Trace World

Trace guards still require FrameState.

However, traces introduce additional state concerns.

---

## 16.1 Guard FrameState

A trace guard FrameState must reconstruct the lower-tier state at the guard’s guest bytecode offset.

This includes:

- guest registers
- closure cells
- exception state
- tracing/profiling state
- suspension state
- materialized virtual objects

---

## 16.2 Meta-trace FrameState

For meta-traces, FrameState has two layers:

1. interpreter state
2. guest frame projection

If a meta-trace side exit occurs before lifting or in low-level trace code, DVM may first reconstruct interpreter state, then resume Tier 0.

---

## 16.3 Exit vs deopt

Not every trace side exit is a deopt.

A side exit is a deopt only if it reconstructs a lower-tier execution state rather than jumping to another compiled trace or method continuation.

Examples:

- root trace to bridge: not necessarily deopt
- trace to interpreter: deopt-like
- trace to method continuation: partial deopt
- trace to deopt stub: full deopt

---

# 17. PGO for Traces

Traces depend heavily on profile confidence.

---

## 17.1 Trace-specific profile data

Collect:

- loop header heat
- path taken frequency
- branch stability
- side exit frequency
- bridge frequency
- state signature stability
- shape stability
- type stability
- call target stability
- allocation site stability
- range stability
- deopt correlation

---

## 17.2 Confidence model

Confidence must include:

- sample count
- stability over time
- decay
- variance
- side-exit correlation
- previous trace failure history

Low confidence prevents:

- aggressive trace formation
- deep inlining
- vectorization
- strong shape promotion
- PEA materialization risk

---

# 18. Invalidation and Trace Trees

Trace trees have many dependencies.

---

## 18.1 Dependency kinds

Traces depend on:

- guest code version
- bytecode version
- shape version
- type version
- method version
- global version
- module version
- builtin version
- inline cache version
- profile version
- runtime configuration
- GC object model
- Guest Language Profile version

---

## 18.2 Invalidation behavior

When a dependency changes:

1. Mark affected traces invalid.
2. Patch trace entries to fallback.
3. Allow running traces to exit safely.
4. Invalidate bridges dependent on the trace.
5. Retire code after quiescence.
6. Record telemetry.

---

# 19. Trace Compiler Data Structures

Detailed C++26 sketch:

```cpp
enum class TraceKind : uint8_t {
    MetaInterpreter,
    GuestNormal,
    Native,
    Bridge,
    Static
};

enum class TraceStateKind : uint8_t {
    Recording,
    Compiled,
    Installed,
    Invalid,
    Blacklisted
};

struct TraceStateSignature {
    SmallVector<ValueAbstract, 16> live_values;
    ExceptionStateAbstract exception_state;
    ToolingStateAbstract tooling_state;
    SuspensionStateAbstract suspension_state;
};

struct TraceGuardRecord {
    NodeId guard_node;
    FrameStateId frame_state;
    GuardKind kind;
    Confidence confidence;
    DependencySetId dependencies;
    ExitTargetId exit_target;
};

struct TraceExitRecord {
    ExitId id;
    GuardId guard;
    ExitTargetKind target_kind;
    TargetId target;
    FrameStateId frame_state;
    MaterializationPlanId materialization;
};

struct TraceFragment {
    TraceId id;
    TraceKind kind;
    TraceStateKind state;

    GuestFunctionId function;
    BytecodeOffset entry_offset;
    FrameStateId entry_frame_state;
    TraceStateSignature entry_signature;

    DGWGraph graph;

    SmallVector<TraceGuardRecord, 16> guards;
    SmallVector<TraceExitRecord, 8> exits;
    SmallVector<TraceId, 4> bridges;
    DependencySetId dependencies;

    TelemetryCounters counters;
};
```

---

# 20. Recorder API Sketch

```cpp
class TraceRecorder {
public:
    [[nodiscard]] std::expected<TraceId, TraceError>
    start_root_trace(const TraceStartRequest& request);

    [[nodiscard]] std::expected<void, TraceError>
    record_guest_op(GuestTraceOp op);

    [[nodiscard]] std::expected<void, TraceError>
    record_meta_op(MetaTraceOp op);

    [[nodiscard]] std::expected<void, TraceError>
    record_guard(TraceGuardKind kind, NodeId condition, FrameStateId state);

    [[nodiscard]] std::expected<void, TraceError>
    record_side_exit(ExitTargetId target, FrameStateId state);

    [[nodiscard]] std::expected<void, TraceError>
    record_call(CallSiteId site, CallTargetId target);

    [[nodiscard]] std::expected<void, TraceError>
    record_allocation(AllocSiteId site, RegionId region);

    [[nodiscard]] std::expected<TraceClosure, TraceError>
    attempt_loop_close(const CurrentStateSignature& current);

    [[nodiscard]] std::expected<void, TraceError>
    abort_trace(TraceAbortReason reason);
};
```

---

# 21. Interpreter Annotation API

The interpreter must expose trace hints.

Example:

```cpp
struct PromotionHint {
    enum class Kind {
        Constant,
        Shape,
        Type,
        IntRange,
        NonNull,
        CallTarget
    };

    Kind kind;
    SymbolId name;
    NodeId value;
    Confidence confidence;
};

struct VirtualizationHint {
    AllocSiteId site;
    bool may_scalarize;
    bool may_materialize_on_exit;
    SmallVector<SymbolId, 8> observable_fields;
};
```

Interpreter handler example conceptually:

```cpp
handler(ADD_INT) {
    trace.begin_guest_op(BytecodeOpcode::ADD_INT);
    trace.read_guest_reg(rA);
    trace.read_guest_reg(rB);
    trace.guard_int_range(rA);
    trace.guard_int_range(rB);
    trace.write_guest_reg(rC, add_int(rA, rB));
    trace.frame_state_at(current_pc);
    trace.end_guest_op();
}
```

This allows normal tracing directly from Tier 0.

---

# 22. Meta-Tracing Limitations and Rules

Meta-tracing is powerful but dangerous if undisciplined.

---

## Rule M-1 — No opaque C++ semantics inside hot trace recording

The recorder must not be forced to trace through:

- C++ exceptions
- RTTI
- virtual dispatch with unknown semantics
- opaque library calls
- global locks
- non-deterministic system calls

Such operations must be side exits or explicitly modeled trace helpers.

---

## Rule M-2 — Interpreter handlers must be traceable or exit

If a bytecode handler cannot be expressed as traceable micro-ops, it must side exit.

---

## Rule M-3 — Meta-traces must be liftable

A meta-trace that cannot be lifted to guest semantic form is only allowed as a low-tier Tier 1 trace.

It must not enter full Tier 2 optimization unless lifted.

---

## Rule M-4 — Guest state projection must be exact

The mapping from interpreter VM state to guest state must be verified.

Incorrect projection causes silent semantic bugs.

---

## Rule M-5 — Meta-traces must reconstruct interpreter state on failure

If a low-level meta-trace fails before guest lifting, it must restore the interpreter state exactly.

---

# 23. Normal Tracing Rules for Higher Tiers

## Rule N-1 — Higher-tier traces must be guest-semantic

Tier 2 traces should be expressed in guest DGW form, not interpreter micro-ops, except where VM operations are required for correctness.

---

## Rule N-2 — Trace guards must be tied to FrameState

Every trace guard must have an exact FrameState.

---

## Rule N-3 — Trace closure requires compatible state signatures

A trace loop may close only if the loop header state signature is compatible.

---

## Rule N-4 — Side exits must be profiled

Every side exit must record:

- exit count
- reason
- target
- cost
- bridge status

---

## Rule N-5 — Trace expansion must be budgeted

Trace trees must not grow without bound.

Limits required for:

- code size
- bridge count
- compile time
- memory usage
- guard density

---

# 24. Testing Requirements for Hybrid Tracing

Tracing introduces new failure modes. DVM testing must cover them.

---

## 24.1 Meta-trace lifting tests

Verify that raw meta-traces lift to correct guest traces.

Check:

- dispatch elimination
- PC folding
- VM-to-guest register projection
- guard preservation
- FrameState correctness

---

## 24.2 Trace closure tests

Verify:

- loop closes with compatible state
- loop refuses closure with incompatible state
- bridge traces form correctly
- loop-carried values are correct

---

## 24.3 Side-exit tests

Force every guard to fail.

Verify:

- interpreter fallback
- baseline fallback
- bridge activation
- materialization correctness
- exception state correctness
- tooling state correctness

---

## 24.4 Trace tree stress tests

Create:

- many bridges
- deep trace trees
- conflicting state signatures
- frequent invalidations
- GC during trace compilation
- OSR during trace patching

---

## 24.5 Differential trace tests

Compare:

- Tier 0 interpreter
- Tier 1 meta-trace
- Tier 2 guest trace
- Tier 3 static trace
- guest reference implementation

All must be observationally equivalent.

---

## 24.6 Deopt and trace replay

Failed trace compilations must save:

- raw trace buffer
- lifted DGW trace
- PGO snapshot
- state signatures
- guard records
- exit records
- compiler flags
- RNG seed
- dependency versions

---

# 25. Revised Architecture Diagram

```text
                   Guest Language Frontend
                            |
                            v
                   DVM Portable Bytecode
                            |
                            v
          +----------------------------------+
          | Tier 0 Register Interpreter      |
          |  - direct-threaded               |
          |  - traceable handlers            |
          |  - profiling                     |
          +----------------+-----------------+
                           |
             +-------------+-------------+
             |                           |
             v                           v
   Meta-Trace Recorder          Normal Trace Recorder
   (interpreter micro-ops)      (guest semantic ops)
             |                           |
             v                           v
       Raw Meta-Trace              Raw Guest Trace
             |                           |
             v                           |
        Trace Lifter                     |
             |                           |
             +-------------+-------------+
                           |
                           v
                    DGW/Trace Graph
                           |
                           v
                  Trace Optimization
                  - guard optimization
                  - PEA
                  - vectorization
                  - bridge formation
                  - FrameState finalization
                           |
                           v
                    Native Trace Code
                           |
          +----------------+----------------+
          |                |                |
          v                v                v
     Root Trace       Bridge Traces     Trace Exits
          |                |                |
          +--------+-------+                |
                   |                        |
                   v                        v
             Trace Tree Linking      Interpreter/Baseline/Method
                   |
                   v
              Trace Cache
```

---

# 26. Final Design Conclusion

If DVM uses **meta-tracing mixed with normal tracing**, the correct IR is not a plain method Sea of Nodes.

The correct IR is:

> **DGW/Trace**  
> A trace-first Dynamic Graph Web with first-class regions, references, guards, side exits, FrameStates, and state signatures.

Meta-tracing provides:

- fast interpreter specialization
- low-latency warmup
- dispatch elimination
- VM-level insight

Normal tracing provides:

- high-level guest semantic optimization
- PEA/EA
- vectorization
- trace trees
- strong hot-loop performance

Method graphs provide:

- fallback
- cold paths
- exception paths
- trace stitching
- static trace extraction

The result is a DVM that can be both highly dynamic and extremely fast.

The key implementation priorities are:

1. Make the interpreter traceable.
2. Build a precise meta-to-guest lifting pipeline.
3. Use DGW/Trace as the core optimizing representation.
4. Treat side exits and bridges as first-class compilation units.
5. Require FrameState and dependency records for every trace guard.
6. Budget trace growth aggressively.
7. Test forced side exits and trace invalidation relentlessly.
