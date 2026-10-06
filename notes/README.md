# Research notes

These articles explain programming-language and compiler mechanisms through
standards, research, and implementation examples. Each article develops a
self-contained subject with sources collected at the end.

## Reading by question

| Question | Article | Focus |
| --- | --- | --- |
| What do coroutines provide, and what must a runtime still implement? | [Coroutines, execution, and lifetime](async-programming.md) | Suspension, scheduling, completion, cancellation, structured ownership, and the roles of compilers and libraries |
| Which program facts should survive each compiler boundary? | [Compiler architecture: semantics, analysis, and lowering](compiler-architecture.md) | Representation authority, legality, specialization, verification, and preparation costs across compiler designs |
| When does failure need a stored value instead of a control edge? | [Failure models: effects, control, and runtime representation](failure-models.md) | Typed effects, result values, exceptions, callable boundaries, and suspended completion |
| How do module ownership and reachability affect builds? | [C++ modules: from textual inclusion to semantic ownership](cpp-modules.md) | Imports, interface and implementation partitions, cycles, BMI dependencies, and incremental builds |

## Maintenance

- Explain mechanisms in concise, factual, neutral prose. Use examples and
  counterexamples where they clarify a rule.
- Keep the content independent of repository implementation, project decisions,
  task history, and delivery plans.
- Distinguish specifications, proposals, implementation examples, and measured
  observations. State versions and assumptions where they affect a claim.
- Collect sources and related reading under `References` at the end, grouped
  by subject when useful. Use an inline citation only when identifying the source is necessary to understand
  the statement.
- Consolidate repeated explanations while retaining useful reasoning and sources.
  Give distinct subjects separate articles and update links when moving them.
