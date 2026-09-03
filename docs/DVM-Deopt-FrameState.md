# DVM Deoptimization and FrameState Machinery

This is a normative subsystem design for the **Dynamic Virtual Machine (DVM)**.

This subsystem is one of the most critical correctness systems in DVM. It is
the mechanism that allows DVM to speculate aggressively while still preserving
exact guest-language semantics.

The golden rule is:

> If speculation fails, DVM must resume execution in a state observationally
> indistinguishable from the state the lower tier, usually Tier 0, would have
> reached at that point.

This design covers:

1. Deoptimization principles
2. FrameState semantics
3. Guard integration
4. Virtual object materialization
5. Partial escape analysis integration
6. Inlined frame reconstruction
7. OSR entry and exit
8. Exception state reconstruction
9. Suspension state reconstruction
10. GC interaction
11. Metadata layout
12. Runtime deoptimization flow
13. Telemetry and throttling
14. Verification
15. Testing requirements

---

# 1. Purpose of the Deoptimization System

The DVM deoptimization system exists to make aggressive optimization safe.

DVM Tier 2 may assume many things based on PGO:

- this value has a specific type
- this object has a specific shape
- this call site is monomorphic
- this integer remains in range
- this pointer does not alias another pointer
- this container layout is stable
- this branch is almost always taken
- this method target will not change
- this module/global version will not change

Every one of those assumptions can fail.

When an assumption fails, DVM must not:

- crash
- continue with wrong state
- silently disable correctness
- reorder visible effects incorrectly
- lose exception state
- lose frame introspection state
- lose debugging/profiling state
- expose partially materialized objects

Instead, DVM must:

1. detect the failed assumption with a guard
2. transfer control to the deoptimization runtime
3. reconstruct the exact lower-tier state
4. resume execution in a lower tier
5. record telemetry
6. optionally invalidate compiled code
7. optionally recompile with weaker assumptions

---

# 2. Normative Laws Governing This System

This subsystem is directly governed by the DVM laws, especially:

## Rule 4 — Deoptimization Must Reconstruct Tier 0 State

Guard failure must restore the exact Tier 0 state.

## Rule 5 — FrameState is Mandatory for All Guards

Every speculative assumption must have a FrameState attachment.

## Rule 81 — Deoptimization Metadata Is a Required Compilation Output

A compilation is incomplete without deopt metadata.

## Rule 82 — FrameState Must Be Complete and Machine-Checkable

The verifier must reject incomplete FrameStates.

## Rule 83 — Guard Failure Must Produce Exact Lower-Tier State

Optimistic execution must not create observable divergence.

## Rule 84 — Deopt Loops Must Be Detected and Throttled

Repeated deopt must trigger adaptive behavior.

## Rule 85 — Speculative Side Effects Must Be Reversible or Deferred

Irreversible guest-visible effects cannot be speculated illegally.

## Rule 86 — All GC References in JIT Code Must Be Tracked

Deopt must preserve GC safety.

## Rule 116 — OSR Entry and Exit Must Be Semantically Exact

OSR deopt must preserve exact program state.

---

# 3. Core Definitions

## 3.1 Guard

A **guard** is a runtime check that validates a speculative assumption.

Examples:

- type guard
- shape guard
- bounds guard
- null guard
- overflow guard
- alias guard
- call target guard
- receiver guard
- global version guard
- module version guard
- class hierarchy guard
- FFI proof guard

A guard has:

- checked values
- expected condition
- success path
- failure path
- deopt reason
- dependency set
- FrameState attachment

---

## 3.2 Deopt Point

A **deopt point** is a machine-code location where execution may transfer from
optimized code to a lower tier.

Deopt points include:

- guard failure branches
- uncommon traps
- OSR exit points
- allocation failure paths
- debug/profiling forced fallback points
- exception transition points where required
- suspension point fallbacks

Each deopt point must have:

- machine PC or patchpoint identity
- FrameState ID
- deopt reason category
- target tier
- metadata for reconstruction

---

## 3.3 FrameState

A **FrameState** is a machine-checkable snapshot describing how to reconstruct
lower-tier execution state.

A FrameState describes:

- guest bytecode offset
- source position
- register-to-interpreter mappings
- stack slot mappings
- primitive values
- object references
- constants
- closure cells
- free variables
- global/module version
- builtin/runtime version
- exception state
- tracing/profiling state
- suspension state
- virtual object graph
- inlined frame stack
- dependency state

A FrameState is not merely a list of values. It is a reconstruction plan.

---

## 3.4 Virtual Object

A **virtual object** is an object that has been eliminated by escape analysis
or partial escape analysis.

The object does not currently exist in the heap. Its fields are represented by
SSA values, constants, or other virtual objects.

If deopt requires the object to exist, the object must be materialized.

---

## 3.5 Materialization

**Materialization** is the process of creating real runtime objects from
virtual object state.

Materialization must produce objects that are:

- correctly initialized
- correctly typed
- correctly shaped
- safely visible to GC
- correctly linked
- registered with finalizers/weak references only when legal
- identity-safe where required

---

## 3.6 Deopt Target

A **deopt target** is the lower-tier execution state that DVM resumes after
deoptimization.

Usually this is:

- Tier 0 interpreter

But possible targets include:

- Tier 0 interpreter
- Tier 1 baseline code, if safe and supported
- another lower-tier compiled variant, if policy allows

The default correctness target is Tier 0.

---

# 4. High-Level Deoptimization Flow

The high-level flow is:

```text
optimized code executing
        |
        v
guard fails
        |
        v
transfer to deopt stub
        |
        v
capture machine state
        |
        v
locate deopt metadata
        |
        v
resolve FrameState
        |
        v
materialize virtual objects
        |
        v
reconstruct guest frame stack
        |
        v
restore exception / tracing / monitoring state
        |
        v
transfer control to Tier 0
        |
        v
record telemetry and update policy
```

The runtime must ensure that from the guest program’s perspective, execution
continues as if the optimized code had never made a wrong assumption.

---

# 5. FrameState Design Goals

The FrameState system must satisfy the following goals.

## 5.1 Exactness

A FrameState must describe enough state to reconstruct the exact lower-tier
world.

