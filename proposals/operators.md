# Operator capabilities

- **Status:** Exploration
- **Implementation:** Not started
- **Scope:** Closed user-defined semantic hooks for existing operator tokens
- **Depends on:** [Generics](generics.md), including canonical evidence and associated-type normalization

## Summary

This proposal explores user-defined behavior for existing operator tokens through
closed static capabilities and canonical `impl` evidence. The carrier, identity,
token set, signatures, equality, and compound-assignment rules remain unaccepted.

`OPEN-02` first evaluates identity and visibility for the leading candidate.
`OPEN-01` then accepts, revises, or rejects that carrier. Token and signature
choices follow in `OPEN-03` through `OPEN-06`. Additional families are deferred.

## Context

Nominal arithmetic needs same-type and heterogeneous operands and results:

```text
Vec3   + Vec3   -> Vec3
Vec3   * f32    -> Vec3
Meter  * Meter  -> SquareMeter
Matrix * Matrix -> Matrix
```

Fieldwise derivation covers only some of these cases. Ordinary source operations
would keep algorithmic behavior, evaluation, failure, and access within Carven
analysis.

The design must preserve these existing or accepted constraints:

- Parsing fixes tokens, precedence, and associativity without operand-type lookup.
- `&&` and `||` short-circuit left to right. Other operands evaluate left to right,
  exactly once.
- Access-position `&` and `&&` denote Write and Take, not overloadable operators.
- Equality is currently a compiler-derived recursive property shared by runtime
  comparison, aggregate equality, pattern normalization, and constant facts.
  Floating patterns consider `0.0` and `-0.0` equal.
- Only complete plain assignment restores a Taken `var`; compound assignment
  preserves its existing availability requirements.
- Accepted generic semantics provide definition-site checking, canonical evidence,
  coherence, module-domain locality, anchored heads, and associated normalization.
  Those facilities are not implemented.

Generics owns the evidence model; this proposal maps tokens to its operations.
C++ export eligibility and concurrency guarantees require separate contracts.

## Goals and non-goals

The initial goal is checked nominal behavior for selected existing tokens,
including heterogeneous right operands and results where required. Each token
must resolve to one semantic operation before lowering, with ordinary evaluation,
access, failure, and diagnostics.

The scope excludes new tokens or precedence, ADL, overload sets, SFINAE,
implicit-conversion ranking, specialization, priority, local impl activation,
blanket impls, and caller-selected witnesses. It also excludes hooks for
short-circuiting, calls, indexing, member access, plain assignment, and access
markers. No general attribute namespace or new `@` meaning is proposed.
Owning string concatenation requires explicit allocation intent.

## Design

The following capability design is a candidate. Its identity model must be
evaluated before selecting the carrier.

### Candidate capability carrier

**Maturity:** Exploration.

The leading candidate maps tokens to compiler-defined capabilities implemented
through canonical evidence:

```carven
impl Add for Vec3 {
    type Rhs = Vec3;
    type Output = Vec3;
    fn add(left: Vec3, right: Vec3) -> Vec3 { ... }
}

impl Mul for Meter {
    type Rhs = Meter;
    type Output = SquareMeter;
    fn mul(left: Meter, right: Meter) -> SquareMeter { ... }
}
```

The examples show required behavior, not accepted spelling. `Rhs` may be an
associated type, capability argument, or another bounded static input. Canonical
evidence must uniquely determine `Output`.

```text
parsed operator
  + normalized operand types
  -> one compiler-known capability obligation
  -> zero or one canonical evidence
  -> one resolved named capability operation in SemIRProgram
```

This path has no member/non-member fallback, ADL, implicit-conversion candidate
set, specialization, priority, or declaration-order tie-break.

### Candidate operator surface

**Maturity:** Exploration; `OPEN-03` through `OPEN-06`.

A minimal candidate includes unary `-` and binary `+ - * / %`. Unary results,
heterogeneous right operands, and output types remain signature decisions.

Initial exclusions under consideration are equality, which serves several
semantic consumers, and compound assignment, which has place and availability
rules. Short-circuiting, plain assignment, `++`, `--`, calls, indexing, member
access, `?`, `as`, and access markers remain outside the candidate surface.
`OPEN-05` and `OPEN-06` must explicitly settle equality and compound assignment.

### Generic definition-site behavior

**Maturity:** Conditional on selecting the carrier in `OPEN-01`.

Resolve generic-body operators at definition site from declared capabilities:

```carven
fn sum<T: Add>(left: T, right: Add::Rhs<T>) -> Add::Output<T> {
    return left + right;
}
```

The spelling is illustrative. Application sites validate the unique substitution
and evidence; associated projections normalize before lowering. C++ substitution
does not reselect operator meaning.

### Diagnostics and compiler facts

Distinguish absent evidence, conflicts, invalid closed-capability names,
projection failure or cycles, signature mismatch, and excluded forms. Expression
diagnostics point to operators and operands; evidence diagnostics point to the
impl head or member.

If selected, the carrier records a resolved capability operation and canonical
evidence dependency in SemIRProgram.

### Candidate lowering boundary

**Maturity:** Conditional on `OPEN-01`; no backend selected.

Lowering may emit a named operation call or equivalent static form preserving
evaluation order, access, and failure behavior. It need not generate overloaded
C++ operators. Operand representation does not initiate further semantic lookup.

## Open decisions

**Next discussion:** `OPEN-02`

### OPEN-02 — What identity and visibility do compiler-known capabilities use?

- **Status:** Active
- **Depends on:** Generics `GEN-05`, `GEN-06`, `GEN-11`, `GEN-12`
- **Question:** Make desugaring and source `impl Add` name the same capability
  and coherence domain. This evaluates the candidate without accepting it.
