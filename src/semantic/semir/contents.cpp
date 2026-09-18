module carven:semantic.semir.contents.impl;

import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

constexpr auto closure_content = 1u;
constexpr auto callable_content = 2u;
constexpr auto storage_content = 4u;
constexpr auto all_contents = closure_content | callable_content | storage_content;

struct ContentDependent final {
    std::size_t index;
    unsigned mask;
};

template<typename Types, typename Declarations>
auto solve_type_contents(
    const Types& types,
    const Declarations& declarations,
    std::span<const TypeID> roots
) noexcept -> std::vector<TypeContents> {
    if (types.owner() != declarations.owner()) {
        invariant_violation("type contents inputs belong to different programs");
    }
    auto indices = std::map<TypeID, std::size_t>();
    auto reachable = std::vector<TypeID>();
    auto dependents = std::vector<std::vector<ContentDependent>>();
    auto contents = std::vector<unsigned>();
    const auto intern = [&](TypeID type) noexcept {
        const auto [entry, inserted] = indices.try_emplace(type, reachable.size());
        if (inserted) {
            reachable.push_back(type);
            dependents.emplace_back();
            contents.push_back(0u);
        }
        return entry->second;
    };
    auto root_indices = std::vector<std::size_t>();
    for (const auto type : roots) {
        root_indices.push_back(intern(type));
    }
    for (auto index = 0uz; index < reachable.size(); ++index) {
        const auto depend = [&](ConstructionTypeRef type, unsigned mask) noexcept {
            const auto* concrete = std::get_if<TypeID>(&type);
            if (concrete == nullptr) {
                invariant_violation("type contents query requires completed declaration fields");
            }
            const auto child = intern(*concrete);
            dependents[child].push_back({.index = index, .mask = mask});
        };
        const auto& canonical = [&]() noexcept -> decltype(auto) {
            if constexpr (std::same_as<Types, CanonicalTypeStore>) {
                return types.type(reachable[index]);
            } else {
                return types.copy(reachable[index]);
            }
        }();
        canonical.value.visit(
            Overloaded {
                [&](const ClosureTypeValue&) noexcept { contents[index] = closure_content; },
                [&](const CallableViewTypeValue&) noexcept { contents[index] = callable_content; },
                [&](const ArrayTypeValue& value) noexcept {
                    contents[index] = storage_content;
                    depend(value.element, all_contents);
                },
                [&](const StructTypeValue& value) noexcept {
                    const auto& declaration = declarations.structure(value.structure);
                    for (const auto& field : declaration.fields) {
                        depend(field.type, all_contents);
                    }
                },
                [&](const EnumTypeValue& value) noexcept {
                    const auto& declaration = declarations.enumeration(value.enumeration);
                    for (const auto case_id : declaration.cases) {
                        const auto& member = declarations.enum_case(case_id);
                        for (const auto element : member.payload_types) {
                            depend(element, all_contents);
                        }
                    }
                },
                [&](const SliceTypeValue& value) noexcept {
                    depend(value.element, callable_content);
                },
                [&](const BuiltinTypeValue& value) noexcept {
                    if (value.kind == BuiltinType::String) {
                        contents[index] = storage_content;
                    }
                },
                [&](const CppTypeValue& value) noexcept {
                    if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                        for (const auto argument : named->arguments) {
                            depend(argument, callable_content);
                        }
                    }
                },
                []<typename Value>(const Value&) static noexcept {
                    static_assert(
                        std::same_as<Value, PointerTypeValue>
                        || std::same_as<Value, RangeTypeValue>
                        || std::same_as<Value, FunctionTypeValue>
                    );
                },
            }
        );
    }
    auto pending = std::deque<std::size_t>();
    auto queued = std::vector<bool>(contents.size());
    for (auto index = 0uz; index < contents.size(); ++index) {
        if (contents[index] != 0u) {
            pending.push_back(index);
            queued[index] = true;
        }
    }
    while (!pending.empty()) {
        const auto index = pending.front();
        pending.pop_front();
        queued[index] = false;
        for (const auto& dependent : dependents[index]) {
            const auto merged = contents[dependent.index] | (contents[index] & dependent.mask);
            if (merged != contents[dependent.index]) {
                contents[dependent.index] = merged;
                if (!queued[dependent.index]) {
                    pending.push_back(dependent.index);
                    queued[dependent.index] = true;
                }
            }
        }
    }
    auto result = std::vector<TypeContents>();
    result.reserve(roots.size());
    for (const auto index : root_indices) {
        result.push_back({
            .closure_owner = (contents[index] & closure_content) != 0u,
            .callable_view = (contents[index] & callable_content) != 0u,
            .storage_owner = (contents[index] & storage_content) != 0u,
        });
    }
    return result;
}

} // namespace

auto compute_type_contents(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations
) noexcept -> std::vector<TypeContents> {
    auto roots = std::vector<TypeID>();
    roots.reserve(types.size());
    for (const auto [id, type] : types.entries()) {
        static_cast<void>(type);
        roots.push_back(id);
    }
    return solve_type_contents(types, declarations, roots);
}

auto TypeContents::read_borrows_storage() const noexcept -> bool {
    return storage_owner || closure_owner;
}

auto query_type_contents(
    const CanonicalTypeStoreBuilder& types,
    DeclarationConstructionView declarations,
    TypeID type
) noexcept -> TypeContents {
    return solve_type_contents(types, declarations, std::array {type}).front();
}
