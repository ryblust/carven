module carven:semantic.analysis.resolution;

import :semantic.hir.ids;
import :source.provenance.ids;
import :source.text;
import std;

enum class LookupError {
    Missing,
    Diagnosed,
};

template<typename T>
using LookupResult = std::expected<T, LookupError>;
