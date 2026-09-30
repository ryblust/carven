#include <carven/runtime/trap.hpp>

static_assert(
    noexcept(carven::runtime::trap(std::string_view(), carven::runtime::SourceSite::native()))
);
