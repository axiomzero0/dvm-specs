// dgw/control.hpp — Blockless Control Flow helpers (Spec Part 4).
//
// Spec citation: DGW-Core-IR.md Part 4 "Blockless Control Flow and Exceptions".
//
//  4.1 Branches and Joins
//  4.2 Loops (The STATE Node)
//  4.3 Exceptions (The EXCEPT Edge)
//
// This header provides small helpers that make the spec's diagrams easy to
// express in C++. The Weaver's create_branch/create_join/create_state/
// create_handler are the actual constructors; the helpers here wire up
// edges according to the spec's diagrams.
//
#pragma once

#include <cstdint>

#include "dgw/ids.hpp"
#include "dgw/weaver.hpp"

namespace dgw {

// Connect a CONTROL output (port 0 of `src`) to the CONTROL input
// (port 0) of `dst`. Used pervasively by Part 4's diagrams.
inline EdgeId chain_control(Weaver& w, NodeId src, NodeId dst) {
  return w.connect_control(src, dst);
}

// Connect the TRUE_CONTROL output of a BRANCH to `dst`'s CONTROL input.
inline EdgeId branch_true_to(Weaver& w, NodeId br, NodeId dst) {
  // BRANCH output port 0 is TRUE_CONTROL per sig_detail::branch_out.
  return w.connect(br, PortId{0}, dst, PortId{0}, EdgeKind::CONTROL);
}

// Connect the FALSE_CONTROL output of a BRANCH to `dst`'s CONTROL input.
inline EdgeId branch_false_to(Weaver& w, NodeId br, NodeId dst) {
  return w.connect(br, PortId{1}, dst, PortId{0}, EdgeKind::CONTROL);
}

// Splice a HANDLER in front of a node's EXCEPT output. The HANDLER's
// CONTROL output then acts as the recovery path for the exception.
// (Spec 4.3: "EXCEPT token bypasses normal CONTROL flow and routes
//  directly to a HANDLER node.")
//
// For CALL nodes, EXCEPT lives at output port 2 (per sig_detail::call_out).
// For other potential throwers, the Weaver queries the actual port_count
// and uses the last output port.
EdgeId route_except_to(Weaver& w, NodeId may_throw, NodeId handler);

// Connect a STATE node's backedge: the body's terminal VALUE output
// feeds back into the STATE's input port 1 (the backedge slot per
// sig_detail::state_in).
EdgeId connect_backedge(Weaver& w, NodeId body_tail, NodeId state);

}  // namespace dgw
