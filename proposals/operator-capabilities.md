# Operator capabilities

- **Status:** Exploration
- **Scope:** Closed user-defined semantic hooks for existing operator tokens

## Summary

This proposal explores user-defined behavior for existing operator tokens through
closed static capabilities and canonical `impl` evidence. The carrier, identity,
token set, signatures, equality, and compound-assignment rules remain unaccepted.

`OPEN-02` first evaluates identity and visibility for the leading candidate.
`OPEN-01` then accepts, revises, or rejects that carrier. Token and signature
choices follow in `OPEN-03` through `OPEN-06`. Additional families are deferred.

### Goals and non-goals

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

## Context

Nominal arithmetic needs same-type and heterogeneous operands and results:

```text
Vec3   + Vec3   -> Vec3
Vec3   * f32    -> Vec3
Meter  * Meter  -> SquareMeter
Matrix * Matrix -> Matrix
```

Use ordinary source operations to express algorithmic behavior with checked
evaluation, failure, and access contracts.

The design must preserve these existing or accepted constraints:

- Parsing fixes tokens, precedence, and associativity without operand-type lookup.
- `&&` and `||` short-circuit left to right. Other operands evaluate left to right,
  exactly once.
- Access-position `&` and `&&` denote Write and Take, not overloadable operators.
- Equality support is compiler-selected. Arrays and tuples derive elementwise
  support, and eligible payload enums derive case/payload support. Ordinary
  structures and value classes do not support `==` or `!=`. Supported comparison,
  pattern normalization, and constant facts must remain consistent; floating
  patterns consider `0.0` and `-0.0` equal.
- Only complete plain assignment restores a Taken `var`; compound assignment
  preserves its existing availability requirements.
- Generic definition-site checking is implemented. The accepted capability design
  adds canonical evidence, coherence, module-domain locality, anchored heads, and
  associated normalization; these extensions are not implemented.

C++ export eligibility and concurrency guarantees require separate contracts.

## Design

Evaluate the following candidate's identity model before selecting the carrier.

### Ownership and prerequisites

The compiler owns token-to-capability mapping, canonical evidence selection,
operand/result checking, and publication of the resolved operation. Builtin
capability identities may live in standard modules. Standard Craft and user
source supply nominal types and operation bodies. Runtime Craft supplies shared
native mechanisms only where the selected operations need them.

Select the required evidence and signature contracts with the initial nominal
examples. Compare closed concrete signatures with signatures exposing generic
associated projections. Concrete signatures can publish their result type
directly; symbolic projections require finite normalization before publication.
Both use the canonical evidence and resolved-call model. A selected slice states
whether it supports concrete nominal impls, generic impls, and generic-body use,
with the checking required for each. Public generic extensions are prerequisites
only where those signatures or uses consume them.

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

The examples use provisional spelling. `Rhs` may be an
associated type, capability argument, or another bounded static input. Canonical
evidence must uniquely determine `Output`.

Compare `Vec3 * f32` and `Vec3 * Vec3` in the same compilation. An associated
`Rhs` fixes one right type per evidence key; supporting both requires the key to
distinguish those obligations. Select operand identity and output derivation
together, preserving unique resolution without candidate ranking.

```text
parsed operator
  + normalized operand types
  -> one compiler-known capability obligation
  -> zero or one canonical evidence
  -> one resolved named capability operation in SemIRProgram
```

Resolve the obligation through canonical evidence. Diagnose missing or conflicting
evidence at the source operation.

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

For a slice admitting generic-body operators, resolve them at definition site
from declared capabilities:

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

### OPEN-02 — What identity and visibility do compiler-known capabilities use?

- **Status:** Active
- **Depends on:** Named capabilities, canonical compiler/source evidence,
  compilation-wide coherence, and module-domain locality for nominal impl heads
- **Question:** Make desugaring and source `impl Add` name the same capability
  and coherence domain. This evaluates the candidate without accepting it.
- **Constraints:** One source identity, deterministic visibility, and evidence
  domain; a runtime-header path cannot supply that identity.
- **Current boundary:** Standard entries combine source and compiler-provided
  declarations under one canonical `crafts.carven.std...` module identity.
  Imports and local aliases resolve through that identity.
- **Options:** A compiler-provided capability in a standard module, or a
  compiler-recognized source capability in that module. Define how desugaring
  selects it, how source impls name it, and which producer supplies evidence.
  Consider a separate language identity only with a concrete lookup requirement
  that the standard-module model cannot satisfy.
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
  output; or closed concrete signatures using the same canonical evidence rules.
- **Closure condition:** Fix each initial unary/binary signature and demonstrate
  unique resolution for the selected concrete and generic uses, including two
  right-operand types for one left type. Identify any symbolic projections and
  the finite normalization they require.

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
slice. Deliver capability identities, evidence validation, operator-to-operation
resolution, source diagnostics, and resolved-operation lowering together.
Implement finite associated-type normalization for signatures exposing those
projections. Select a concrete-only or generic delivery from the nominal examples
and required source algorithms; each delivery preserves canonical coherence.

Existing tokens require no grammar change. Add representations only for the
selected operations.

## Validation

Decision experiments and the delivered slice cover:

- same-type operands, heterogeneous right operands, and heterogeneous outputs;
- multiple right-operand types for one left type with unique evidence;
- definition-site resolution and application-site evidence validation for admitted
  generic uses;
- stable rejection of duplicate and overlapping evidence;
- unchanged excluded equality, short-circuit, assignment, access, and control forms;
- unchanged precedence and associativity;
- left-to-right, exactly-once operands with ordinary failure and access behavior;
- C++20/C++23 compilation, linking, and execution, without fixing private generated
  names or capability representation.