This includes guest-visible state and runtime-internal state needed for
correct continuation.

## 5.2 Machine-Checkability

FrameStates must be verifiable by the compiler and runtime.

The verifier must reject:

- missing mappings
- dangling NodeIds
- incomplete virtual object graphs
- missing GC maps
- missing exception state
- missing closure cells
- missing inlined frame parents
- missing dependency records

## 5.3 Compactness

FrameState metadata can become large.

DVM must use:

- deduplication
- interned symbols
- compressed offsets
- shared constant pools
- structural sharing
- lazy decoding where safe

But compactness must never reduce correctness.

## 5.4 Replayability

A FrameState must be replayable from compiled metadata.

If a deopt failure occurs in CI, the replay artifact must contain enough
information to reconstruct the event.

## 5.5 GC Safety

FrameState reconstruction must not leave object references untracked.

All references must be visible to GC through:

- stack maps
- handles
- register maps
- materialization contexts

## 5.6 Low Guard Overhead

Guard success is hot.

Guard failure is rare.

Therefore:

- guard success path must be cheap
- FrameState references must not burden normal execution
- deopt metadata may be out-of-line
- materialization logic may be slow-path-only

---

# 6. FrameState Kinds

DVM defines several FrameState kinds.

## 6.1 `GuardFrameState`

Attached to a speculative guard.

This is the most common FrameState kind.

It describes the state to reconstruct if the guard fails.

Examples:

- type guard failure
- shape guard failure
- bounds guard failure
- alias guard failure
- call target guard failure

---

## 6.2 `CallFrameState`

Attached around calls.

It describes state before or after a call.

Call FrameStates are needed for:

- inlined calls
- exception reconstruction
- stack traces
- debugger frames
- profiler call events
- deopt inside callees

---

## 6.3 `OSREntryFrameState`

Describes state when entering optimized code from the interpreter at a non-entry
point, usually a loop header.

---

## 6.4 `OSRExitFrameState`

Describes state when leaving optimized code back to the interpreter, usually
from a loop.

---

## 6.5 `ExceptionFrameState`

Describes state where an exception may be in flight.

It includes:

- exception object
- exception context/chain where supported
- traceback state
- handler search state
- cleanup state where relevant

---

## 6.6 `SuspensionFrameState`

Describes suspended execution for:

- generators
- coroutines
- async functions
- fibers
- continuations, if supported

It includes:

- resume point
- suspended locals
- iterator/await state
- exception state
- cleanup state
- frame identity

---

## 6.7 `RuntimeCallFrameState`

Describes state before calling into a runtime helper that may deoptimize,
allocate, trigger GC, or transition back to guest code.

---

# 7. FrameState Execution Point Semantics

A FrameState must specify an exact execution point.

This includes:

- guest function ID
- bytecode offset
- source position
- inline context
- reexecution policy

## 7.1 Reexecution Policy

DVM defines three reexecution policies:

### `ResumeAt`

Interpreter resumes exactly at the bytecode offset.

This is the default for guards placed before an operation.

### `ResumeAfter`

Interpreter resumes after the bytecode offset.

This may only be used if all effects of the operation have been committed or
are proven non-observable.

### `ResumeOSR`

Interpreter resumes at an OSR entry point, usually a loop header.

This is used for OSR exit.

## 7.2 Conservative Default

If there is any doubt, DVM must use `ResumeAt`, not `ResumeAfter`.

It is safer to recompute pure work than to skip an observable effect.

---

# 8. FrameState Contents

A complete FrameState contains the following components.

---

## 8.1 Guest Location

This identifies where execution should resume.

Fields:

- guest module ID
- guest function ID
- bytecode offset
- source file symbol
- source line
- source column
- inline depth
- inline parent FrameState ID, if any

---

## 8.2 Interpreter Register Map

DVM Tier 0 is a register interpreter.

Therefore FrameState must map optimized values into interpreter registers.

For each interpreter register required by the lower-tier frame:

- interpreter register index
- value kind
- value location
- value type
- GC reference flag

Value locations may be:

- optimized machine register
- optimized stack slot
- constant pool entry
- immediate
- virtual object ID
- materialized object handle
- closure cell ID
- runtime global slot
- unused/undefined, if guest semantics allow

---

## 8.3 Stack Slot Map

Some lower-tier state may live in stack slots rather than interpreter
registers.

This includes:

- temporary values
- exception temporaries
- call arguments in transition
- OSR loop state
- debug-visible slots

For each slot:

- slot index
- value kind
- value location
- type
- GC reference flag

---

## 8.4 Closure and Free Variable Map

If the guest function uses closures, FrameState must describe:

- captured cells
- free variables
- cell values
- cell mutability
- cell ownership

Closure cells may be:

- live object references
- primitive values
- virtualized values
- constants

---

## 8.5 Global / Module / Builtin Version State

Many speculative assumptions depend on versioned global state.

FrameState must record the versions assumed at the deopt point:

- module version
- global dictionary version
- builtin/runtime library version
- type/class version
- shape version
- method resolution version
- descriptor version
- import state version

This is not only for invalidation. It is also for validation during deopt.

If versions changed between guard evaluation and deopt reconstruction, the
runtime must reconstruct the state appropriate for the current lower-tier
execution point.

---

## 8.6 Exception State

If an exception may be live, FrameState must describe:

- exception object
- exception type
- exception value
- traceback state
- exception chain/context where supported
- active handler context
- cleanup stack, if guest language requires it
- source position for traceback

If no exception is live, FrameState must explicitly mark exception state empty.

---

## 8.7 Tracing / Profiling / Monitoring State

If tooling is active, FrameState must describe:

- tracing enabled flag
- profiler enabled flag
- monitoring event mask
- current event suppression state
- last event kind, where required
- debugger step state, where supported

This prevents duplicate or missing tooling events after deopt.

---

## 8.8 Suspension State

For suspended guest constructs, FrameState must describe:

- suspension kind
- resume bytecode offset
- suspended local values
- iterator state
- awaited value state
- pending exception state
- close/cancel state
- finalization state

Suspension FrameStates are mandatory wherever suspension points are deopt
points.

---

