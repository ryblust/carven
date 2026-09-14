module carven:semantic.evaluation.output;

import std;

enum class ConstantOutputStream { Standard, Error };

// Called synchronously; the recipient consumes bytes before returning.
using ConstantOutput = std::function<void(ConstantOutputStream, std::string_view)>;
