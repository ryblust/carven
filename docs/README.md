# Documentation

The repository [README](../README.md) introduces Carven, provides the first
compiler example, and directs readers to detailed documentation.

This directory contains an introductory guide, public contracts, maintainer
contracts, repository policies, and design criteria. The table identifies the
primary document for each subject. Entry points and guides include concise
summaries and common operational guidance; the primary document carries the
complete maintained explanation. Public behavior comes only from the owning
public contract; maintainer documents, tests, guides, and proposals do not add
separate promises.

| Document | Kind | Responsibility |
| --- | --- | --- |
| [language.md](language.md) | Informative guide | Concise introduction to writing supported Carven |
| [grammar.md](grammar.md) | Public contract | Source encoding, tokens, productions, precedence, and token-only disambiguation |
| [semantics.md](semantics.md) | Public contract | Observable behavior, failure contracts, semantic validity, stable diagnostic identities, and C++ interoperation |
| [cli.md](cli.md) | Public contract | Command invocation, source-path derivation, output selection, test emission, and inspection commands |
| [compatibility.md](compatibility.md) | Public contract | Compiler-host requirements, generated-C++ consumer support, data-model limits, and stability boundaries |
| [compiler.md](compiler.md) | Maintainer contract | Implemented compiler stages, semantic publication gates, persistent facts, owners, and dependency direction |
| [backend.md](backend.md) | Maintainer contract | Implemented `TargetProgram` ownership, semantic-to-C++20 lowering, target-unit verification, and emission |
| [testing.md](testing.md) | Repository policy | Validation workflow, test-suite responsibilities, execution boundaries, coverage, and target organization |
| [conventions.md](conventions.md) | Repository policy | C++ source conventions, borrowing, lifecycle terminology, errors, modules, naming, and formatting |
| [philosophy.md](philosophy.md) | Design criteria | Criteria for evaluating language and compiler design proposals |