## 8.9 Virtual Object Graph

If escape analysis eliminated objects, FrameState must describe how to
materialize them.

Each virtual object entry contains:

- virtual object ID
- allocation site ID
- guest type/class ID
- shape ID
- field values
- array elements, if applicable
- identity constraints
- finalization constraints
- weak-reference constraints
- reference-count constraints, if observable
- initialization state

---

## 8.10 Inlined Frame Stack

If DVM inlined functions, FrameState must describe the full inline stack.

For each inlined frame:

- function ID
- bytecode offset
- source position
- local map
- closure map
- receiver/self value, if applicable
- exception state for that frame
- call site identity
- return bytecode offset in caller

The deoptimizer must reconstruct guest frames in the correct order.

---

## 8.11 Dependency Snapshot

FrameState may reference dependency versions that were assumed.

Examples:

- type version
- shape version
- global version
- module version
- builtin version
- profile version

This snapshot is used for:

- validation
- telemetry
- debugging
- replay

---

## 8.12 Materialization Budget

FrameState should include cost metadata:

- number of objects to materialize
- maximum graph depth
- estimated allocation count
- whether materialization may trigger GC
- whether materialization may run finalizers
- whether materialization may call guest code

This allows the runtime to prepare and allows the compiler to avoid dangerous
speculation.

---

# 9. Compile-Time FrameState Construction

FrameState construction occurs throughout the optimizing pipeline.

---

## 9.1 Initial FrameState Construction

Pass:

- `P-011: FrameState Construction Pass`

During graph building, DVM creates initial FrameStates for:

- calls
- runtime transitions
- exception-capable operations
- suspension points
- deopt-capable checks
- OSR entries

At this stage, FrameStates are conservative and usually complete.

---

## 9.2 FrameState Updates After Inlining

When inlining occurs, FrameStates must be updated to include inline frame
stacks.

If function `A` inlines `B`, and `B` contains a guard, the guard FrameState
must include:

- frame for `B`
- frame for `A`
- mappings for values from both frames

---

## 9.3 FrameState Updates After EA/PEA

Escape analysis may replace real objects with virtual values.

When that happens, FrameStates must be updated:

- real object references become virtual object references
- materialization plans are attached
- field values are recorded
- object initialization state is recorded

This is mandatory.

A pass that scalarizes an object without updating FrameStates is incorrect.

---

## 9.4 FrameState Minimization

Pass:

- `P-504: Deopt State Minimization`

This pass reduces FrameState size while preserving reconstructability.

Allowed minimizations:

- remove unused interpreter registers if guest frame does not require them
- share constant values
- deduplicate identical FrameStates
- compress value locations
- remove dead virtual object fields if unobservable

Forbidden minimizations:

- removing values needed for traceback reconstruction
- removing values needed for debugger/frame inspection
- removing values needed for reference-count-visible semantics
- removing values needed for exception state
- removing values needed for tracing/profiling correctness

---

## 9.5 FrameState Deduplication

Pass:

- `P-505: FrameState Deduplication`

Many guards share identical FrameStates.

DVM should deduplicate structurally:

- same function
- same bytecode offset
- same inline stack
- same value mappings
- same virtual object graph
- same exception state

Deduplication must be content-addressed.

---

## 9.6 Final FrameState Emission

Pass:

- `P-572: Deopt Metadata Finalization`

This pass converts compiler-internal FrameState graphs into final runtime
metadata.

Output includes:

- FrameState table
- virtual object table
- value location table
- inline frame table
- GC map table
- dependency table
- deopt point table

---

# 10. Guard Integration

Every guard must be connected to FrameState machinery.

---

## 10.1 Guard Metadata

Each guard node must contain:

```cpp
struct GuardMetadata {
    GuardKind kind;
    NodeId guarded_value_0;
    NodeId guarded_value_1;
    SourcePosition source_position;
    ProfileSource profile_source;
    Confidence confidence;
    DependencySet dependencies;
    FrameStateId frame_state;
    DeoptReason deopt_reason;
    GuardPlan plan;
    bool affects_exception_state;
    bool affects_tracing_state;
    bool affects_suspension_state;
};
```

---

## 10.2 Guard Kinds and Required FrameState Details

### Type Guard

Requires:

- FrameState at checked operation
- type dependency
- receiver/value mapping

### Shape Guard

Requires:

- shape version dependency
- object mapping
- layout metadata

### Bounds Guard

Requires:

- array/container mapping
- index value
- bounds state
- FrameState before failing access

### Null Guard

Requires:

- value mapping
- FrameState before null-dependent operation

### Overflow Guard

Requires:

- numeric operation mapping
- overflow semantics of guest language
- FrameState before operation

### Alias Guard

Requires:

- pointer/base mappings
- alias dependency
- FrameState before memory operation

### Call Target Guard

Requires:

- call site mapping
- receiver mapping
- direct target dependency
- FrameState before call

### Global/Module Version Guard

Requires:

- global/module version dependency
- namespace mapping
- FrameState before operation

---

## 10.3 Guard Placement Rule

A guard must be placed before the first irreversible guest-visible effect that
depends on the guarded assumption.

If the guarded assumption is used to eliminate a check, inline a call, reorder
memory access, or scalarize an object, the guard must dominate that
transformation and must fail before any unsafe effect commits.

---

## 10.4 Guard Failure Branches

Guard failure branches must not be ordinary cold branches only.

They must carry:

- deopt stub target
- deopt reason
- FrameState ID
- patchpoint metadata, if applicable

---

# 11. Virtual Object Materialization Design

This is one of the most delicate parts of the system.

---

## 11.1 When Materialization Is Required

Materialization is required when lower-tier execution needs a real object that
was virtualized.

Examples:

- interpreter expects an object in a register
- deopt occurs before an operation that observes the object
- exception traceback needs the object
- debugging tool requests frame locals
- native transition requires real object
- suspension state requires object
- weak reference/finalizer semantics require object existence

---

## 11.2 When Materialization Is Forbidden

Materialization is not a free escape hatch.

If an object could have been observed before deopt, it must not have been
virtualized unless DVM can prove no observation occurred.

Forbidden cases:

