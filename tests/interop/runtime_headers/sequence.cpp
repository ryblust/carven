#include <carven/runtime/sequence.hpp>

#include <type_traits>
#include <utility>

namespace {

struct NonDefault final {
    explicit NonDefault(int number) noexcept
        : number(number) {}

    int number;
};

struct Node final {
    carven::runtime::Sequence<Node> children;
};

constexpr auto site = carven::runtime::SourceSite::native();

static_assert(std::is_same_v<
              decltype(carven::runtime::checked_sequence_index(
                  std::declval<carven::runtime::Sequence<bool>&>(),
                  0,
                  site
              )),
              bool&>);
static_assert(std::is_same_v<
              decltype(carven::runtime::checked_sequence_index(
                  std::declval<const carven::runtime::Sequence<bool>&>(),
                  0,
                  site
              )),
              const bool&>);

} // namespace

auto sequence_header_contract() noexcept -> bool {
    auto values = carven::runtime::Sequence<NonDefault>();
    values.push(NonDefault(7));
    auto copy = values;
    carven::runtime::checked_sequence_index(copy, 0, site).number = 9;
    auto nodes = carven::runtime::Sequence<Node>();
    nodes.push(Node {});
    nodes.remove(0, site);
    return carven::runtime::checked_sequence_index(values, 0, site).number == 7
        && carven::runtime::checked_sequence_index(copy, 0, site).number == 9
        && nodes.empty();
}