- **Constraints:** One source identity, deterministic visibility, and evidence
  domain; a runtime-header path cannot supply that identity.
- **Options:** Language-builtin identities; official declarations at reserved
  `crafts.carven.std...` paths referenced through `std::`; or compiler-provided
  declarations visible through ordinary imports or a prelude.
- **Closure condition:** Define consistent lookup, desugaring, coherence, and
  missing-name diagnostics for the candidate. `OPEN-01` selects the carrier.

### OPEN-01 — Should existing operator tokens use canonical closed capabilities?

- **Status:** Blocked
- **Depends on:** `OPEN-02`
- **Activation condition:** The candidate has an evaluable identity and visibility model.
- **Question:** Choose the mechanism for user-provided operator behavior.
- **Constraints:** Type-independent parsing, deterministic resolution, resolved
  meaning before lowering, and unchanged evaluation/access rules.
- **Options:** Compiler-known capabilities with canonical impls; fieldwise
  derivation only, with its heterogeneous and algorithmic limitations; or no
  user-defined nominal operators.
- **Closure condition:** Compare real nominal types, definition-site checking,
  coherence, diagnostics, and visible costs using the identity model.

### OPEN-03 — Which token families enter the first slice?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`
- **Activation condition:** Carrier and identity are selected.
- **Question:** Select the smallest useful token set.
- **Constraints:** Preserve precedence and evaluation; each token maps to one
  closed capability identity.
- **Options:** Unary `-` and binary `+ - * / %`; binary arithmetic only; or a
  smaller set justified by a use case. Bitwise, shift, and ordering remain
  under `DEFER-01` through `DEFER-03`.
- **Closure condition:** Cover same-type, heterogeneous-right-operand, and
  heterogeneous-result examples with only the tokens they require.

### OPEN-04 — How are right operands and output types represented?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`, `OPEN-03`
- **Activation condition:** The initial tokens and uses are known.
- **Question:** Define signatures, evidence identity, and output normalization.
- **Constraints:** Canonical evidence selects one operation and output;
  heterogeneous operands and results are expressible with finite local solving.
- **Options:** Associated `Rhs` and `Output`; capability arguments with associated
  output; or another shape satisfying accepted generic rules.
- **Closure condition:** Fix each initial unary/binary signature and demonstrate
  unique normalization for generic and concrete calls.

### OPEN-05 — Is user-defined equality excluded from the initial operator scope?

- **Status:** Blocked
- **Depends on:** `OPEN-01`, `OPEN-02`
- **Activation condition:** The capability identity model is selected.
- **Question:** Determine equality's scope across runtime, aggregate, pattern,
  and constant consumers.
- **Constraints:** If admitted, define shared evidence, whether implementations
  may be non-constant, and required equivalence properties.
- **Options:** Exclude `==`/`!=` initially, or admit them after completing a
  unified equality contract.
- **Closure condition:** Record the exclusion or provide the complete contract
  across all consumers.

### OPEN-06 — How does compound assignment relate to binary capabilities?

- **Status:** Blocked
- **Depends on:** `OPEN-03`, `OPEN-04`
- **Activation condition:** A binary capability and its result shape are selected.
- **Question:** Define compound assignment with target-before-value evaluation
  and availability restoration.
- **Constraints:** Evaluate operands once, preserve unavailable-owner rules,
  and satisfy the left-place result contract.
- **Options:** Derive it when binary output can be fully assigned to the left
  type; use a separate closed assignment capability; or exclude it initially.
- **Closure condition:** Select a rule using mutable-place examples covering
  access, Take, failure, and evaluation.

## Deferred work

| ID | Direction and reason deferred | Dependencies | Reactivation condition |
| --- | --- | --- | --- |
| `DEFER-01` | Bitwise `~ & | ^` needs its own admissible-type contract. | `OPEN-01` through `OPEN-04` | A generic or nominal API needs the operations and defines operand, result, and diagnostic rules. |
| `DEFER-02` | Shifts need right-operand and invalid-count rules. | `OPEN-01` through `OPEN-04` | An API needs `<<`/`>>` and defines count, result, failure, and diagnostics. |
| `DEFER-03` | Ordering needs consistent relations among `< <= > >=`. | `OPEN-01` through `OPEN-04` | A generic or nominal API needs and defines those relations. |
| `DEFER-04` | `str + str` needs an owning result and explicit allocation intent. | Implemented owning String; an unresolved concatenation allocation contract | A concrete concatenation API makes allocation visible in source. |
| `DEFER-05` | Fieldwise derive shorthand needs an implemented canonical path to generate. | Implemented operator capabilities and evidence | Repeated fieldwise implementations justify syntax that generates the same canonical impl. |

## Implementation

Implementation requires closure of `OPEN-01` through `OPEN-06` for the selected
slice and implemented generic associated-type normalization. Deliver capability
identities, evidence validation, operator-to-operation resolution, source
diagnostics, and resolved-operation lowering together.

Existing tokens require no grammar change. Add representations only for the
selected operations.

## Validation

Decision experiments and the delivered slice cover:

- same-type operands, heterogeneous right operands, and heterogeneous outputs;
- definition-site resolution and application-site evidence validation;
- stable rejection of duplicate and overlapping evidence;
- unchanged excluded equality, short-circuit, assignment, access, and control forms;
- unchanged precedence and associativity;
- left-to-right, exactly-once operands with ordinary failure and access behavior;
- C++20/C++23 compilation, linking, and execution, without fixing private generated
  names or capability representation.