- object identity was observed
- weak reference was registered
- finalizer was registered
- object escaped to native code
- object became visible through debugging hooks
- object was stored into an escaping location
- object became reachable through GC introspection
- reference count was observed, if guest exposes it

If any of these are possible, EA/PEA must not virtualize the object.

---

## 11.3 Virtual Object Representation

During compilation:

```cpp
struct VirtualObjectState {
    VirtualObjectId id;
    AllocSiteId allocation_site;
    TypeId guest_type;
    ShapeId shape;
    SmallVector<VirtualField, 8> fields;
    SmallVector<VirtualElement, 4> elements;
    bool has_finalizer;
    bool has_weakref_observation;
    bool identity_observed;
    bool refcount_observed;
    InitializationState init_state;
};
```

Each field:

```cpp
struct VirtualField {
    SymbolId field_name;
    FieldOffset offset;
    ValueLocation value;
};
```

Each element:

```cpp
struct VirtualElement {
    uint32_t index;
    ValueLocation value;
};
```

---

## 11.4 Materialization Ordering

Materialization must handle cycles.

The algorithm is:

1. Collect all virtual objects reachable from the FrameState.
2. Topologically sort where possible.
3. Allocate all objects before filling references where cycles exist.
4. Register all allocated objects in a materialization handle table.
5. Fill fields/elements using resolved values.
6. Apply write barriers where required.
7. Finalize initialization state.
8. Register finalizers/weakrefs only if legal and required.
9. Replace virtual object references in FrameState with real handles.
10. Verify GC visibility.

---

## 11.5 Allocation During Materialization

Materialization may allocate objects.

This allocation must occur through a controlled runtime path.

Requirements:

- allocation failure must be handled safely
- GC may occur
- allocated objects must be tracked immediately
- partially initialized objects must not escape incorrectly
- guest-visible allocation hooks must only run if semantically required
- if allocation triggers finalizers, those finalizers must not observe invalid
  state

If the runtime cannot guarantee safe materialization under GC, the compiler
must not virtualize those objects.

---

## 11.6 Object Identity During Materialization

If an object was legal to virtualize, its identity was not observed before
deopt.

Therefore materialization may create a new object identity.

However:

- if identity becomes observable at or after deopt, the materialized object
  must behave consistently from that point forward
- if the guest language requires stable identity across certain operations, the
  runtime must preserve it after materialization
- if the object has an address-observable capability, the object must be pinned
  or represented via handles as required

---

## 11.7 Finalizers and Weak References

Virtual objects must not have active finalizers or weak references unless the
Guest Language Profile and EA prove that this is safe.

If materialization creates an object that requires a finalizer:

1. allocate object
2. initialize fields
3. make it safe for GC
4. register finalizer only after object is fully initialized

If weak references are part of the guest state, they must only observe a valid
object.

---

## 11.8 Reference Count Semantics

If the guest language exposes reference counts:

- materialized objects must receive correct initial reference counts
- counts must match the lower-tier state that would have existed
- if exact reference-count reconstruction is impossible, the optimization must
  be rejected or the feature disabled

Reference-count-visible behavior is a semantic fidelity issue, not an
implementation detail.

---

# 12. Inlined Frame Reconstruction

Inlining is essential for performance, but it complicates deopt.

---

## 12.1 Inline Frame Stack

A FrameState inside an inlined callee must describe the full frame stack.

Example:

```text
caller: function A
callee: function B
inner: function C
```

If deopt occurs inside inlined `C`, FrameState must reconstruct:

- frame C
- frame B
- frame A

Each frame must have:

- function identity
- bytecode offset
- locals
- closure cells
- receiver/self where applicable
- exception state
- source position

---

## 12.2 Reconstruction Order

The deoptimizer usually reconstructs from innermost to outermost or outermost
to innermost depending on runtime frame-linking requirements.

The semantic requirement is:

- stack traces must show correct frame order
- exceptions must unwind correctly
- debugger must see correct frames
- local variables must be correct in each frame

DVM must choose one canonical reconstruction order and verify it.

---

## 12.3 Return Addresses and Bytecode Offsets

For each inlined frame except the innermost, FrameState must include the
return bytecode offset in the caller.

This is required for:

- tracebacks
- exception unwinding
- profiling return events
- debugger step-out
- OSR-like reconstruction

---

## 12.4 Inlined Exception Frames

If an exception occurs inside an inlined frame, the deoptimizer must
reconstruct exception state as if the inlined call were a real call.

This includes:

- exception object
- traceback frames
- cleanup handlers
- finally/scope-exit constructs
- exception chaining/context

---

## 12.5 Inlined Frame Locals

If frame locals are requested after deopt, every inlined frame must be able to
provide correct locals.

This means FrameState cannot discard values that may be introspected.

If a value is dead but introspectable, DVM must either:

- preserve it
- recompute it in lower tier
- materialize it
- disable the optimization for that scope

---

# 13. OSR Deoptimization

On-stack replacement has special deopt requirements.

---

## 13.1 OSR Entry State

When entering optimized code from Tier 0 at a loop header, DVM creates an OSR
entry FrameState.

This FrameState records:

- interpreter register file at loop header
- loop induction variables
- iterator state
- exception state
- closure cells
- active suspension state, if any
- current source position

---

## 13.2 OSR Exit State

If optimized OSR code must deoptimize, it must return to the interpreter at a
semantically exact point.

Usually this is:

- loop header
- loop backedge
- safe loop exit point

The OSR exit FrameState must reconstruct:

- current loop induction values
- interpreter registers
- iterator state
- exception state
- frame locals
- suspension state if supported

---

## 13.3 OSR and Partial Escape

If PEA virtualized objects inside an OSR loop, OSR exit may require
materialization.

Example:

- loop allocates temporary object
- object does not escape on hot path
- PEA scalarizes it
- loop exits through cold path where object escapes
- OSR exit or deopt occurs

The deoptimizer must materialize the object exactly as required by the exit
path.

---

## 13.4 OSR Deopt Must Preserve Loop Semantics

OSR deopt must not:

- skip loop iterations
- duplicate loop iterations
- lose induction variable updates
- lose iterator advancement
- lose exception state
- change loop termination behavior

