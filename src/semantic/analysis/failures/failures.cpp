module carven:semantic.analysis.failures.impl;

import :semantic.analysis.failures;
import std;

auto normalize_failure_members(std::vector<HIRTypeID> failures) noexcept -> std::vector<HIRTypeID> {
    // Canonicalization is used while declaration contracts are still under
    // construction. It must therefore depend only on closed identity facts,
    // never on declaration records that may not have been defined yet.
    std::ranges::sort(failures, {}, &HIRTypeID::index);
    failures.erase(std::ranges::unique(failures).begin(), failures.end());
    return failures;
}
