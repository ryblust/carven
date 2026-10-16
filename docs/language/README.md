# Language

This directory covers writing Carven programs, their syntax, and their validity
and observable behavior for the current checkout.

## Find a document

| Reader task | Reference |
| --- | --- |
| Learn to write Carven | [Tutorial](tutorial.md), then runnable [examples](../../examples/README.md) |
| Find a standard capability, module, or API contract | [Standard capabilities](standard.md) |
| Check source spelling or parsing | [Grammar](grammar.md) |
| Resolve imports, names, or declaration visibility | [Modules and names](modules.md) |
| Determine a type, numeric conversion, or slice operation | [Types and context](types.md) |
| Declare or construct a class, structure, array, or enum | [Aggregates](aggregates.md) |
| Understand copying, mutation, Take, or a borrowed value's lifetime | [Bindings, access, and mutation](ownership.md) |
| Create a pointer, check for null, or access its target | [Pointer values](pointers.md) |
| Declare or call functions, create closures, or use callable views | [Functions and callable values](functions.md) |
| Await cold operations, start lexical children, or request cancellation | [Async functions and lexical children](async.md) |
| Construct checked intervals, sample monotonic time, or wait for a deadline | [Time and waiting](time.md) |
| Determine evaluation order, loop behavior, or match selection | [Evaluation and control flow](control-flow.md) |
| Declare, propagate, catch, or rethrow a typed failure | [Failure contracts](failures.md) |
| Evaluate constants, execute `const fn` or `const` blocks, or freeze a result | [Values and constants](constants.md) |
| Construct, borrow, mutate, or format Unicode text | [Text and interpolation](text.md) |
| Use native names and types, C strings, source fragments, or C++ function boundaries | [C++ interoperation](interop.md) |
| Read input, print values, or frame byte records | [Input and output](io.md) |
| Define an entry or test, or interpret an assertion or diagnostic | [Entry points, tests, and diagnostics](execution.md) |

## Reference boundaries

Each topic defines the validity, evaluation, and lifetime rules for its operations,
including language-visible C++ boundaries. Compiler references describe analysis
and generation; toolchain references cover invocation and native integration.
Development references govern repository conventions and validation.
