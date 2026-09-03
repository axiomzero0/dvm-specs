# DVM Subsystem Specification: DGW-Core IR

**Status:** Stable
**Subsystem:** Tier 2 / Tier 3 Optimizing Compiler
**Depends On:** `DVM Bytecode`, `Guest Language Profiles`, `Deopt Machinery`

> This is the exhaustive, uncompressed, deeply technical specification for
> **DGW-Core (Dynamic Graph Web)**, the mid-level optimizing IR for the DVM
> (Dynamic Virtual Machine).
>
> This document leaves no implementation detail to chance. It defines the exact
> memory layout, the mutation algorithms, the semantic invariants, and the
> lowering mechanics required to build a world-class, aggressively speculating
> JIT compiler in C++26.

---

## Part 1: The Memory Architecture (The "Loom")

A JIT compiler lives and dies by cache locality and allocation speed. DGW-Core
abandons traditional object-oriented node graphs (which scatter nodes across
the heap, causing TLB misses and pointer-chasing) in favor of a **Woven Arena**
using **Structure of Arrays (SoA)** and **Intrusive Index-Based Linked Lists**.

### 1.1 The Core Identifiers

Everything in DGW-Core is a 32-bit integer index. This guarantees a 4-byte
memory footprint per reference, allows trivial serialization, and prevents
pointer invalidation during arena reallocation (DVM Rule 15).

```cpp
using NodeId   = uint32_t; // Index into NodeTable
using EdgeId   = uint32_t; // Index into EdgeTable
using PortId   = uint16_t; // Local index within a node's port array
using RegionId = uint32_t; // Index into RegionTable (Memory identity)
using SymbolId = uint32_t; // Interned string/identifier (DVM Rule 16)
using TypeId   = uint32_t; // Index into TypeTable
```

### 1.2 The Graph Arena (Structure of Arrays)

The graph state is held in a single `GraphArena`. Passes iterate over these
dense arrays, allowing the CPU's hardware prefetcher to pull data into L1
cache seamlessly (DVM Rule 20).

```cpp
struct GraphArena {
    // --- NODE TABLE ---
    std::pmr::vector<NodeKind>    node_kinds;
    std::pmr::vector<TypeId>      node_types;      // The output type of the node
    std::pmr::vector<uint32_t>    node_payload_idx;// Index into specific payload SoA
    std::pmr::vector<NodeFlags>   node_flags;      // Bitmask (Pure, Volatile, Guarded, etc.)
    std::pmr::vector<uint32_t>    node_first_use;  // Head of the intrusive use-def chain

    // --- EDGE TABLE (Intrusive Doubly Linked List for O(1) mutation) ---
    std::pmr::vector<NodeId>      edge_source_node;
    std::pmr::vector<PortId>      edge_source_port;
    std::pmr::vector<EdgeKind>    edge_kinds;      // VALUE, CONTROL, MEMORY, EFFECT, GUARD
    std::pmr::vector<uint32_t>    edge_next_use;   // Next edge in the use-def chain
    std::pmr::vector<uint32_t>    edge_prev_use;   // Prev edge in the use-def chain (for O(1) detach)

    // --- PORT TABLE (Inputs are fixed per node instance) ---
    std::pmr::vector<EdgeId>      port_connected_edge; // The edge currently plugged into this port

    // --- REGION TABLE (First-class memory identities) ---
    std::pmr::vector<RegionKind>  region_kinds;    // HEAP, STACK, GLOBAL, VIRTUAL
    std::pmr::vector<uint32_t>    region_sizes;
    std::pmr::vector<uint8_t>     region_aligns;
    std::pmr::vector<EscapeState> region_escapes;  // NO_ESCAPE, ARG_ESCAPE, GLOBAL_ESCAPE
};
```

### 1.3 Node Payloads (Union of Specific Data)

Nodes that require extra data (like constants, field offsets, or call
signatures) do not bloat the base `NodeTable`. They reference an index into
specialized payload arrays.

```cpp
struct ConstPayload { std::variant<int64_t, double, SymbolId> value; };
struct RefPayload   { RegionId region; int64_t offset; AccessPerm perms; };
struct CallPayload  { SymbolId target; CallConv conv; uint16_t arg_count; };
struct GuardPayload { FrameStateId frame_state; DeoptReason reason; };
```

---

## Part 2: The Port and Edge Typing System

In DGW-Core, edges are not just "lines between boxes." They are strictly typed
conduits. A node exposes **Ports**, and an Edge connects an **Output Port** to
an **Input Port**.

