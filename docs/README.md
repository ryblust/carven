# Documentation

These documents describe Carven in this checkout. Each detailed rule has one
owning document; tutorials and examples explain how to use it.

## Reading paths

| Need | Document | Scope |
| --- | --- | --- |
| Learn the basics | [Tutorial](tutorial.md) | A first program, basic concepts, and short examples |
| Complete a task | [Examples](../examples/README.md) | Runnable programs, local explanations, and expected output |
| Look up a source form | [Grammar](grammar.md) | Encoding, tokens, syntax, precedence, and parsing |
| Determine program behavior | [Language semantics](semantics.md) | Validity, types, evaluation, ownership, failures, and C++ boundary behavior |
| Invoke the compiler | [CLI](cli.md) | Options, input paths, output writes, and process status |
| Build generated C++ | [Toolchain and artifacts](toolchain.md) | Native requirements, artifact paths, compilation, linking, and build integration |
| Work on semantic analysis | [Compiler architecture](compiler.md) | Construction, analysis, publication, and internal ownership |
| Work on code generation | [C++ generation](backend.md) | Representation, lowering, dependencies, and emission |
| Validate changes | [Testing](testing.md) | Test placement, assertions, and validation commands |
| Write repository C++ | [Conventions](conventions.md) | Source organization and C++ rules |
| Evaluate a design | [Principles](principles.md) | Criteria for language and implementation decisions |

## Responsibilities

The repository README introduces the project, its value, and its design goals.
It may use promotional language while distinguishing goals from verified
behavior. Reference documents state concrete rules and requirements neutrally.

Grammar defines source forms. Semantics defines their meaning and validity,
including observable copies, aliases, evaluation order, lifetimes, and native
boundary behavior. Compiler and backend describe how those rules are implemented.

CLI defines the compiler process and its filesystem writes. Toolchain defines
native build inputs, artifact layout, and build-system scheduling and promotion.
Test statements belong to semantics; repository validation belongs to testing.

The tutorial teaches concepts in order. Examples combine them into complete
tasks. Both may briefly explain a rule where needed, while detailed conditions
and exceptions remain in the reference documents.

## Maintenance

Describe supported behavior with concrete conditions and results. Keep proposals
and historical explanations outside these documents. State restrictions and
external responsibilities where they affect a user's operation. Claims about
safety or cost need corresponding enforcement or evidence.

For each user-visible feature, account for its explicit forms, the meaning of
omissions, automatic operations, evaluation and lifetime boundaries, and failure
behavior. A small example belongs beside a rule when it resolves an ambiguity;
complete task programs belong in examples.

Keep navigation in the indexes. Detailed documents should be understandable
without following cross-document links. Retain such a link only when a specific
rule requires a separate contract. Source links and local contents lists help
locate material without duplicating it.

When documentation, implementation, and tests disagree, resolve the difference
explicitly. Compiler acceptance alone does not establish a language rule. Move
each unique rule to its owning section before removing redundant text, and
update incoming links when a file or heading changes.
