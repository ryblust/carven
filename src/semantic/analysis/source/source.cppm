module carven:semantic.analysis.source;

import :semantic.semir.ids;
import :semantic.semir.type;
import :source.text;
import :support.function_ref;
import std;

// Builtin values describe construction. TypeIDs are emitted only with a
// successfully published program and belong to that same program owner.
using SourceType = std::variant<BuiltinType, TypeID>;

struct SourceOccurrence final {
    SourceSpan location;
    std::optional<SourceSpan> definition;
    std::optional<SourceType> type;
};

// One synchronous observation per semantic analysis. The span borrows temporary
// storage. Recipients copy retained values and keep any accompanying program alive.
// Declaration/nominal failures contribute no occurrences; failed bodies contribute
// none of their local observations. Definitions do not depend on type availability.
using SourceAnalysisOutput = FunctionRef<void(std::span<const SourceOccurrence>) noexcept>;
