module carven:semantic.analysis.expr.result;

import :semantic.analysis.diagnostics;
import :support.task;
import std;

// Non-admission has no diagnostic yet. Its required-expression consumer owns it.
struct ExpressionNotAdmitted final {};

using ExpressionFailure = std::variant<AnalysisFailure, ExpressionNotAdmitted>;

template<typename Value>
using ExpressionResult = std::expected<Value, ExpressionFailure>;

template<typename Value>
using ExpressionTask = ContinuationTask<ExpressionResult<Value>>;
