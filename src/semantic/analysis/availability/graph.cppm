module carven:semantic.analysis.availability.graph;

import :semantic.analysis.availability.place;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :source.provenance;
import std;

struct AvailabilityBlockID final {
    std::uint32_t value;
    constexpr auto operator==(const AvailabilityBlockID&) const noexcept -> bool = default;
};

struct TakeSite final {
    ProgramOriginID origin;
    std::uint32_t source;
    std::uint32_t offset;
    std::uint32_t expression;

    constexpr auto operator==(const TakeSite& other) const noexcept -> bool {
        return source == other.source && offset == other.offset && expression == other.expression;
    }

    constexpr auto operator<=>(const TakeSite& other) const noexcept {
        return std::tuple(source, offset, expression)
            <=> std::tuple(other.source, other.offset, other.expression);
    }
};

struct AvailabilityUse final {
    HIRExprID expression;
    AvailabilityPlaceID place;
};

struct AvailabilityTake final {
    HIRExprID expression;
    AvailabilityPlaceID place;
    TakeSite site;
};

struct AvailabilityRestore final {
    AvailabilityPlaceID place;
    bool requires_write;
};

struct AvailabilityCapture final {
    HIRExprID expression;
    AvailabilityPlaceID source;
    ProgramOriginID origin;
    bool write;
};

struct AvailabilityAccessCheck final {
    HIRExprID primary;
    std::optional<HIRExprID> related;
    std::vector<AvailabilityPlaceID> reads;
    std::vector<AvailabilityPlaceID> writes;
    std::vector<AvailabilityPlaceID> takes;
};

enum class InvalidTakeKind {
    Partial,
    NonOwner,
};

struct AvailabilityInvalidTake final {
    ProgramOriginID origin;
    InvalidTakeKind kind;
};

using AvailabilityOperation = std::variant<
    AvailabilityUse,
    AvailabilityTake,
    AvailabilityRestore,
    AvailabilityCapture,
    AvailabilityAccessCheck,
    AvailabilityInvalidTake>;

struct AvailabilityBlock final {
    std::uint32_t operation_begin;
    std::uint32_t operation_count;
    std::uint32_t successor_begin;
    std::uint32_t successor_count;
};

struct BodyAvailabilityGraph final {
    AvailabilityBlockID entry;
    std::vector<AvailabilityBlock> blocks;
    std::vector<AvailabilityOperation> operations;
    std::vector<AvailabilityBlockID> successors;
};

struct MutableAvailabilityBlock final {
    std::vector<AvailabilityOperation> operations;
    std::vector<AvailabilityBlockID> successors;
};

class MutableAvailabilityGraph final {
public:
    auto append(
        std::vector<AvailabilityOperation> operations = {},
        std::vector<AvailabilityBlockID> successors = {}
    ) noexcept -> AvailabilityBlockID;

    auto set_successors(
        AvailabilityBlockID id,
        std::vector<AvailabilityBlockID> successors
    ) noexcept -> void;

    auto freeze(AvailabilityBlockID entry) && noexcept -> BodyAvailabilityGraph;

private:
    static auto normalize_successors(std::vector<AvailabilityBlockID>& successors) noexcept -> void;

    std::vector<MutableAvailabilityBlock> blocks;
};
