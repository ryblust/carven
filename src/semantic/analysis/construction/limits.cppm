module carven:semantic.analysis.construction.limits;

import std;

// Source aggregate construction and static derivation have independent work budgets.
inline constexpr auto maximum_construction_work = 524'288uz;