---

# 14. Exception State Reconstruction

Guest exceptions are first-class semantic values.

---

## 14.1 Exception Object Reconstruction

If an exception is live at deopt, FrameState must provide:

- exception object
- exception type
- exception value/message
- traceback state
- exception chain/context where supported
- notes/annotations where supported

If the exception object was virtualized, it must be materialized.

---

## 14.2 Traceback Reconstruction

Tracebacks must show the correct guest frames.

If DVM inlined functions, the traceback must still show the inlined functions
as if they were real frames, unless the Guest Language Profile explicitly
defines different traceback semantics.

Traceback reconstruction requires:

- function IDs
- bytecode offsets
- source positions
- frame links

---

## 14.3 Cleanup and Finally Semantics

If deopt occurs near scope-exit constructs, FrameState must preserve the state
needed to execute cleanup correctly.

Examples:

- finally blocks
- using/dispose constructs
- defer-like constructs
- with-block cleanup
- catch handlers
- try-state stacks

---

## 14.4 Exceptions During Materialization

Materialization itself may fail, for example due to allocation failure.

If materialization fails, DVM must not corrupt guest state.

Possible policies:

1. If the guest language would raise a memory error at this point, raise the
   correct guest exception after reconstructing as much state as possible.
2. If the runtime cannot safely continue, treat this as a P0 VM bug.

Silent wrong behavior is forbidden.

---

# 15. Suspension State Reconstruction

Suspension constructs are semantic boundaries.

---

## 15.1 Suspension Points

Suspension points include:

- yield
- await
- coroutine suspend
- fiber switch
- continuation capture/resume, if supported

Each suspension point must be a valid deopt/safepoint candidate if JIT code
may be active there.

---

## 15.2 Suspension FrameState Contents

A Suspension FrameState must contain:

- suspended function ID
- resume bytecode offset
- local values
- iterator state
- awaited value
- pending exception
- close/cancel state
- finalization state
- caller/resumer identity where required

---

## 15.3 Deopt at Suspension

If a guard fails at a suspension point, DVM must reconstruct a valid suspended
lower-tier object/frame.

The guest program must be able to resume, close, throw into, or inspect the
suspended construct correctly.

---

## 15.4 Deopt After Resume

If optimized code resumes a suspended construct and later deoptimizes, the
FrameState must reflect post-resume state, not stale suspension state.

---

# 16. GC Integration

Deopt must be GC-safe at every phase.

---

## 16.1 Stack Maps

Every deopt point must have stack map information describing live GC
references.

Stack maps identify:

- registers containing object references
- stack slots containing object references
- spill slots containing object references
- FrameState references requiring liveness

---

## 16.2 GC During Deopt

GC may occur during materialization.

Therefore:

- all newly allocated materialized objects must be tracked immediately
- all partially initialized objects must be in a safe state
- all interpreter frame references must be visible
- all optimized-code references still live must be covered by stack maps

---

## 16.3 Moving GC

If DVM uses a moving GC:

- materialized object references must use handles or be updated after movement
- FrameState reconstruction must not depend on stale addresses
- identity-address observable objects must be pinned or use stable identity
  mechanisms

---

## 16.4 Write Barriers

Materialization stores must execute correct write barriers.

If a materialized object stores a reference into another object, the GC write
barrier must run if required.

Missing barriers during materialization are blocker bugs.

---

## 16.5 Read Barriers

If the GC uses read barriers, load-side reconstruction must also comply.

This may affect:

- loading object fields during materialization validation
- reading forwarding pointers
- resolving handles

---

# 17. Effect Correctness

Deoptimization must not alter guest-visible effect order.

---

## 17.1 Effects Before Guard

All effects that occurred before the guard must remain committed.

They must not be undone unless the DVM effect system proves that undoing is
safe.

Examples of committed effects:

- stores to escaping memory
- native calls
- I/O
- guest-visible allocation hooks
- exceptions raised
- tracing events emitted
- finalizer callbacks run
- weakref callbacks run

---

## 17.2 Effects After Guard

Effects after the guard must not have occurred yet if the guard protects them.

If they did occur, deopt cannot simply restart before them.

Therefore:

- guards must precede dependent effects
- speculative effects must be deferred
- irreversible effects require proof

---

## 17.3 Pure Recomputation

It is usually safe to recompute pure expressions after deopt.

Therefore FrameStates may choose to recompute pure values in Tier 0 instead of
preserving them.

However:

- if the guest language can observe evaluation counts, recomputation may be
  illegal
- if debugging/tracing requires expression evaluation events, recomputation may
  need tooling coordination

---

# 18. Runtime Deoptimization Sequence

This is the detailed runtime sequence.

---

## Step 1: Guard Failure

The guard condition fails in optimized code.

Control transfers to a deopt stub.

The transfer must preserve:

- condition flags where needed
- live registers described by FrameState
- stack pointer
- return address or patchpoint identity

---

## Step 2: Deopt Stub Entry

The deopt stub is machine-generated or runtime-provided.

It must:

- save live machine registers
- identify the deopt point
- locate metadata
- avoid using guest language exceptions
- avoid using global locks on the hot failure path unless necessary

---

## Step 3: Metadata Lookup

Using PC offset, patchpoint ID, or guard ID, the runtime finds:

- FrameState ID
- deopt reason
- target tier
- dependency snapshot
- materialization plan
- stack map

---

## Step 4: Machine State Capture

The runtime captures:

- integer registers
- floating-point/vector registers where relevant
- flags
- stack slots
- current native frame information

Only values required by FrameState need be preserved, but the stub must not
clobber required values before capture.

---

## Step 5: FrameState Resolution

The deocoder resolves FrameState entries:

- constants
- machine register values
- stack slot values
- virtual object references
- closure cells
- exception state
- inline frame stack

---

## Step 6: Dependency Validation

The runtime may validate dependency versions.

This is useful for telemetry and debugging.

Examples:

- shape changed
- class changed
- global version changed
- module version changed
- builtin shadowed
- profile decayed

Validation failure is not fatal; it explains why deopt happened.

---

## Step 7: Virtual Object Materialization

If the FrameState contains virtual objects:

