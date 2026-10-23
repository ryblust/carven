module carven:semantic.semir.contents.impl;

import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.type;
import :semantic.semir.values;
import :support.invariant;
import std;

namespace {

template<typename Types, typename Declarations>
auto solve_type_contents(
    const Types& types,
    const Declarations& declarations,
    std::span<const ConstructionTypeRef> roots
) noexcept -> std::vector<std::pair<ConstructionTypeRef, TypeContents>> {
    if (types.owner() != declarations.owner()) {
        invariant_violation("type contents inputs belong to different programs");
    }
    const auto members = [&](const auto& shape) noexcept -> std::vector<ConstructionTypeRef> {
        auto result = std::vector<ConstructionTypeRef>();
        using Shape = std::remove_cvref_t<decltype(shape)>;
        if constexpr (std::same_as<Shape, StructTypeValue>) {
            for (const auto& field : declarations.structure(shape.structure).fields) {
                result.push_back(field.type);
            }
        } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
            for (const auto id : declarations.enumeration(shape.enumeration).cases) {
                for (const auto field : declarations.enum_case(id).payload_types) {
                    result.push_back(field);
                }
            }
        }
        return result;
    };
    return collect_type_contents(roots, [&](ConstructionTypeRef reference, auto depend) noexcept {
        const auto* concrete = std::get_if<TypeID>(&reference);
        if (!concrete) {
            invariant_violation("type contents query requires completed declaration fields");
        }
        const auto& canonical = [&]() noexcept -> decltype(auto) {
            if constexpr (std::same_as<Types, CanonicalTypeStore>) {
                return types.type(*concrete);
            } else {
                return types.copy(*concrete);
            }
        }();
        return canonical.value.visit([&](const auto& shape) noexcept {
            return type_content_mask(shape, members, depend);
        });
    });
}

} // namespace

auto compute_type_contents(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations
) noexcept -> std::vector<TypeContents> {
    auto roots = std::vector<ConstructionTypeRef>();
    roots.reserve(types.size());
    for (const auto [id, type] : types.entries()) {
        static_cast<void>(type);
        roots.push_back(id);
    }
    auto result = std::vector<TypeContents>();
    for (const auto& [type, contents] : solve_type_contents(types, declarations, roots)) {
        static_cast<void>(type);
        result.push_back(contents);
    }
    return result;
}

auto TypeContents::contains_carven_owner() const noexcept -> bool {
    return contains_operation_owner || contains_storage_owner || contains_closure_owner;
}

auto TypeContents::read_borrows_storage() const noexcept -> bool {
    return contains_parametric_value || contains_carven_owner() || contains_native_value;
}

auto TypeContents::read_is_value_snapshot() const noexcept -> bool {
    return !read_borrows_storage();
}

auto TypeContents::can_store_inline() const noexcept -> bool {
    return !has_value_storage_constraint;
}

auto query_type_contents(
    const CanonicalTypeStoreBuilder& types,
    DeclarationConstructionView declarations,
    TypeID type
) noexcept -> TypeContents {
    return solve_type_contents(types, declarations, std::array {ConstructionTypeRef(type)})
        .front()
        .second;
}

auto query_type_contents(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> TypeContents {
    return collect_type_contents(
               std::array {type},
               [&](ConstructionTypeRef reference, auto depend) noexcept {
                   const auto members =
                       [&](const auto& shape) noexcept -> std::vector<ConstructionTypeRef> {
                       auto result = std::vector<ConstructionTypeRef>();
                       using Shape = std::remove_cvref_t<decltype(shape)>;
                       if constexpr (std::same_as<Shape, StructTypeValue>) {
                           const auto fields = values.struct_field_types(shape.structure);
                           if (!fields) {
                               invariant_violation(
                                   "execution contents require completed declaration fields"
                               );
                           }
                           result.assign(fields->begin(), fields->end());
                       } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
                           const auto cases = values.enum_case_types(shape.enumeration);
                           if (!cases) {
                               invariant_violation(
                                   "execution contents require completed declaration fields"
                               );
                           }
                           for (const auto& item : *cases) {
                               result.insert(
                                   result.end(),
                                   item.payload_types.begin(),
                                   item.payload_types.end()
                               );
                           }
                       } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
                           const auto fields =
                               values.generic_member_types(std::get<GenericTypeID>(reference));
                           if (!fields) {
                               invariant_violation(
                                   "execution contents require checked generic members"
                               );
                           }
                           result = *fields;
                       }
                       return result;
                   };
                   return visit_execution_type(values, reference, [&](const auto& shape) noexcept {
                       return type_content_mask(shape, members, depend);
                   });
               }
    )
        .front()
        .second;
}

auto solve_type_content_graph(
    std::vector<unsigned> contents,
    std::span<const std::vector<TypeContentDependent>> dependents
) noexcept -> std::vector<TypeContents> {
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
    result.reserve(contents.size());
    for (auto index = 0uz; index < contents.size(); ++index) {
        result.push_back({
            .contains_operation_owner = (contents[index] & operation_owner_content) != 0u,
            .contains_closure_owner = (contents[index] & closure_owner_content) != 0u,
            .contains_callable_view = (contents[index] & callable_view_content) != 0u,
            .contains_storage_owner = (contents[index] & storage_owner_content) != 0u,
            .contains_storage_view = (contents[index] & storage_view_content) != 0u,
            .contains_native_value = (contents[index] & native_value_content) != 0u,
            .contains_parametric_value = (contents[index] & parametric_value_content) != 0u,
            .has_value_storage_constraint = (contents[index] & value_storage_constraint) != 0u,
        });
    }
    return result;
}

auto TypeContents::mask() const noexcept -> unsigned {
    return (contains_operation_owner ? operation_owner_content : 0u)
        | (contains_closure_owner ? closure_owner_content : 0u)
        | (contains_callable_view ? callable_view_content : 0u)
        | (contains_storage_owner ? storage_owner_content : 0u)
        | (contains_storage_view ? storage_view_content : 0u)
        | (contains_native_value ? native_value_content : 0u)
        | (contains_parametric_value ? parametric_value_content : 0u)
        | (has_value_storage_constraint ? value_storage_constraint : 0u);
}
