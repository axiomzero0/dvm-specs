// src/graph.cpp — Graph facade implementation.
//
#include "dgw/graph.hpp"

namespace dgw {

Graph::OptStats Graph::optimize_default() {
  OptStats s;
  s.gvn     = pass_gvn(weaver_);
  s.dce     = pass_dce(weaver_);
  s.cleanup = pass_cleanup(weaver_);
  s.post_verify = verifier_.verify_all();
  return s;
}

}  // namespace dgw
