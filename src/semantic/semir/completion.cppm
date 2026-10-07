module carven:semantic.semir.completion;

import :semantic.semir.body;
import :semantic.semir.structured;
import std;

// The ways control can leave a construct. The set follows from the structure
// of the tree; the value of a condition never narrows it.
enum class Exit : std::uint8_t {
    Normal = 1u,
    Return = 2u,
    Break = 4u,
    Continue = 8u,
    Failure = 16u,
    Stop = 32u,
    Cancelled = 64u,
};

class ExitSet final {
public:
    constexpr ExitSet() noexcept = default;

    constexpr ExitSet(Exit exit) noexcept
        : bits(static_cast<std::uint8_t>(exit)) {}

    constexpr auto contains(Exit exit) const noexcept -> bool {
        return (bits & static_cast<std::uint8_t>(exit)) != 0u;
    }

    constexpr auto operator|(ExitSet other) const noexcept -> ExitSet {
        return ExitSet(static_cast<std::uint8_t>(bits | other.bits));
    }

    constexpr auto operator&(ExitSet other) const noexcept -> ExitSet {
        return ExitSet(static_cast<std::uint8_t>(bits & other.bits));
    }

    constexpr auto without(Exit exit) const noexcept -> ExitSet {
        return ExitSet(static_cast<std::uint8_t>(bits & ~static_cast<std::uint8_t>(exit)));
    }

    // Sequencing: the next construct is entered only through normal completion.
    constexpr auto then(ExitSet next) const noexcept -> ExitSet {
        return contains(Exit::Normal) ? without(Exit::Normal) | next : *this;
    }

    constexpr auto operator==(const ExitSet&) const noexcept -> bool = default;

private:
    constexpr explicit ExitSet(std::uint8_t bits) noexcept
        : bits(bits) {}

    std::uint8_t bits = 0u;
};

// Pattern identities belong to the body, rather than its expression tree.
// These synchronous queries borrow the owner while completion is calculated.
// The query owns its closures, including closures returned by reader factories.
struct CompletionPatterns final {
    std::function<std::variant<PatternValue, ElaboratedPatternValue>(PatternID)> read;
    std::function<bool(EnumCaseID)> single_case;
};

struct PatternCompletion final {
    bool accepted;
    bool rejected;
};

// One operation can query each completed pattern as it is added. Supply its
// dynamic bounds on the first query; later queries reuse completed values.
// The query retains no addresses into the growing bound tree.
class CompletionQuery final {
public:
    explicit CompletionQuery(
        CompletionPatterns patterns,
        std::span<const SemPatternBounds> bounds = {}
    ) noexcept;
    ~CompletionQuery() noexcept;
    CompletionQuery(const CompletionQuery&) = delete;
    auto operator=(const CompletionQuery&) -> CompletionQuery& = delete;

    auto pattern(PatternID pattern, std::span<const SemPatternBounds> bounds = {}) noexcept
        -> PatternCompletion;
    auto enter_try(const SemanticRegion& body) noexcept -> void;
    auto catch_entry(std::optional<ConstructionTypeRef> type) noexcept -> bool;
    auto consume_catch(
        std::optional<ConstructionTypeRef> type,
        std::optional<PatternID> pattern
    ) noexcept -> void;
    auto catch_accepted() const noexcept -> bool;
    auto finish_catch(bool guard_may_reject) noexcept -> void;
    auto pending_failures() const noexcept -> std::optional<std::vector<ConstructionTypeRef>>;

private:
    struct State;
    std::unique_ptr<State> state;
};

auto exits(const SemanticExpression& expression, const CompletionPatterns& patterns = {}) noexcept
    -> ExitSet;
auto exits(const SemanticStatement& statement, const CompletionPatterns& patterns = {}) noexcept
    -> ExitSet;
auto exits(const SemanticRegion& region, const CompletionPatterns& patterns = {}) noexcept
    -> ExitSet;
