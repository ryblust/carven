# Language

This directory covers writing Carven programs, their syntax, and their validity
and observable behavior for the current checkout. The [Tutorial](tutorial.md)
introduces the language in learning order; [Grammar](grammar.md) defines its
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
| Determine evaluation order, loop behavior, or match selection | [Evaluation and control flow](control-flow.md) |
| Declare, propagate, catch, or rethrow a typed failure | [Failure contracts](failures.md) |
| Evaluate constants, execute `const fn` or constant blocks, or freeze a result | [Values and constants](constants.md) |
| Construct, borrow, mutate, or format Unicode text | [Text and interpolation](text.md) |
| Use native names and types, C strings, source fragments, or C++ function boundaries | [C++ interoperation](interop.md) |
| Print values, define an entry or test, or interpret an assertion or diagnostic | [Printing, entry points, and tests](execution.md) |

## Reading across topics

Start with the page for the operation you are using. Each page includes its
local validity, evaluation, and lifetime rules. For shared concepts, consult
[type context](types.md#type-context-and-inference),
[Read/Write/Take access](ownership.md), and
[evaluation order](control-flow.md#operators-and-evaluation).

Some tasks cross a specific boundary:

- For `const fn`, read [constant execution](constants.md#compile-time-function-execution)
  alongside the ordinary [call rules](functions.md#functions-and-calls).
  For `const test`, the same executor subset applies, while
  [test ordering and assertions](execution.md#entry-points-and-tests) define the test behavior.
- For slices, [runtime views](types.md#read-only-slices) borrow live backing;
  [frozen constant slices](constants.md#frozen-constant-slices) define publication
  with static backing.
- For pointers, read the [local non-null checks](pointers.md#local-non-null-checks)
  and [native responsibility](pointers.md#native-representation-and-responsibility)
  together. Pointer target access does not establish general target liveness.
- For output, [interpolation](text.md#string-interpolation) constructs formatted
  text; [printing](execution.md#printing) defines direct output and structural display.

## Reference boundaries

Language-visible rules remain here even when they involve C++.
[Compiler](../compiler/README.md) describes analysis and semantic publication;
[Backend](../compiler/backend/README.md) describes their C++ realization.
[CLI](../toolchain/cli.md) and [Toolchain](../toolchain/artifacts.md) cover invocation and native
integration. Repository test placement and validation belong to
[Testing](../development/testing.md).

Each complete rule has one topic owner. Links identify shared rules and
stage-specific requirements.
