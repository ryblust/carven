#include <carven/runtime/report.hpp>

#include <type_traits>

static_assert(std::is_nothrow_invocable_v<
              decltype(&carven::runtime::assertion_failed),
              std::string_view,
              std::uint32_t,
              std::uint32_t,
              std::string_view,
              std::optional<std::string_view>,
              std::optional<std::string_view>,
              std::string_view>);
