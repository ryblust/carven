# Semantic execution extensions

- **Status:** Exploration
- **Implementation:** Not started for the extensions described here
- **Scope:** Structured static-root expressions, address observation, C-string literal identity, and execution-mode policies

## Summary

This proposal explores inline structured expressions in static initializers and
array extents, and source-visible observations of execution addresses. These are
independent design slices. Structured roots can reduce one-use `const fn` helpers;
address observations need explicit identity and stability guarantees before they
can produce ordinary values or affect retained results.

## Current foundation

The semantic executor consumes typed
structured operations for required and interpreted execution. Execution places
contain a domain identity, object number, and projection path rather than a host
address. Assignment to a live owner preserves its identity; Take and scope exit
end it, and reinitialization does not revive old pointers. Read observation and
Write aliasing retain their ordinary type and storage contracts.

Ordinary `const fn` bodies support structured controls, but the initializer and
extent entry in `StaticRootSite` rejects direct `if`, `match`, and `try` forms.
`CStringConstant` retains spelling for content display; generated C++ supplies
static-lifetime string literals. Neither constant interning nor native literal
pooling establishes a source literal-identity policy. The full execution path
from a C-string pointer through `ptr<void>` conversion to address formatting is
not implemented.

Source admission, executed-path support, and result publication have separate
checks. This proposal covers observations during execution; constant storage
covers retained values and reference graphs.

## Structured expressions in static roots

An initializer that chooses between values could express its selection directly
instead of declaring a one-use `const fn`. Direct `if`, `match`, and `try` are
separate candidates.

Reuse ordinary expression construction for typing, branch results, scopes, and
lifetimes. The root entry continues to own constant-name visibility, operation
admission, dependency requests, execution budgets, and completion. Declaration
types and array extents may require values before global body construction and
solving are complete. Forward requests, dependency cycles, and root-local bindings
therefore need a construction and execution protocol available at declaration time.

Reuse branch analysis and execution. Initializer freezing and the nonnegative-integer
check for an extent remain distinct completion paths. Define legality of both
branches and the unselected path explicitly, including names, failures, and
unsupported operations; executing one branch does not validate the other.

## Address observation

Choose the relationship being observed before choosing its representation:

| Candidate | Contract to define |
| --- | --- |
| Debug identity | Execution domain, object, projection, and stability interval |
| Execution-environment virtual address | Null, copying, conversion, subobject relations, reuse, and observations after invalidation |
| Target-layout virtual memory | Alignment, address width, field offsets, byte access, and provenance |
| Native bridge | ABI, synchronization, reentry, permissions, lifetime, and failure |

An internal `ExecutionValue*` cannot stand for a source address. Distinct object
identities do not guarantee different address text, and the same address does not
grant the same access permissions. Assigning different numbers to every
projection does not reproduce native subobject address relations. Invalid-pointer
comparison and display need their own policy; rejection of dereference does not
settle those observations.

Address text can be compared or used in control flow, so formatting still has an
observable contract. A C-string converted to `ptr<void>` and then formatted is a
candidate probe. Conversion must preserve its target and permissions without
creating an object or granting Write access.

## C-string literal identity

Constant content and source object identity require separate representations.
The alternatives are:

| Candidate policy | Required execution and backend guarantees |
| --- | --- |
| Every source literal has independent identity | Retain occurrence identity and emit backing that guarantees separation, including across artifacts |
| Equal contents always share identity | Define the merging domain and guarantee shared backing across the applicable modules |
| Sharing or separation is implementation-selected | Do not turn an implementation-dependent identity observation into an environment-independent constant fact |

The first two choices need explicit backing and a defined cross-module strategy;
optional C++ literal pooling cannot guarantee them. Introduce occurrence identity
when the selected policy requires it. A merged `ConstantID` cannot reconstruct
the original literal occurrences.

## Required and interpreted observation policies

Required execution has three candidate boundaries:

- Keep address observation unsupported.
- Permit diagnostic or debug output through an output boundary that does not
  return an ordinary value.
- Permit address text as an ordinary constant, defining whether it describes the
  compile-time environment and its stability within one execution, across
  repeated compilations, and across targets.

Interpreted display can be decided independently. Literal-identity and address
policies must agree wherever an observation includes a literal.

Rejecting pointer values only at freezing is insufficient to prevent address
observations from influencing retained results: an observation can first become
text or an integer, choose a branch, or determine an array extent. A policy that
prohibits that influence must restrict the observation itself or define complete
dependency rules.

## Open decisions

### OPEN-01 — Define one structured static-root form

Choose `if`, `match`, or `try` and define its source legality, root-local scope,
dependency protocol, and completion paths. Close the decision with initialization
and extent examples, including forward dependencies and rejected cycles.

### OPEN-02 — Choose literal identity and its domain

Choose a literal policy and the backing guarantees needed to implement it.
Explain equal contents, separate occurrences, and cross-module uses. This decision
is needed for observations that depend on literal identity.

### OPEN-03 — Select an observation and execution-mode policy

Specify the observed relationship, permissions, invalidation behavior, and
stability interval, and choose its required and interpreted admission. Apply
`OPEN-02` when literals participate. State whether an ordinary result can carry
the observation and how retained-result restrictions are enforced.

## Validation

Structured-root cases cover both-branch legality, exactly-once evaluation,
unselected-path contracts, forward dependencies and cycles, source failures,
dangling borrows, budgets, and freezing rejection. Check initializer and extent
completion independently.

Address cases cover equal-valued independent objects, copied-pointer aliases,
assignment visibility, Take followed by reinitialization, exactly-once target
selection, Read and Write observations, failure and region cleanup, and literal
identity across modules. Define and test invalid-pointer display and comparison
without dereferencing a dangling pointer. If the selected scope includes payload
positions, test invalidation after variant replacement.

For the common valid subset, required, interpreted, and generated native execution
need separate evidence. Required and interpreted execution share an executor and
cannot establish independent correctness merely by agreeing. Check promised
address relations within each environment, then compare environment-independent
results. Do not strip all address text from comparisons in a way that hides alias
errors. Preserve source contracts for copying, cleanup, typed failure, and resource
stops while implementing the selected observation.

## References

- [Semantic execution](../docs/compiler/analysis/evaluation.md): current evaluator and publication boundaries.
- [Constant storage](constant-storage.md): retained values and reference graphs.
