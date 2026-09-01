# Mandatory Agent Review for Rule Compliance

**Status:** Stable
**Subsystem:** Cross-cutting / Process
**Depends On:** All other DVM specification documents
**Applies To:** Every artifact produced by any AI agent, contributor, or automation
that touches the DVM `compiler/`, `runtime/`, `tools/`, `tests/`, or `docs/` trees.

---

## Rule Statement

> **Every output produced by any agent — code, prose, spec, diagram, build script,
> test, or configuration — must be reviewed by a second, independent agent for
> compliance with the DVM specification corpus before that output is considered
> deliverable. No exceptions.**

This rule is normative. It is enforced by CI at merge time and by convention at
authoring time. An agent that ships an unreviewed output is in violation of the
DVM specification, and the output is treated as if it does not exist.

---

## 1. Why This Rule Exists

The DVM is a speculating JIT runtime. A single spec violation in the compiler
can silently corrupt guest-language semantics, deopt safety, or memory
ordering. Reviewers cannot catch these violations by reading diffs casually.
The review must be **structured**, **spec-indexed**, and **performed by an
agent that did not write the output**, because the producing agent is
structurally blind to its own assumptions.

The four classes of failure this rule exists to prevent:

1.  **Memory-layout drift.** A "small" change to `GraphArena` that violates
    Part 1 of DGW-Core-IR.md (e.g. introducing a pointer field, breaking SoA,
    or storing a `RegionId` as a `uint64_t`) silently destroys cache locality
    and breaks serialization.
2.  **Weaver invariant violations.** A new optimization pass that rewires
    edges without updating `edge_next_use` / `edge_prev_use` corrupts the
    use-def chains and crashes the verifier on the next pass.
3.  **Speculation-safety holes.** A `GUARD` node whose failure path does not
    route exclusively to `DEOPT_TRAP` / `UNCOMMON_TRAP` is a silent deopt
    unsoundness that only manifests under a real speculative miss in
    production.
4.  **Memory-chain breaks.** Reordering a `STORE` past a `LOAD` from the
    same `Region` without alias-analysis justification violates Part 8.3 of
    DGW-Core-IR.md and can change observable program behavior.

---

## 2. Scope of "Output"

An "output" is **any artifact an agent produces or modifies** that will land
in the repository or be presented to a human as deliverable work. This
includes, but is not limited to:

| Artifact | Review Required? | Reviewer Scope |
|----------|------------------|----------------|
| C++ source under `compiler/` | **Yes** | Spec parts covering the affected subsystem |
| C++ source under `runtime/` | **Yes** | Spec parts covering the affected subsystem |
| Test files under `tests/` | **Yes** | Whether the test actually exercises the spec rule it claims to |
| Build files (`CMakeLists.txt`, scripts) | **Yes** | Whether they introduce dependencies or flags forbidden by the spec |
| New spec documents under `docs/` | **Yes** | Whether they contradict existing specs |
| Edits to existing specs under `docs/` | **Yes** | Whether the edit preserves or breaks existing normative rules |
| Diagrams, READMEs, comments | **Yes, lightweight** | Whether claims in prose match the code |

A "lightweight" review is still mandatory; it is simply scoped to the prose
the artifact introduces, not the entire subsystem.

---

## 3. The Review Protocol

### 3.1 Independence Requirement

The reviewing agent **must not** be the same agent that produced the output.
The producing agent may not self-review. The producing agent may, however,
prepare a *self-audit checklist* that the reviewer uses as a starting point,
provided the reviewer independently verifies each item.

### 3.2 Spec-Indexed Review

The reviewer must:

1.  **Identify the spec parts that govern the output.** For a change to the
    Weaver, that is Part 5 of `DGW-Core-IR.md`. For a change to the verifier,
    that is Part 8. For a change to the deopt machinery, that is
    `DVM-Deopt-FrameState.md` in its entirety.
2.  **Quote the exact rule text** under review, by section number.
3.  **Point at the exact line(s)** in the output that claim to satisfy the
    rule.
4.  **State, for each rule, one of three verdicts:**
    *   `PASS` — the output satisfies the rule.
    *   `FAIL` — the output violates the rule. The reviewer must include the
      minimal reproducer or the exact code path that violates.
    *   `N/A` — the rule does not apply to this output.
5.  **Sign the review** with the reviewer's agent identifier and a
    UTC timestamp.

### 3.3 Mandatory Verifier Run

If the output is code that the `WebVerifier` (Part 8 of `DGW-Core-IR.md`) can
exercise, the reviewer must run the verifier on the output's test fixtures
and report the result. A review that does not include a verifier run is
considered incomplete.

### 3.4 Blocking Status

A review is one of:

*   **APPROVED** — all rules `PASS` or `N/A`, verifier green.
*   **CHANGES_REQUESTED** — one or more rules `FAIL` or the verifier is red.
  The producing agent must address every `FAIL` and re-request review. A
  review that returns `CHANGES_REQUESTED` cannot be re-issued by the same
  reviewer until the producing agent has pushed a new commit; ping-pong
  without new commits is forbidden.
*   **REJECTED** — the output is fundamentally non-compliant and cannot be
  salvaged by edits. The producing agent must restart.

A merge requires `APPROVED`. No exceptions. The CI gate enforces this.

---

## 4. Reviewer Qualifications

The reviewer must:

*  Have read, in full, every spec document listed in the repository
   `README.md` at the time of review.
*  Be able to cite spec rules by section number without looking them up.
*  Have no authorship on the lines under review.
*  Be willing to write `FAIL` verdicts. A reviewer that issues only `PASS`
   verdicts across many reviews is statistically suspect and may be
   spot-audited by a third agent.

---

## 5. Producer Responsibilities

The producing agent must:

1.  **Self-audit before requesting review.** Produce a checklist of the spec
   rules the output touches, with the producing agent's own `PASS`/`FAIL`
   self-assessment. The reviewer uses this as input, not ground truth.
2.  **Include spec citations in the commit message.** A commit that touches
   the Weaver must cite Part 5 of `DGW-Core-IR.md`. A commit that touches the
   verifier must cite Part 8. The CI gate parses these citations.
3.  **Run the verifier locally before requesting review.** A review request
   on code that fails the verifier is treated as a procedural violation and
   counts against the producing agent's review-standing.
4.  **Address every `FAIL` with a new commit.** Edits-in-place without a
   new commit invalidate the review.

---

## 6. CI Enforcement

The CI gate must:

1.  **Block merge** if the pull request has no `APPROVED` review record
   satisfying Section 3.
2.  **Block merge** if the verifier is red on any test fixture touched by
   the pull request.
3.  **Block merge** if the commit message lacks spec citations for the
   files it modifies.
4.  **Block merge** if the reviewer is the same agent as the producer.

CI violations are not warnings. They are blocking.

---

## 7. Records

Every review must be recorded in `docs/reviews/` as a Markdown file named:

```
reviews/<YYYY-MM-DD>-<producer-agent-id>-<short-output-slug>.md
```

The file must contain:

*  The producer's self-audit checklist (Section 5.1).
*  The reviewer's spec-indexed verdicts (Section 3.2).
*  The verifier run log (Section 3.3).
*  The final review status (Section 3.4).
*  Reviewer agent ID and UTC timestamp.

Reviews are append-only. A review may not be edited after submission; a
new review file is produced for each review cycle.

---

## 8. Summary

> An agent that ships without review has shipped nothing. An agent that
> reviews without spec citations has reviewed nothing. An agent that
> approves a `FAIL` has committed a procedural violation. There are no
> exceptions, and CI enforces this rule with the same hardness as any
> other rule in this corpus.
