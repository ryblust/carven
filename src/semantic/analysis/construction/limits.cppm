module carven:semantic.analysis.construction.limits;

import std;

// Source aggregate construction and static derivation have independent work budgets.
inline constexpr auto maximum_construction_work = 524'288uz;

// Generic instance closure and symbolic storage checks account for separate work.
inline constexpr auto maximum_generic_instances = 65'536uz;
inline constexpr auto maximum_generic_type_work = 65'536uz;
inline constexpr auto maximum_generic_depth = 256uz;
