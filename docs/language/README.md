# Language

This directory covers writing Carven programs, their syntax, and their validity
and observable behavior for the current checkout. The tutorial
introduces the language in learning order; the grammar defines its
spelling and parsing. Topic references define the rules for each language feature.

## Find a document

| Reader task | Reference |
| --- | --- |
| Learn to write Carven | [Tutorial](tutorial.md), then runnable [examples](../../examples/README.md) |
| Check source spelling or parsing | [Grammar](grammar.md) |
| Resolve imports, names, or declaration visibility | [Modules and names](modules.md) |
| Determine a type, numeric conversion, or slice operation | [Types and context](types.md) |
| Declare or construct a class, structure, array, or enum | [Aggregates](aggregates.md) |
| Understand copying, mutation, Take, or a borrowed value's lifetime | [Bindings, access, and mutation](ownership.md) |
| Create a pointer, check for null, or access its target | [Pointer values](pointers.md) |
| Declare or call functions, create closures, or use callable views | [Functions and callable values](functions.md) |
| Await cold operations, start lexical children, or request cancellation | [Async functions and lexical children](async.md) |
| Determine evaluation order, loop behavior, or match selection | [Evaluation and control flow](control-flow.md) |
| Declare, propagate, catch, or rethrow a typed failure | [Failure contracts](failures.md) |
| Evaluate constants, execute `const fn` or `const` blocks, or freeze a result | [Values and constants](constants.md) |
| Construct, borrow, mutate, or format Unicode text | [Text and interpolation](text.md) |
| Use native names and types, C strings, source fragments, or C++ function boundaries | [C++ interoperation](interop.md) |
| Print values, define an entry or test, or interpret an assertion or diagnostic | [Printing, entry points, and tests](execution.md) |

## Reading across topics

Start with the page for the operation you are using. Each page includes its
local validity, evaluation, and lifetime rules. Shared concepts include type
context, Read/Write/Take access, and evaluation order.

Some tasks cross a specific boundary:

- `const fn` follows ordinary call rules and static-execution admission.
  `const test` uses the same executor subset with test ordering and assertions.
- Runtime slices borrow live backing; frozen constant slices have program-lifetime
  backing.
- Pointer access combines local non-null checks with native target-lifetime
  obligations.
- Interpolation constructs formatted text; printing performs direct output and
  structural display.

## Reference boundaries

Language-visible rules remain here even when they involve C++.
[Compiler](../compiler/README.md) covers analysis, semantic publication, and C++
generation. [Toolchain](../toolchain/README.md) covers invocation and native
integration. [Development](../development/README.md) covers repository conventions,
test placement, and validation.
