module carven:semantic.analysis.availability.solve.impl;

import :semantic.analysis.availability.graph;
import :semantic.analysis.availability.place;
import :semantic.analysis.availability.solve;
import :semantic.analysis.session.read;
import :semantic.hir.place;
import :support.invariant;
import :support.visit;
import std;

namespace {

class UnavailableBits final {
public:
    explicit UnavailableBits(std::size_t places = 0) noexcept
        : many(places > 64 ? (places + 63) / 64 : 0, 0) {}

    auto contains(AvailabilityPlaceID place) const noexcept -> bool {
        const auto mask = 1ull << (place.value % 64);
        return many.empty() ? (one & mask) != 0 : (many[place.value / 64] & mask) != 0;
    }

    auto insert(AvailabilityPlaceID place) noexcept -> bool {
        const auto mask = 1ull << (place.value % 64);
        auto& word = many.empty() ? one : many[place.value / 64];
        const auto changed = (word & mask) == 0;
        word |= mask;
        return changed;
    }

    auto erase(AvailabilityPlaceID place) noexcept -> bool {
        const auto mask = 1ull << (place.value % 64);
        auto& word = many.empty() ? one : many[place.value / 64];
        const auto changed = (word & mask) != 0;
        word &= ~mask;
        return changed;
    }

    auto join(const UnavailableBits& source) noexcept -> bool {
        if (many.size() != source.many.size()) {
            invariant_violation("availability bitsets have incompatible storage widths");
        }
        auto changed = false;
        if (many.empty()) {
            const auto previous = one;
            one |= source.one;
            return one != previous;
        }
        for (auto&& [destination, incoming] : std::views::zip(many, source.many)) {
            const auto previous = destination;
            destination |= incoming;
            changed |= destination != previous;
        }
        return changed;
    }

private:
    std::uint64_t one = 0;
    std::vector<std::uint64_t> many;
};

struct AvailabilityWitness final {
    AvailabilityPlaceID place;
    TakeSite site;
    constexpr auto operator==(const AvailabilityWitness&) const noexcept -> bool = default;
};

class AvailabilityState final {
public:
    explicit AvailabilityState(std::size_t place_count = 0) noexcept
        : unavailable(place_count) {}

    auto contains(AvailabilityPlaceID place) const noexcept -> bool {
        return unavailable.contains(place);
    }

    auto witness(AvailabilityPlaceID place) const noexcept -> TakeSite {
        const auto found = std::ranges::lower_bound(
            witnesses,
            place.value,
            {},
            [](const AvailabilityWitness& value) static noexcept { return value.place.value; }
        );
        if (found == witnesses.end() || found->place != place) {
            invariant_violation("unavailable place has no provenance witness");
        }
        return found->site;
    }

    auto take(AvailabilityPlaceID place, TakeSite site) noexcept -> bool {
        if (!unavailable.insert(place)) {
            return false;
        }
        const auto found = std::ranges::lower_bound(
            witnesses,
            place.value,
            {},
            [](const AvailabilityWitness& value) static noexcept { return value.place.value; }
        );
        witnesses.insert(found, {.place = place, .site = site});
        return true;
    }

    auto restore(AvailabilityPlaceID place) noexcept -> void {
        if (!unavailable.erase(place)) {
            return;
        }
        const auto found = std::ranges::lower_bound(
            witnesses,
            place.value,
            {},
            [](const AvailabilityWitness& value) static noexcept { return value.place.value; }
        );
        if (found == witnesses.end() || found->place != place) {
            invariant_violation("restored unavailable place has no provenance witness");
        }
        witnesses.erase(found);
    }

    auto join(const AvailabilityState& source) noexcept -> bool {
        auto changed = unavailable.join(source.unavailable);
        auto merged = std::vector<AvailabilityWitness>();
        merged.reserve(witnesses.size() + source.witnesses.size());
        auto left = 0uz;
        auto right = 0uz;
        while (left < witnesses.size() && right < source.witnesses.size()) {
            const auto left_place = witnesses[left].place.value;
            const auto right_place = source.witnesses[right].place.value;
            if (left_place < right_place) {
                merged.push_back(witnesses[left++]);
            } else if (right_place < left_place) {
                merged.push_back(source.witnesses[right++]);
            } else {
                merged.push_back(
                    witnesses[left].site <= source.witnesses[right].site ? witnesses[left]
                                                                         : source.witnesses[right]
                );
                ++left;
                ++right;
            }
        }
        merged.insert(
            merged.end(),
            witnesses.begin() + static_cast<std::ptrdiff_t>(left),
            witnesses.end()
        );
        merged.insert(
            merged.end(),
            source.witnesses.begin() + static_cast<std::ptrdiff_t>(right),
            source.witnesses.end()
        );
        changed |= merged != witnesses;
        witnesses = std::move(merged);
        return changed;
    }

private:
    UnavailableBits unavailable;
    std::vector<AvailabilityWitness> witnesses;
};

class BodyAvailabilitySolver final {
public:
    BodyAvailabilitySolver(
        SemanticDraftView hir,
        const AvailabilityPlaceCatalog& catalog,
        BodyID body,
        const BodyAvailabilityGraph& graph
    ) noexcept
        : hir(hir),
          catalog(catalog),
          body(body),
          graph(graph) {}