1. collect virtual object graph
2. allocate objects
3. register handles
4. initialize fields/elements
5. execute barriers
6. finalize object headers
7. register finalizers/weakrefs if legal
8. replace virtual references with real handles

---

## Step 8: Interpreter Frame Reconstruction

The runtime creates Tier 0 frames.

For each frame:

- allocate interpreter frame structure
- set function/code object
- set bytecode offset
- set source position
- fill interpreter registers
- fill stack slots
- restore closure cells
- restore exception state
- restore tracing/profiling state
- restore suspension state if applicable

For inlined frames, reconstruct the full stack.

---

## Step 9: Stack and GC Registration

Before resuming Tier 0:

- register interpreter frames with stack walker
- publish GC maps
- ensure all references are tracked
- ensure safepoint state is valid

---

## Step 10: Transfer Control to Tier 0

The runtime transfers control to the interpreter at the specified bytecode
offset and reexecution policy.

This transfer must be atomic from the guest program’s perspective.

---

## Step 11: Telemetry Recording

The runtime records:

- deopt reason
- deopt site ID
- function ID
- FrameState ID
- materialization count
- allocation count
- GC interaction flag
- time cost
- current tier
- PGO confidence
- previous deopt history

---

## Step 12: Policy Update

The tiering policy manager may:

- reduce confidence in assumption
- disable specific speculation
- mark function for weaker recompilation
- temporarily blacklist function
- invalidate compiled code
- update inline cache state

---

# 19. Deopt Metadata Layout

DVM should use a structured metadata layout.

---

## 19.1 Function-Level Deopt Header

Each compiled function has:

```cpp
struct DeoptHeader {
    uint32_t function_id;
    uint32_t code_version;
    uint32_t deopt_point_count;
    uint32_t frame_state_count;
    uint32_t virtual_object_count;
    uint32_t inline_frame_count;
    uint32_t gc_map_count;
    uint32_t dependency_count;
    uint32_t constant_pool_count;
    GuestProfileHash guest_profile_hash;
};
```

---

## 19.2 Deopt Point Table

Each deopt point:

```cpp
struct DeoptPointEntry {
    uint32_t pc_offset;
    FrameStateId frame_state;
    DeoptReason reason;
    DeoptTargetKind target;
    DependencySetId dependencies;
    uint32_t stack_map_id;
    uint32_t patchpoint_id;
};
```

---

## 19.3 FrameState Table

Each FrameState:

```cpp
struct FrameStateEntry {
    FrameStateId id;
    FrameStateKind kind;
    GuestFunctionId function_id;
    BytecodeOffset offset;
    SourcePosition source;
    FrameStateId parent_inline_frame;
    RegisterMapId register_map;
    StackMapId stack_map;
    ClosureMapId closure_map;
    ExceptionStateId exception_state;
    ToolingStateId tooling_state;
    SuspensionStateId suspension_state;
    VirtualObjectGraphId virtual_graph;
    DependencySnapshotId dependency_snapshot;
};
```

---

## 19.4 Value Location Table

Value locations should be encoded compactly:

```cpp
struct ValueLocation {
    ValueKind kind;
    TypeId type;
    uint32_t location_index;
};
```

Where `kind` may be:

- `Register`
- `StackSlot`
- `Constant`
- `Immediate`
- `VirtualObject`
- `MaterializedHandle`
- `ClosureCell`
- `GlobalSlot`
- `Undefined`

---

## 19.5 Virtual Object Table

```cpp
struct VirtualObjectEntry {
    VirtualObjectId id;
    AllocSiteId site;
    TypeId type;
    ShapeId shape;
    uint32_t field_count;
    uint32_t element_count;
    ObjectConstraintFlags constraints;
};
```

---

## 19.6 Inline Frame Table

```cpp
struct InlineFrameEntry {
    uint32_t frame_index;
    GuestFunctionId function_id;
    BytecodeOffset offset;
    SourcePosition source;
    uint32_t caller_frame_index;
    BytecodeOffset caller_return_offset;
    RegisterMapId local_map;
    ClosureMapId closure_map;
};
```

---

## 19.7 GC Map Table

```cpp
struct GCMapEntry {
    uint32_t map_id;
    uint32_t register_mask;
    uint32_t stack_ref_count;
    uint32_t spill_ref_count;
    // encoded reference locations follow
};
```

---

# 20. Deopt Reasons

DVM should define a stable deopt reason taxonomy.

## 20.1 Speculation Failure Reasons

- `TypeMismatch`
- `ShapeMismatch`
- `ReceiverTypeMismatch`
- `CallTargetMismatch`
- `LayoutChanged`
- `ClassHierarchyChanged`
- `GlobalVersionChanged`
- `ModuleVersionChanged`
- `BuiltinVersionChanged`
- `ContainerLayoutChanged`
- `SignatureMismatch`
- `AliasAssumptionFailed`
- `RangeCheckFailed`
- `BoundsCheckFailed`
- `NullCheckFailed`
- `OverflowCheckFailed`
- `NumericRepresentationMismatch`

## 20.2 Runtime Transition Reasons

- `AllocationMaterialization`
- `FFITransition`
- `NativeCallback`
- `GCBarrierFallback`
- `SuspensionTransition`
- `ExceptionTransition`

## 20.3 Tooling Reasons

- `DebuggerRequested`
- `TracerRequested`
- `ProfilerRequested`
- `MonitoringRequested`

## 20.4 Policy Reasons

- `OSRExit`
- `DeoptLoopThrottle`
- `CodeInvalidated`
- `DependencyInvalidated`
- `ProfileDecayed`
- `InternalGuardFailure`

---

# 21. Deopt Policies and Throttling

Deopt feedback must influence future compilation.

---

## 21.1 Per-Site Tracking

For each deopt site, track:

- total deopt count
- recent window count
- reason distribution
- average materialization cost
- time since last deopt
- associated guard kind
- associated PGO confidence

---

## 21.2 Per-Function Tracking

For each function, track:

- total deopt count
- deopt rate
- recompilation count
- tier history
- blacklist state
- compile cost
- code cache pressure contribution

---

## 21.3 Throttling Actions