### 2.1 Edge Kinds (The Threads of the Web)

```cpp
enum class EdgeKind : uint8_t {
    VALUE,    // Pure data dependency (SSA). Carries TypeId.
    CONTROL,  // Execution flow (Success path). Carries no data, just ordering.
    EXCEPT,   // Execution flow (Exception/Failure path).
    MEMORY,   // Memory state token (Memory SSA). Ensures Load/Store ordering.
    EFFECT,   // Side-effect ordering (I/O, GC barriers, Tracing).
    GUARD     // Speculative assumption dependency.
};
```

### 2.2 Port Signatures

Every `NodeKind` has a strict, compile-time verified port signature. The
verifier (Part 9) ensures edges only connect to compatible ports.

**Example: `LOAD` Node Signature**

*   `Input[0]`: `CONTROL` (Execution must reach here)
*   `Input[1]`: `MEMORY` (Must happen after previous memory state)
*   `Input[2]`: `VALUE` (Must be a `REF` type)
*   `Output[0]`: `VALUE` (The loaded data)
*   `Output[1]`: `MEMORY` (The new memory state)

**Example: `GUARD` Node Signature**

*   `Input[0]`: `CONTROL` (Incoming execution)
*   `Input[1]`: `VALUE` (The boolean condition to check)
*   `Output[0]`: `CONTROL` (Success path, assumption holds)
*   `Output[1]`: `CONTROL` (Failure path, routes to `DEOPT_TRAP`)

---

## Part 3: The Memory and Region Model (Structural Aliasing)

This is the core innovation of DGW-Core. We do not use raw integer pointers for
memory operations. We use **First-Class Regions** and **References**. This makes
Escape Analysis (EA) and Partial Escape Analysis (PEA) trivial graph-reachability
problems.

### 3.1 Regions (Allocations)

An `ALLOC` node does not produce a pointer; it produces a `RegionId`.

```cpp
enum class RegionKind : uint8_t {
    STACK, HEAP, GLOBAL, TLS, VIRTUAL, ELIMINATED
};
```

### 3.2 References (Projections)

A `REF` node projects a specific view into a Region.

```text
[ALLOC] (Produces Region R1)
   |
   +--> [REF] (region=R1, offset=0, type=i32)
   |
   +--> [REF] (region=R1, offset=4, type=f32)
```

Because `REF`s carry their `RegionId`, **Alias Analysis is O(1)**. If
`RefA.region != RefB.region`, they strictly cannot alias (unless one is `GLOBAL`
or `UNKNOWN`).

### 3.3 Memory SSA (The Memory Token)

Memory operations (`LOAD`, `STORE`, `CALL`) consume and produce a `MEMORY`
edge. This forms a strict, single-linked chain of memory states, preventing the
optimizer from illegally reordering stores and loads.

```text
[STORE] (RefA, Val1) --> MEMORY_OUT --> [LOAD] (RefB) --> MEMORY_OUT --> [STORE] (RefC, Val2)
```

### 3.4 Partial Escape Analysis (PEA) via `MATERIALIZE`

If EA proves `Region R1` has `EscapeState::NO_ESCAPE`, the optimizer deletes the
`ALLOC` node and marks the region as `VIRTUAL`. Stores to `VIRTUAL` regions are
intercepted and stored in a side-table.

If `R1` reaches a branch where it *might* escape (e.g., passed to an opaque FFI
call), the optimizer splices in a `MATERIALIZE` node on *only that control path*.

```text
[BRANCH]
  ├── TRUE_CONTROL --> [FFI_CALL] (Requires real pointer)
  |                      ^
  |                      |
  |                  [MATERIALIZE] (Consumes Virtual R1, outputs real Heap Pointer)
  |
  └── FALSE_CONTROL --> [PURE_MATH] (Continues using Virtual R1)
```

---

## Part 4: Blockless Control Flow and Exceptions

DGW-Core eliminates basic blocks during mid-level optimization. Control flow is
modeled as tokens flowing through the web.

### 4.1 Branches and Joins

A `BRANCH` node consumes a `CONTROL` token and a `VALUE` (condition) token, and
emits two `CONTROL` tokens: `TRUE` and `FALSE`.

A `JOIN` node (the blockless equivalent of a Phi node) merges multiple `CONTROL`
tokens and multiple `VALUE` tokens.

