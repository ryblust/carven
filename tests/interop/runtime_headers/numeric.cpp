#include <carven/runtime/numeric.hpp>

static_assert(carven::runtime::Integer<int>);
static_assert(carven::runtime::integer_add(20, 22) == 42);