If deopt rate exceeds thresholds:

1. reduce confidence of associated profile data
2. disable specific speculation kind
3. recompile with weaker assumptions
4. downgrade function to lower tier
5. temporarily blacklist function
6. permanently blacklist function in extreme cases
7. emit telemetry

---

## 21.4 No Silent Deopt Loops

If a site deopts repeatedly, DVM must not keep recompiling the same bad
speculation indefinitely.

This is required by Rule 84.

---

# 22. Interaction with Inline Caches

Guard failure often implies inline-cache state is stale.

Examples:

- shape guard failed because object shape changed
- receiver guard failed because type changed
- call target guard failed because method was redefined
- global guard failed because builtin shadowed

The deopt runtime should notify the inline cache system.

Possible actions:

- patch inline cache to polymorphic
- invalidate monomorphic cache
- update type feedback
- mark dependency changed
- trigger dependent code invalidation

---

# 23. Interaction with Code Invalidation

Some deopts indicate that compiled code should be invalidated.

Examples:

- class mutation
- shape transition
- global rebinding
- module mutation
- builtin shadowing
- method redefinition
- code object replacement

In these cases, deopt may be only a local symptom. The dependency system may
need to invalidate all code depending on the changed entity.

The order must be:

1. dependency change observed
2. dependent code marked invalid
3. safe retirement initiated
4. running code allowed to exit or deopt safely
5. old code reclaimed after quiescence

---

# 24. Security and Robustness Requirements

Deopt metadata and runtime paths must be robust.

---

## 24.1 Metadata Validation

Deopt metadata generated by the compiler is trusted only if it passes verifier
checks.

Persisted artifacts containing deopt metadata must be validated before loading.

Malformed metadata must not cause:

- memory corruption
- arbitrary code execution
- invalid frame reconstruction
- GC unsafety
- crashes on valid guest code

---

## 24.2 Deopt Stubs Are Constrained

Deopt stubs must only call approved runtime entrypoints.

They must not:

- execute arbitrary user machine code
- perform arbitrary syscalls
- bypass W^X rules
- write outside approved runtime memory

---

## 24.3 No C++ Exceptions in Deopt Path

The deopt path is a hot runtime path, even if rare.

It must use `Result<T, Error>` style error handling.

Native C++ exceptions are forbidden.

---

# 25. Verification Requirements

The FrameState verifier must check the following.

---

## 25.1 Structural Checks

- every guard has a FrameState
- every deopt point references a valid FrameState
- every FrameState has a valid guest location
- every inline frame has a valid parent except root
- every value mapping references a valid location
- every virtual object has a valid type/shape
- every virtual field has a valid value
- every virtual object graph is resolvable

---

## 25.2 Semantic Checks

- FrameState execution point is compatible with guard location
- FrameState preserves exception state where needed
- FrameState preserves closure cells where needed
- FrameState preserves introspectable locals where needed
- FrameState does not rely on discarded observable values
- FrameState does not require materialization of already-observed objects

---

## 25.3 GC Checks

- every reference in FrameState is GC-tracked
- every stack map covers references across safepoints
- materialization plan respects write barriers
- materialization plan supports moving GC if enabled

---

## 25.4 Tooling Checks

- tracing state preserved if tracing active
- profiling state preserved if profiling active
- monitoring state preserved if monitoring active
- debugger frame reconstructability preserved if debugging supported

---

# 26. Performance Requirements

Deopt must not ruin normal execution.

---

## 26.1 Guard Success Cost

Guard success must be cheap.

Preferred guard forms:

- single compare and branch
- shape/version load and compare
- bounds comparison
- null check folded with existing check
- patchpoint with dependency invalidation

Guard success should not require:

- function calls
- atomic operations
- global locks
- profile updates on every execution
- FrameState decoding

---

## 26.2 FrameState Storage Cost

FrameState metadata should be out-of-line where possible.

Hot code should not embed large FrameState blobs inline.

Use:

- metadata tables
- compressed IDs
- cold sections
- deduplication

---

## 26.3 Deopt Failure Cost

Deopt failure is rare but must still be bounded.

Materialization may be expensive, but it must not be unbounded.

The compiler must estimate materialization cost and avoid speculation with
catastrophic deopt cost unless justified.

---

# 27. Telemetry Requirements

Every deopt event must produce structured telemetry.

## 27.1 Event Fields

- timestamp
- function ID
- code version
- deopt point ID
- guard kind
- deopt reason
- FrameState ID
- target tier
- materialized object count
- allocated object count
- inline frame depth
- GC occurred during deopt
- allocation failure occurred
- execution time in optimized code before deopt
- profile confidence at guard
- dependency versions

## 27.2 Privacy

Telemetry must not include guest source code or user data unless explicitly
opted in.

---

# 28. Example: Type Guard Deopt

Suppose guest code is:

```text
function get_x(obj):
    return obj.x
```

PGO says `obj` is usually shape `Point`.

Tier 2 compiles:

```text
guard obj.shape == Point else deopt
load field offset Point.x from obj
return value
```

If `obj` is not `Point`, guard fails.

The guard has a FrameState at the bytecode offset for `obj.x` load.

FrameState includes:

- function `get_x`
- bytecode offset of member load
- interpreter register containing `obj`
- source position
- exception state empty
- tracing state
- no virtual objects, unless PEA virtualized something

Deopt reconstructs Tier 0 frame:

```text
get_x:
    register obj = actual object
    resume at member load bytecode
```

Interpreter then performs the correct dynamic member access.

---

# 29. Example: PEA Materialization During Deopt

Suppose guest code is:

```text
function distance(ax, ay, bx, by):
    p = Point(ax - bx, ay - by)
    return p.x * p.x + p.y * p.y
```

EA proves `p` does not escape.

Tier 2 scalarizes:

```text
px = ax - bx
py = ay - by
result = px * px + py * py
```

No `Point` allocation occurs.

Now suppose debugging is enabled and the debugger requests frame locals at the
return point, and the guest language requires `p` to be visible.

If `p` must be visible, EA should not have eliminated it, or FrameState must
materialize it.

If materialization is legal:

- deopt FrameState contains virtual object `p`
- `p.x = px`
- `p.y = py`