```text
[BRANCH]
  ├── TRUE_CONTROL --> [ADD] ---+
  |                             |
  └── FALSE_CONTROL -> [SUB] ---+--> [JOIN] (Merges ADD and SUB results)
```

### 4.2 Loops (The `STATE` Node)

Loops are modeled using `STATE` nodes, which act as the loop header. A `STATE`
node has two inputs: the initial value (from outside the loop) and the backedge
value (from inside the loop).

```text
[INIT] (i=0)
   |
   v
[STATE] <-----------------------+
   |                            |
[BODY] (i = i + 1)              |
   |                            |
[BACKEDGE] ---------------------+
   |
[EXIT]
```

### 4.3 Exceptions (The `EXCEPT` Edge)

Any node that can throw (e.g., `CALL`, `DIV`, `LOAD`) has an optional `EXCEPT`
output port. If an exception occurs, the `EXCEPT` token bypasses normal `CONTROL`
flow and routes directly to a `HANDLER` node.

```text
[CALL]
  ├── CONTROL --> [NEXT_INSTRUCTION]
  └── EXCEPT  --> [HANDLER] (Catches the exception token)
```

---

## Part 5: The Weaver (Graph Mutation API)

**DVM Rule 10** demands idempotent, fast passes. We do not use "rollback"
mechanisms. The `Weaver` class provides O(1) or O(U) (where U is use count)
mutation primitives.

### 5.1 Rewiring Uses (`rewire_uses`)

Replaces all uses of `OldNode` with `NewNode`.

*Algorithm:* Walk the intrusive linked list starting at `node_first_use[OldNode]`.
For each edge, update `edge_source_node` to `NewNode`. Move the list head to
`node_first_use[NewNode]`.

*Complexity:* O(U).

### 5.2 The Forwarding Node Trick (O(1) Rewiring)

If a node has 50,000 uses, O(U) is too slow for a JIT. Instead, the Weaver
creates a `FWD` (Forwarding) node.

1. `FWD` takes `NewNode` as input.
2. The Weaver rewires the *single* use of `OldNode`'s output port to point to
   `FWD`. (Wait, no, `OldNode` is the source. We change the *users* to point to
   `FWD`? No, the trick is: we leave the users alone. We change `OldNode`'s
   payload to become a `FWD` node that just points to `NewNode`.)

*Correction:* In DGW-Core, a `FWD` node is a special `NodeKind`. The Weaver
changes `node_kinds[OldNode] = FWD` and sets its single input to `NewNode`. The
users don't change. A later fast `CleanupPass` collapses `FWD` chains in a
single linear sweep. **Complexity: O(1).**

### 5.3 Splicing (`splice_into_edge`)

Inserts a new node into the middle of an existing edge (e.g., inserting a
`GUARD` before a `LOAD`).

1. Create `NewNode`.
2. Connect `NewNode`'s input to the original source.
3. Update the target's input port to point to `NewNode`.
4. Update the intrusive linked list pointers (`prev_use`, `next_use`).

*Complexity:* O(1).

### 5.4 Killing (`kill_node`)

Marks a node as dead. It is disconnected from the graph. The actual memory is
not freed immediately; it is reclaimed in bulk when the compilation epoch ends
(DVM Rule 7, 14).

---

## Part 6: Optimization Passes on the Web

Because the IR is a mutable web, traditional passes become **Graph
Transformers** operating via the `Weaver`.

### 6.1 Global Value Numbering (GVN)

1. Iterate over all pure nodes.
2. Hash `(NodeKind, InputEdges)`.
3. If hash exists in GVN map, call `weaver.rewire_uses(CurrentNode, ExistingNode)`.
4. Call `weaver.kill_node(CurrentNode)`.

### 6.2 Dead Code Elimination (DCE)

1. Seed a worklist with all `Observable` nodes (`RETURN`, `STORE` to global,
   `I/O`, `DEOPT_TRAP`).
2. Walk backwards along `VALUE`, `CONTROL`, `MEMORY`, and `EFFECT` edges,
   marking nodes as `Live`.
3. Any node not marked `Live` is passed to `weaver.kill_node()`.

### 6.3 Loop Invariant Code Motion (LICM)

1. Identify `STATE` nodes (loop headers).
2. For each node inside the loop, check if its `VALUE` inputs originate from
   outside the loop (do not depend on the `STATE` backedge).
3. If invariant, and its `EFFECT`/`MEMORY` dependencies allow it, detach it
   from the loop's `CONTROL` token and reattach it to the `CONTROL` token
   *preceding* the loop.

