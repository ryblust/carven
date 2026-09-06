module carven:backend.generation.plan.representation.impl;

import :backend.generation.plan;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto failure_order_key(const SemIRProgram& semantic, TypeID id) noexcept
    -> std::tuple<std::string, std::string, std::uint8_t> {
    const auto& declarations = semantic.declarations();
    const auto provenance = semantic.provenance();
    return std::visit(
        Overloaded {
            [&](const StructTypeValue& value) noexcept {
                const auto& declaration = declarations.structure(value.structure);
                const auto& module_decl = declarations.module_decl(declaration.module_id);
                return std::tuple {
                    std::string(provenance.module_record(module_decl.provenance_module).path.value()),
                    std::string(provenance.spelling(declaration.name)),
                    std::uint8_t {0},
                };
            },
            [&](const EnumTypeValue& value) noexcept {
                const auto& declaration = declarations.enumeration(value.enumeration);
                const auto& module_decl = declarations.module_decl(declaration.module_id);
                return std::tuple {
                    std::string(provenance.module_record(module_decl.provenance_module).path.value()),
                    std::string(provenance.spelling(declaration.name)),
                    std::uint8_t {1},
                };
            },
            []<typename Value>(const Value&) noexcept
                -> std::tuple<std::string, std::string, std::uint8_t> {
                static_assert(
                    std::same_as<Value, BuiltinTypeValue>
                        || std::same_as<Value, ArrayTypeValue>
                        || std::same_as<Value, FunctionTypeValue>
                        || std::same_as<Value, ClosureTypeValue>
                        || std::same_as<Value, CallableViewTypeValue>
                        || std::same_as<Value, CppTypeValue>,
                    "unhandled canonical failure type"
                );
                invariant_violation("failure set contains a non-nominal type");
            },
        },
        semantic.types().type(id).value
    );
}

} // namespace

auto plan_failure_abi(const SemIRProgram& semantic) noexcept -> FailureABI {
    auto mapping = std::vector<std::vector<TypeID>>(semantic.failure_sets().size());
    for (const auto entry : semantic.failure_sets().entries()) {
        auto members = entry.value.members;
        std::ranges::sort(members, [&](TypeID left, TypeID right) noexcept {
            return failure_order_key(semantic, left) < failure_order_key(semantic, right);
        });
        for (const auto [previous, current] : members | std::views::adjacent<2>) {
            if (failure_order_key(semantic, previous) == failure_order_key(semantic, current)) {
                invariant_violation("distinct failure nominals have the same target identity");
            }
        }
        mapping[entry.id.index()] = std::move(members);
    }
    return FailureABI(semantic.identity(), std::move(mapping));
}
