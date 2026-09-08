// src/graph.cpp — Graph facade implementation.
//
#include "dgw/graph.hpp"

namespace dgw {

Graph::OptStats Graph::optimize_default() {
  OptStats s;
  // Pipeline per Performance Priorities §10:
  // const-fold → GVN → DCE → cleanup → verify
  // Constant folding runs first so GVN sees folded constants and can
  // merge them. DCE then removes the dead arithmetic nodes that were
  // replaced by FWD to CONST.
  s.constfold = pass_constfold(weaver_);
  s.gvn       = pass_gvn(weaver_);
  s.dce       = pass_dce(weaver_);
  s.cleanup   = pass_cleanup(weaver_);
  s.post_verify = verifier_.verify_all();
  return s;
}

}  // namespace dgw
