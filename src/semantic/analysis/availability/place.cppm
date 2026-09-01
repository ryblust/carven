module carven:semantic.analysis.availability.place;

import :semantic.analysis.session.read;
import :semantic.hir.ids;
import std;

struct AvailabilityPlaceID final {
    std::uint32_t value;
    constexpr auto operator==(const AvailabilityPlaceID&) const noexcept -> bool = default;
};
struct AvailabilityPlaceRef final {
    BodyID body;
    AvailabilityPlaceID local;
};

class AvailabilityPlaceCatalog final {
public:
    explicit AvailabilityPlaceCatalog(SemanticDraftView hir) noexcept;

    auto local(BodyID body, SymbolID symbol) const noexcept -> AvailabilityPlaceID;
    auto global(BodyID body, AvailabilityPlaceID place) const noexcept -> SymbolID;
    auto count(BodyID body) const noexcept -> std::size_t;

private:
    std::vector<std::optional<AvailabilityPlaceRef>> place_refs;
    std::vector<std::vector<SymbolID>> body_places;
};
