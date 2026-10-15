module carven:semantic.evaluation.output;

import :support.function_ref;
import std;

enum class ExecutionOutputStream { Standard, Error };

// Called synchronously; the recipient consumes bytes before returning.
// The callable is borrowed for the complete analysis or execution operation.
using ExecutionOutput = FunctionRef<void(ExecutionOutputStream, std::string_view) noexcept>;