    auto run() noexcept -> BodyAvailabilityFindings {
        auto entries = std::vector<std::optional<AvailabilityState>>(graph.blocks.size());
        auto queued = std::vector<std::uint8_t>(graph.blocks.size(), 0);
        auto worklist = std::deque<AvailabilityBlockID>();
        entries[graph.entry.value] = AvailabilityState(catalog.count(body));
        queued[graph.entry.value] = 1;
        worklist.push_back(graph.entry);
        while (!worklist.empty()) {
            const auto block_id = worklist.front();
            worklist.pop_front();
            queued[block_id.value] = 0;
            auto state = *entries[block_id.value];
            const auto& block = graph.blocks[block_id.value];
            for (auto offset = 0u; offset < block.operation_count; ++offset) {
                execute(graph.operations[block.operation_begin + offset], state);
            }
            for (auto offset = 0u; offset < block.successor_count; ++offset) {
                const auto successor = graph.successors[block.successor_begin + offset];
                auto changed = false;
                if (!entries[successor.value].has_value()) {
                    entries[successor.value] = state;
                    changed = true;
                } else {
                    changed = entries[successor.value]->join(state);
                }
                if (changed && queued[successor.value] == 0) {
                    queued[successor.value] = 1;
                    worklist.push_back(successor);
                }
            }
        }
        return findings();
    }

private:
    auto remember_unavailable(
        HIRExprID expression,
        AvailabilityPlaceID place,
        TakeSite site
    ) noexcept -> void {
        const auto key = std::pair(expression, place.value);
        const auto found = unavailable.find(key);
        if (found == unavailable.end()) {
            unavailable.emplace(key, site);
        } else if (site < found->second) {
            found->second = site;
        }
    }

    auto execute(const AvailabilityOperation& operation, AvailabilityState& state) noexcept
        -> void {
        std::visit(
            Overloaded {
                [&](const AvailabilityUse& value) noexcept {
                    if (state.contains(value.place)) {
                        remember_unavailable(
                            value.expression,
                            value.place,
                            state.witness(value.place)
                        );
                    }
                },
                [&](const AvailabilityTake& value) noexcept {
                    const auto found = reached_takes.find(value.place.value);
                    if (found == reached_takes.end()) {
                        reached_takes.emplace(value.place.value, value.site);
                    } else if (value.site < found->second) {
                        found->second = value.site;
                    }
                    if (state.contains(value.place)) {
                        remember_unavailable(
                            value.expression,
                            value.place,
                            state.witness(value.place)
                        );
                    } else {
                        static_cast<void>(state.take(value.place, value.site));
                    }
                },
                [&](const AvailabilityRestore& value) noexcept {
                    const auto symbol = catalog.global(body, value.place);
                    const auto& binding = hir.binding(symbol);
                    if (!binding.has_value()) {
                        invariant_violation("availability restore references a non-binding symbol");
                    }
                    if (!value.requires_write || binding->capabilities.write) {
                        state.restore(value.place);
                    }
                },
                [&](const AvailabilityCapture& value) noexcept {
                    if (state.contains(value.source)) {
                        remember_unavailable(
                            value.expression,
                            value.source,
                            state.witness(value.source)
                        );
                    }
                    if (!value.write) {
                        return;
                    }
                    const auto found = reached_write_captures.find(value.source.value);
                    if (found == reached_write_captures.end()) {
                        reached_write_captures.emplace(value.source.value, value.origin);
                    } else if (value.origin.index() < found->second.index()) {
                        found->second = value.origin;
                    }
                },
                [&](const AvailabilityAccessCheck& value) noexcept {
                    for (const auto place : value.takes) {
                        if (!std::ranges::contains(value.reads, place)
                            && !std::ranges::contains(value.writes, place)) {
                            continue;
                        }
                        operation_conflicts.try_emplace(
                            {value.primary, place.value},
                            value.related
                        );
                    }
                },
                [&](const AvailabilityInvalidTake& value) noexcept {
                    invalid_takes.insert({value.origin, value.kind});
                },
            },
            operation
        );
    }

    auto findings() const noexcept -> BodyAvailabilityFindings {
        auto result = BodyAvailabilityFindings {};
        result.unavailable_uses.reserve(unavailable.size());
        for (const auto& [key, site] : unavailable) {
            result.unavailable_uses.push_back({
                .expression = key.first,
                .site = site,
            });
        }
        result.operation_conflicts.reserve(operation_conflicts.size());
        for (const auto& [key, related] : operation_conflicts) {
            result.operation_conflicts.push_back({
                .primary = key.first,
                .related = related,
            });
        }
        result.invalid_takes.reserve(invalid_takes.size());
        for (const auto& [origin, kind] : invalid_takes) {
            result.invalid_takes.push_back({
                .origin = origin,
                .kind = kind,
            });
        }
        for (const auto& [place, capture_origin] : reached_write_captures) {
            const auto taken = reached_takes.find(place);
            if (taken != reached_takes.end()) {
                result.capture_conflicts.push_back({
                    .take_site = taken->second,
                    .capture_origin = capture_origin,
                });
            }
        }
        return result;
    }

    SemanticDraftView hir;
    const AvailabilityPlaceCatalog& catalog;
    BodyID body;
    const BodyAvailabilityGraph& graph;
    std::flat_map<std::pair<HIRExprID, std::uint32_t>, TakeSite> unavailable;
    std::flat_map<std::pair<HIRExprID, std::uint32_t>, std::optional<HIRExprID>>
        operation_conflicts;
    std::flat_set<std::pair<ProgramOriginID, InvalidTakeKind>> invalid_takes;
    std::flat_map<std::uint32_t, TakeSite> reached_takes;
    std::flat_map<std::uint32_t, ProgramOriginID> reached_write_captures;
};


} // namespace

auto solve_body_availability(
    SemanticDraftView hir,
    const AvailabilityPlaceCatalog& catalog,
    BodyID body,
    const BodyAvailabilityGraph& graph
) noexcept -> BodyAvailabilityFindings {
    return BodyAvailabilitySolver(hir, catalog, body, graph).run();
}
