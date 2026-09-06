# Compiler architecture

This document describes semantic construction and analysis, including publication
gates, ownership boundaries, and dependency direction.

## Pipeline

`compiler/` owns the compilation input contract and end-to-end orchestration.
`compiler.request` describes the closed source batch and depends only on source
types; the frontend consumes this contract independently. `compiler.compile`
sequences parsing, semantic analysis, and artifact generation. Command-line
input preparation and filesystem output belong to `driver/`.

```text
CompilationRequest → SyntaxProgram → ProgramDraft → SemIRProgram
```

`parse_program` parses the closed source batch and resolves module imports.
`analyze` constructs declarations and typed structured bodies, solves types and
failure sets, validates contracts, checks ownership and callable loans, and
publishes an immutable semantic program. Source errors discard the draft.

## Publication gates

The analysis driver first builds the declaration catalog and completes
signatures and required constants, then elaborates the body batch and emits
unused-import diagnostics. Publication consumes the draft and performs these
steps in order:

1. Solve construction types and failure terms.
2. Validate global semantic contracts.
3. Resolve body drafts into typed structured bodies.
4. Analyze the body batch, including ownership and callable relationships.
5. Publish the bodies and seal the program.

Failure at a gate prevents publication. Warnings accompany a successful
`SemIRProgram`. No unresolved draft is passed to target planning.

## Ownership and identity

`SyntaxProgram` owns source provenance, syntax trees, and resolved imports.
`ProgramDraft` consumes it and owns mutable declarations, canonical interning,
construction types, failure constraints, and body construction. Declarations
may reserve identities for recursion and forward references.

Program IDs belong to one program. Binding, pattern, scope, and lifetime IDs
belong to one body. Owning query surfaces validate identity and range. Expression,
statement, and region occurrences are recursively owned values.

Publication consumes construction state. `SemIRProgram` retains resolved
semantic data and provenance; lowering does not query source syntax.

## Structured semantics

Bodies retain conditionals, loops, matches, handlers, lexical scopes, and exits.
Places describe storage identity and projection evaluation. Values describe
computation. Initialization, assignment, and Take remain distinct operations.
Children and operation contracts specify evaluation order.

Bindings carry their role and access. Scope and full-expression boundaries
record lifetimes. Function return, failure propagation, loop transfer, and test
exit retain their destinations. Nested callables have separate boundaries.

Match distinguishes a source place from an owned temporary. Pattern owners and
guards retain their order. Constant-inactive source is validated but contributes
no executed operations or ownership transitions.

## Solving and validation

Canonical types, callable signatures, constants, and failure sets are the
published query surfaces. Construction types and failure terms are solved
before publication. Failure inference computes the least fixed point of the
program's failure constraints.

Constant evaluation, operator selection, and pattern coverage each have one
semantic implementation. Construction and structured contract checking consume those rules.
Operator contracts are checked on structured expressions; ownership analysis
consumes their access and result relationships.

Local construction checks its preconditions. Program validation checks owner
and range relations, type and call contracts, scope and control legality,
binding relations, declaration topology, and cross-body callable relations.
Each declaration and body has its required unique owner; each closure has one
construction site and one body.

## Ownership and callable loans

Ownership analysis walks resolved structured bodies. Availability belongs to an
owner; holder relationships belong to storage positions within that owner.
Known field and element writes replace the relationships at that position;
unknown element writes merge possible relationships. Ordinary scalar elements
need no relationship rows.

Value captures carry copied relationships. Write captures refer to live
storage, so consumers follow the target's current contents. Lexical regions and
full expressions release their own objects on each normal and control exit.
A result's destination lifetime does not change the execution position.

Calls map parameters and captures to actual storage. Aliases share one state,
including the order of external writes. Call answers contain returned
relationships and external state for normal and typed failure completion;
completed locals are discarded. Queries normalize reachable input storage,
aliases, active accesses, and relative lifetimes. Recursive dependencies reach a
fixed point over these relationships. Query state remains private to semantic
analysis and never enters generation or runtime.

Unfinished calls retain direct place and borrowed-target accesses. Array
iteration retains its source owner. Match guards additionally require stable
subject storage. Branches merge only real successors; loops include entry,
backedges, and exits. Diagnostic witnesses do not distinguish execution states.
Return and failure states have caller consumers; test termination has none.
All source operations receive contract checks independently of execution-state
analysis, including unreachable source.

## Diagnostics and dependencies

Source operations retain origins. Implicit operations use linked expansion
origins. Diagnostics are produced at the rule owner. Compiler-private invariant
failures terminate at the violated boundary.

Frontend facilities depend on source and syntax. Semantic construction consumes
syntax; analysis consumes resolved operations. The backend consumes published
semantics. Runtime support and build orchestration do not determine Carven
access or ownership legality.

## External C++ delegation

Scope bindings distinguish Carven declarations from explicitly imported C++ names
and namespace lookup environments. External names do not reserve synthetic
function or nominal declaration identities. Named external types and name
operations share `CppNameReference`: a context module, a `Global` or
`ModuleScope` lookup origin, and a nonempty identifier path. The context module
supplies declarations even for global lookup. Semantic construction validates
identifier spelling and Carven-owned contracts, not external declaration
existence or identity. Type arguments and operation operands remain separate
from the name. Publication verifies paths, module identities, and references.

Published types distinguish named external type expressions from external result
queries. Queries retain their originating operation and operand types and access
as published delegation records. Structured external operations share ordinary
operands, source provenance, scopes and control flow with Carven operations.

Publication verifies Carven-owned contracts and the structural integrity of
external delegation. External result queries remain in the published program
for C++ type determination. Ownership analysis checks known callable borrows and
Write captures at external operations before publication.
