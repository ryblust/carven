module carven:support.tree_value;

import std;

// Child value destructors bound ordinary variant assignment/emplace/swap too.
// The cleanup policy must accept partially moved owning edges. A policy that
// declares `copyable` supplies its own bounded tree copy.
template<typename Cleanup, typename... Alternatives>
class TreeValue final : public std::variant<Alternatives...> {
public:
    using Base = std::variant<Alternatives...>;
    using Base::Base;
    using Base::operator=;

    TreeValue(const TreeValue& other) noexcept
        requires Cleanup::copyable
        : Base(static_cast<Base&&>(Cleanup::copy(other))) {}

    TreeValue(TreeValue&&) noexcept = default;

    auto operator=(const TreeValue& other) noexcept -> TreeValue&
        requires Cleanup::copyable
    {
        if (this != &other) {
            Base::operator=(static_cast<Base&&>(Cleanup::copy(other)));
        }
        return *this;
    }

    auto operator=(TreeValue&&) noexcept -> TreeValue& = default;

    ~TreeValue() noexcept { Cleanup::clear(*this); }
};