If deopt occurs:

1. allocate real `Point`
2. initialize `x = px`
3. initialize `y = py`
4. place object into interpreter register/local for `p`
5. resume Tier 0

If object identity or weakref observation made virtualization illegal, EA must
not scalarize in the first place.

---

# 30. Example: OSR Deopt From Hot Loop

Suppose:

```text
sum = 0
i = 0
while i < n:
    sum += a[i]
    i += 1
```

Tier 0 executes the loop and detects heat.

DVM compiles the loop with OSR entry at loop header.

Optimized loop deoptimizes because `a` changed shape/type.

OSR exit FrameState must reconstruct:

- current `i`
- current `sum`
- current `a`
- interpreter loop header state
- exception state
- source position

Interpreter resumes at loop header with exact state.

No iterations may be lost or duplicated.

---

# 31. Testing Requirements

The deopt system must have first-class tests.

---

## 31.1 Forced Guard Failure Tests

Every guard kind must have tests that force failure.

Examples:

- type guard failure
- shape guard failure
- bounds guard failure
- null guard failure
- overflow guard failure
- alias guard failure
- call target guard failure
- global version guard failure

---

## 31.2 Differential Deopt Tests

For each test:

- run Tier 0
- run Tier 2 with forced deopt
- compare observable behavior

The result must be equivalent.

---

## 31.3 Frame Reconstruction Tests

Verify:

- local variables
- closure cells
- exception state
- traceback
- source positions
- inlined frames
- receiver/self values
- tooling state

---

## 31.4 Materialization Tests

Verify:

- simple object materialization
- nested object materialization
- cyclic object materialization
- array materialization
- closure materialization
- exception object materialization
- suspended frame materialization

---

## 31.5 GC Stress Tests

Run deopt under:

- frequent GC
- moving GC
- low memory
- allocation failure
- concurrent GC where supported
- barrier stress

---

## 31.6 OSR Tests

Verify:

- OSR entry
- OSR exit
- deopt at loop header
- deopt at loop backedge
- deopt after many iterations
- deopt with modified induction variables
- deopt with iterator state

---

## 31.7 Tooling Tests

Run deopt with:

- tracing enabled
- profiling enabled
- debugger attached where supported
- monitoring hooks enabled

Verify no missing/duplicate events.

---

## 31.8 Deopt Loop Tests

Verify throttling:

- repeated guard failure
- repeated recompile/deopt cycles
- blacklisting behavior
- fallback to Tier 0
- telemetry output

---

## 31.9 Fuzzing

Fuzz:

- guard placement
- FrameState contents
- virtual object graphs
- OSR transitions
- exception transitions
- suspension transitions
- metadata corruption, for persisted artifacts

Untriaged deopt fuzz failures block release.

---

# 32. Failure Modes and Required Behavior

---

## 32.1 Missing FrameState

If a guard has no FrameState, compilation must fail.

This is a compiler bug.

Execution must fall back safely.

---

## 32.2 Incomplete FrameState

If FrameState cannot reconstruct exact state, the speculation must be rejected
at compile time.

Do not discover this at runtime if avoidable.

---

## 32.3 Materialization Allocation Failure

If allocation fails during materialization:

- do not crash
- preserve as much state as safely possible
- produce guest-appropriate memory error if semantically correct
- record telemetry
- treat unrecoverable VM state as P0 bug

---

## 32.4 GC Failure During Materialization

If GC cannot safely handle materialization, this is a blocker bug.

The compiler should have rejected the speculation or the runtime must support
the required handles/pinning.

---

## 32.5 Dependency Race

If dependency invalidation races with deopt:

- deopt must still reconstruct correct lower-tier state
- invalidation must not corrupt running frames
- old code must remain safe until quiescence

---

# 33. Implementation Checklist

A DVM deopt implementation is complete only if all of these are true.

## Compiler Side

- [ ] Every guard has FrameState.
- [ ] Every FrameState is verifier-checked.
- [ ] Every inlined guard has inline frame stack.
- [ ] Every virtual object has materialization plan.
- [ ] Every exception-capable operation has correct exception FrameState.
- [ ] Every suspension point has suspension FrameState where supported.
- [ ] Every OSR point has OSR FrameState.
- [ ] Every dependency is recorded.
- [ ] Every deopt point has a reason category.
- [ ] FrameStates are deduplicated and compressed safely.
- [ ] Deopt metadata is emitted for every compiled function.

## Runtime Side

- [ ] Deopt stub captures machine state correctly.
- [ ] Metadata lookup is robust.
- [ ] Materializer handles cycles.
- [ ] Materializer is GC-safe.
- [ ] Materializer respects barriers.
- [ ] Interpreter frame reconstruction is exact.
- [ ] Exception state reconstruction is exact.
- [ ] Suspension state reconstruction is exact.
- [ ] Tooling state reconstruction is exact.
- [ ] Deopt telemetry is emitted.
- [ ] Deopt loop throttling works.
- [ ] Code invalidation works with deopt.
- [ ] Old code retirement is quiescent.

## Testing Side

- [ ] Forced guard failure tests exist.
- [ ] Differential deopt tests exist.
- [ ] Materialization tests exist.
- [ ] GC stress tests exist.
- [ ] OSR tests exist.
- [ ] Exception tests exist.
- [ ] Suspension tests exist.
- [ ] Tooling tests exist.
- [ ] Fuzzing is scheduled.
- [ ] Replay artifacts are generated.

---

# 34. Final Design Statement

The DVM deoptimization and FrameState machinery must be treated as a core
semantic subsystem, not a backend detail.

Its job is to make aggressive optimization safe.

Every speculative optimization in DVM implicitly says:

> “If I am wrong, the VM can return to a correct lower-tier state.”

The FrameState system is the proof of that promise.

If the FrameState system is incomplete, DVM cannot legally speculate.

Therefore:

- no guard without FrameState
- no virtual object without materialization plan
- no deopt point without metadata
- no materialization without GC safety
- no frame reconstruction without semantic exactness
- no deopt loop without throttling
- no silent fallback without telemetry

This is the foundation that allows DVM to be both dynamic and fast.
