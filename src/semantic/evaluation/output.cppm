module carven:semantic.evaluation.output;

import std;

enum class ExecutionOutputStream { Standard, Error };

// Called synchronously; the recipient consumes bytes before returning.
using ExecutionOutput = std::function<void(ExecutionOutputStream, std::string_view)>;
