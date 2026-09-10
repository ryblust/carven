# Documentation

These documents describe Carven in this checkout.

## Reading paths

| Need | Document | Responsibility |
| --- | --- | --- |
| Learn the basics | [Tutorial](tutorial.md) | A first program, basic concepts, and short examples |
| Complete a task | [Examples](../examples/README.md) | Runnable programs, local explanations, and expected output |
| Look up a source form | [Grammar](grammar.md) | Encoding, tokens, syntax, precedence, and parsing |
| Determine program behavior | [Language semantics](semantics.md) | Program validity and behavior, including ownership, failures, tests, and C++ boundaries |
| Invoke the compiler | [CLI](cli.md) | Options, input paths, output writes, and process status |
| Build generated C++ | [Toolchain and artifacts](toolchain.md) | Native requirements, artifact paths, compilation, linking, and build integration |
| Work on semantic analysis | [Compiler architecture](compiler.md) | Semantic construction, analysis, publication, internal ownership, and dependencies |
| Work on code generation | [C++ generation](backend.md) | C++ representation, target construction, lowering, and emission |
| Validate changes | [Testing](testing.md) | Test placement, assertions, and validation commands |
| Write repository C++ | [Conventions](conventions.md) | Source organization and C++ rules |
| Evaluate a design | [Principles](principles.md) | Criteria for language and implementation decisions |

## Maintenance

- Describe current behavior concretely. Distinguish language rules, implementation
  facts, and design goals; state restrictions and external responsibilities where
  they affect use.
- Give each detailed rule one owning section. Keep necessary context local and
  cross-references exceptional; navigation belongs in indexes and contents lists.
- Explain explicit forms, omissions, evaluation, lifetimes, and failures where
  relevant. Preserve the reasoning and examples needed to understand a rule.
- Order tutorials and examples from imports and declarations to helpers, callers,
  and tests. Explain providers before consumers, then show build commands and
  expected output.
- Resolve disagreements between documentation, implementation, and tests
  explicitly. Update affected explanations and links together; keep proposals and
  historical accounts outside the current reference documents.
