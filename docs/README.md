# Documentation

These documents describe the supported language, its tools, and their
implementation. This index owns reading navigation and document boundaries.
Each detailed rule has one owning document. A guide example illustrates a rule;
an implementation description explains its realization.

| Document | Role | Owns |
| --- | --- | --- |
| [language.md](language.md) | Guide | Introductory examples of supported source |
| [grammar.md](grammar.md) | Language contract | Encoding, tokens, syntax, precedence, and parsing |
| [semantics.md](semantics.md) | Language contract | Meaning, validity, type context, access, ownership, closures, failure, and semantic diagnostics |
| [cli.md](cli.md) | Tool contract | Invocation, host input paths, output writes, inspection, and process status |
| [toolchain.md](toolchain.md) | Integration contract | Native requirements, artifact paths, C++ consumption, build integration, and installed support |
| [compiler.md](compiler.md) | Implementation | Frontend-to-SemIR construction, analysis, publication gates, and internal ownership |
| [backend.md](backend.md) | Implementation | SemIR-to-C++ planning, representation, lowering, dependencies, and emission |
| [testing.md](testing.md) | Contributor workflow | Test responsibilities, evidence, placement, and validation commands |
| [conventions.md](conventions.md) | Contributor rules | Project-authored C++ and source organization |
| [principles.md](principles.md) | Design criteria | Evaluation of language and implementation decisions |

## Boundaries

Grammar and semantics together define the language. CLI and toolchain contracts
define how a source batch reaches native artifacts. Compiler and backend
documents explain internal mechanisms without adding source restrictions.
For example, capture syntax belongs to grammar, capture lifetime to semantics,
its analysis to compiler, and its C++ fields to backend.

Semantic C++ interoperability owns admitted functions, types, and boundary
behavior. Toolchain owns their headers, placement, and native build inputs.
CLI owns filesystem writes; build-system staging and promotion belong to
integration. Test statements are language semantics; running the repository's
test suites is contributor workflow.

## Reading paths

- Learn the language: start with the language guide, then consult grammar for
  exact source forms and semantics for validity and behavior.
- Use the compiler: read CLI for invocation and output handling, then toolchain
  for native compilation, linking, and build integration.
- Work on the implementation: read compiler for semantic construction and
  analysis, then backend for C++ generation.
- Contribute changes: use conventions for source organization and C++ rules,
  testing for validation and formatting, and principles for design decisions.

## Maintenance

Describe current behavior in neutral, concrete terms. State applicable forms,
conditions, results, rejection, and delegated responsibilities. Each document
opens with its own scope; general reading directions belong in this index.
Keep a cross-document link when a specific rule depends on another contract
or a guide example needs its exact rules. Link to the owning section rather
than repeating its detailed rules or adding a general related-documents list.
Record observable mechanisms, including copies, aliases, evaluation order, and
lifetime boundaries.

When code, tests, and a contract disagree, identify and resolve the disagreement;
neither implementation acceptance nor a test silently changes a language rule.
Keep proposals outside current contracts until implemented. These documents
do not promise compatibility with earlier or later compiler versions, generated
layouts, or compiler-private interfaces.
