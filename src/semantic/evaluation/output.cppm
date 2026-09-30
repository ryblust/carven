module carven:semantic.evaluation.output;

import std;

enum class ExecutionOutputStream { Standard, Error };

// A value computed only to determine a type is not the execution of its
// declaration; its output is discarded.
enum class ExecutionOutputMode { Write, Discard };

// Called synchronously; the recipient consumes bytes before returning.
using ExecutionOutput = std::function<void(ExecutionOutputStream, std::string_view)>;
