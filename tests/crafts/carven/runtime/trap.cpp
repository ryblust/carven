#include <carven/runtime/diagnostics/trap.hpp>

static_assert(
    noexcept(carven::runtime::trap(std::string_view(), carven::runtime::SourceSite::native()))
);
