// src/control.cpp — Blockless control-flow helpers (Spec Part 4).
//
#include "dgw/control.hpp"

namespace dgw {

EdgeId route_except_to(Weaver& w, NodeId may_throw, NodeId handler) {
  // Find the EXCEPT output port on may_throw. For CALL nodes, it's the
  // optional 3rd output port (port in_count + 2). For other potential
  // throwers, we don't currently expose EXCEPT ports.
  const NodeSignature sig = signature_of(w.kind_of(may_throw));
  const std::uint16_t in_count = static_cast<std::uint16_t>(sig.inputs.size());
  for (std::uint16_t i = 0; i < sig.outputs.size(); ++i) {
    if (sig.outputs[i].kind == EdgeKind::EXCEPT) {
      PortId except_port{static_cast<std::uint16_t>(in_count + i)};
      // HANDLER's input port 0 is the EXCEPT input per sig_detail::handler_in.
      return w.connect(may_throw, except_port, handler, PortId{0}, EdgeKind::EXCEPT);
    }
  }
  return EdgeId{};  // No EXCEPT port on this node.
}

EdgeId connect_backedge(Weaver& w, NodeId body_tail, NodeId state) {
  // STATE input port 1 is the backedge per sig_detail::state_in.
  return w.connect_value(body_tail, state, PortId{1});
}

}  // namespace dgw
