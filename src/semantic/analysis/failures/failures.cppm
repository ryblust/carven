module carven:semantic.analysis.failures;

import :semantic.hir;
import :semantic.hir.ids;
import std;

auto normalize_failure_members(std::vector<HIRTypeID> failures) noexcept -> std::vector<HIRTypeID>;
