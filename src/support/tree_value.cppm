module carven:support.tree_value;

import std;

// Child value destructors bound ordinary variant assignment/emplace/swap too.
// The cleanup policy must accept partially moved owning edges.
template<typename Cleanup, typename... Alternatives>
class TreeValue final : public std::variant<Alternatives...> {
public:
    using Base = std::variant<Alternatives...>;
    using Base::Base;
    using Base::operator=;
    TreeValue(const TreeValue&) = delete;
    TreeValue(TreeValue&&) noexcept = default;
    auto operator=(const TreeValue&) -> TreeValue& = delete;
    auto operator=(TreeValue&&) noexcept -> TreeValue& = default;

    ~TreeValue() noexcept { Cleanup::clear(*this); }
};