---

## Part 7: Scheduling and Block Formation (Lowering)

Hardware CPUs require linear instruction streams. At the end of the mid-level
pipeline (Phase K), DGW-Core undergoes **Trace Scheduling and Block Formation**.

### 7.1 Trace Formation

1. Start at the `START` node.
2. Follow the `CONTROL` edges. At a `BRANCH`, use PGO (Profile-Guided
   Optimization) probabilities to pick the "hottest" path.
3. This forms a "Trace" (a straight-line sequence of nodes).

### 7.2 Block Extraction

1. Walk the Trace. Group nodes into `MachineBasicBlock`s.
2. A new block is started whenever:
   * A node is the target of a `BRANCH` or `JOIN` from outside the current trace.
   * A `HANDLER` (exception landing pad) is encountered.
3. Convert `JOIN` nodes back into standard SSA `PHI` nodes at the top of the
   newly formed blocks.
4. Convert `STATE` nodes back into standard SSA `PHI` nodes at the loop headers.

### 7.3 Instruction Selection

The pure DGW nodes are pattern-matched into `MachineInstr`s using a BURS
(Bottom-Up Rewrite System) or DAG-covering algorithm. The blockless web is now a
standard Control Flow Graph (CFG) of machine instructions.

---

## Part 8: Verification Invariants (The "Loom Inspector")

To satisfy **DVM Rule 40**, the `WebVerifier` runs after every pass in debug
builds. It checks four layers of validity.

### 8.1 Structural Validity

*   Every `EdgeId` in `port_connected_edge` must be a valid index in
    `EdgeTable`.
*   Every `node_first_use` must point to a valid `EdgeId` or be `NULL_EDGE`.
*   The intrusive linked lists (`next_use`, `prev_use`) must be perfectly
    symmetrical and terminate correctly.
*   No node can be its own ancestor (except via explicit `STATE` backedges).

### 8.2 Semantic Validity (Typing)

*   A `VALUE` edge must connect an output port to an input port that accepts
    the exact `TypeId` (or a valid implicit cast node must exist).
*   A `CONTROL` edge can only connect to a `CONTROL` port.
*   A `MEMORY` edge can only connect to a `MEMORY` port.

### 8.3 Memory & Effect Validity

*   **Single Memory Chain:** Following `MEMORY` edges from any `STORE`/`LOAD`
    must eventually lead back to the `START` node without splitting (unless
    explicitly merged by a `MEMORY_JOIN` node).
*   **No Reordering:** A `STORE` to `Region A` cannot be moved past a `LOAD`
    from `Region A` unless alias analysis proves they are disjoint offsets.
*   **Effect Anchors:** `EFFECT` edges must form a strict partial order. I/O
    and GC barriers cannot be bypassed.

### 8.4 Speculative Validity (Deopt Safety)

*   Every `GUARD` node must have a valid `FrameStateId` in its payload.
*   The failure path of a `GUARD` (the `FALSE_CONTROL` output) must route
    exclusively to a `DEOPT_TRAP` or an `UNCOMMON_TRAP` node.
*   No `Observable` side effects (Stores, I/O) can exist on a control path
    *after* a `GUARD` but *before* the `GUARD`'s success path is committed,
    unless protected by a subsequent guard.

---

## Part 9: Summary of the DGW-Core Advantage

By implementing DGW-Core with this exact specification, the DVM achieves:

1.  **Unprecedented Mutation Speed:** The SoA layout and O(1) Forwarding Node
    trick mean graph rewrites take nanoseconds, keeping JIT compile times
    strictly bounded (DVM Rule 112).

2.  **Flawless Speculation:** First-class `CONTROL` and `GUARD` edges allow the
    optimizer to hoist checks and sink effects globally, unbounded by basic
    block walls.

3.  **Trivial PEA:** First-class `Region` and `Ref` nodes make Escape Analysis
    a simple graph-reachability problem, enabling massive allocation elimination
    for dynamic languages.

4.  **Absolute Semantic Safety:** Explicit `MEMORY` and `EFFECT` tokens ensure
    that guest-language semantics (exceptions, GC barriers, tracing) are never
    illegally reordered, satisfying DVM Rule 68.

DGW-Core is not just an IR. It is a mutable, verifiable, semantic fabric that
allows the DVM to aggressively rewrite reality, while guaranteeing that if a
speculative thread snaps, the Deopt machinery can perfectly reconstruct the
original tapestry.
