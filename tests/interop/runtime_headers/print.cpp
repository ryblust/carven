#include <carven/runtime/print.hpp>

static_assert(noexcept(carven::runtime::println(1, true, 2.5)));
static_assert(noexcept(carven::runtime::eprintln()));
