module carven:semantic.analysis.validation.structure.impl;
import :semantic.analysis.validation.context;
import std;

auto BodyContractVerifier::verify_rows() noexcept -> void {
    verify_body_inputs();
    auto inputs = std::vector<std::uint8_t>(body.bindings().size(), 0u);
    for (const auto id : body.inputs().parameters) {
        const auto& binding = body.binding(id);
        if (!std::holds_alternative<ParameterBindingStorage>(binding.storage)
            || inputs[id.index()] != 0u) {
            invariant_violation("BodyInputs parameter is duplicate or has the wrong role");
        }
        inputs[id.index()] = 1u;
    }
    for (const auto id : body.inputs().captures) {
        const auto& binding = body.binding(id);
        if (!std::holds_alternative<CaptureBindingStorage>(binding.storage)
            || inputs[id.index()] != 0u) {
            invariant_violation("BodyInputs capture is duplicate or has the wrong role");
        }
        inputs[id.index()] = 1u;
    }
    for (const auto [id, binding] : body.bindings()) {
        if (id.owner() != body.identity()
            || binding.name.owner() != body.provenance_identity()
            || !body.lifetime_regions().contains(binding.lifetime)) {
            invariant_violation("local binding contains a foreign owner");
        }
        static_cast<void>(require_type(binding.type));
        require_origin(binding.origin);
        if ((std::holds_alternative<ParameterBindingStorage>(binding.storage)
             || std::holds_alternative<CaptureBindingStorage>(binding.storage))
            && inputs[id.index()] == 0u) {
            invariant_violation("parameter or capture binding is absent from BodyInputs");
        }
    }
    verify_patterns();
}

auto BodyContractVerifier::verify_patterns() const noexcept -> void {
    for (const auto [id, pattern] : body.patterns()) {
        if (id.owner() != body.identity()) {
            invariant_violation("pattern table contains a foreign identity");
        }
        static_cast<void>(require_type(pattern.type));
        require_origin(pattern.origin);
        std::visit(
            Overloaded {
                [](const WildcardPattern&) static noexcept {},
                [&](const LiteralPattern& value) noexcept {
                    if (program.constants().constant(value.constant).type != pattern.type) {
                        invariant_violation("literal pattern type differs from its constant");
                    }
                },
                [&](const OrPattern& value) noexcept {
                    if (value.alternatives.empty()) {
                        invariant_violation("or-pattern has no alternatives");
                    }
                    for (const auto child : value.alternatives) {
                        if (child.index() >= id.index()) {
                            invariant_violation(
                                "pattern alternatives contain a forward edge or cycle"
                            );
                        }
                        if (body.pattern(child).type != pattern.type) {
                            invariant_violation("or-pattern alternative has another type");
                        }
                    }
                },
                [&](const TypeConstraintPattern& value) noexcept {
                    static_cast<void>(require_type(value.type));
                    if (!compatible_pattern_type(pattern.type, value.type)) {
                        invariant_violation(
                            "type-constraint pattern is incompatible with its subject"
                        );
                    }
                },
                [&](const BindingPattern& value) noexcept {
                    if (body.binding(value.binding).type != pattern.type) {
                        invariant_violation("binding pattern has another type");
                    }
                },
                [&](const EnumCasePattern& value) noexcept {
                    const auto enum_case = require_enum_case(value.enum_case);
                    const auto canonical = require_type(pattern.type);
                    const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value);
                    if (enumeration == nullptr
                        || enumeration->enumeration != enum_case.owner
                        || value.payload.size() != enum_case.payload_types.size()) {
                        invariant_violation("enum pattern differs from its enum-case declaration");
                    }
                    for (auto index = 0uz; index < value.payload.size(); ++index) {
                        if (value.payload[index].index() >= id.index()) {
                            invariant_violation("pattern payload contains a forward edge or cycle");
                        }
                        if (body.pattern(value.payload[index]).type
                            != enum_case.payload_types[index]) {
                            invariant_violation("enum pattern payload has the wrong type");
                        }
                    }
                },
            },
            pattern.value
        );
    }
}

auto BodyContractVerifier::pattern_bindings(PatternID id) const noexcept
    -> std::vector<LocalBindingID> {
    return std::visit(
        Overloaded {
            [](const WildcardPattern&) static noexcept { return std::vector<LocalBindingID>(); },
            [](const LiteralPattern&) static noexcept { return std::vector<LocalBindingID>(); },
            [](const TypeConstraintPattern&) static noexcept {
                return std::vector<LocalBindingID>();
            },
            [](const BindingPattern& value) static noexcept {
                return std::vector<LocalBindingID> {value.binding};
            },
            [&](const EnumCasePattern& value) noexcept {
                auto result = std::vector<LocalBindingID>();
                for (const auto child : value.payload) {
                    auto nested = pattern_bindings(child);
                    result.insert(result.end(), nested.begin(), nested.end());
                }
                std::ranges::sort(result, {}, &LocalBindingID::index);
                if (std::ranges::adjacent_find(result) != result.end()) {
                    invariant_violation("pattern binds one local more than once");
                }
                return result;
            },
            [&](const OrPattern& value) noexcept {
                auto result = pattern_bindings(value.alternatives.front());
                std::ranges::sort(result, {}, &LocalBindingID::index);
                for (const auto alternative : value.alternatives | std::views::drop(1)) {
                    auto nested = pattern_bindings(alternative);
                    std::ranges::sort(nested, {}, &LocalBindingID::index);
                    if (nested != result) {
                        invariant_violation("or-pattern alternatives bind different locals");
                    }
                }
                return result;
            },
        },
        body.pattern(id).value
    );
}

auto BodyContractVerifier::signature_for_callable(CallableID id) const noexcept
    -> CallableSignatureID {
    if (id.owner() != program.identity()) {
        invariant_violation("callable belongs to another semantic program");
    }
    return program.declarations().callable(id).signature;
}

auto BodyContractVerifier::signature_for_type(TypeID type) const noexcept -> CallableSignatureID {
    return std::visit(
        Overloaded {
            [&](const FunctionTypeValue& value) noexcept {
                return signature_for_callable(value.callable);
            },
            [&](const ClosureTypeValue& value) noexcept {
                return signature_for_callable(value.callable);
            },
            [](const CallableViewTypeValue& value) static noexcept { return value.signature; },
            []<typename Value>(const Value&) static noexcept -> CallableSignatureID {
                static_assert(
                    std::same_as<Value, BuiltinTypeValue>
                        || std::same_as<Value, StructTypeValue>
                        || std::same_as<Value, EnumTypeValue>
                        || std::same_as<Value, ArrayTypeValue>
                        || std::same_as<Value, CppTypeValue>
                        || std::same_as<Value, PointerTypeValue>,
                    "unhandled non-callable canonical type"
                );
                invariant_violation("value or place does not have a callable type");
            },
        },
        require_type(type).value
    );
}

auto BodyContractVerifier::compatible_pattern_type(TypeID left, TypeID right) const noexcept
    -> bool {
    auto visited = std::flat_set<std::pair<TypeID, TypeID>>();
    const auto compatible = [&](this const auto& self, TypeID left, TypeID right) noexcept -> bool {
        if (left == right || !visited.emplace(left, right).second) {
            return true;
        }
        const auto& a = program.types().type(left).value;
        const auto& b = program.types().type(right).value;
        if (const auto* array = std::get_if<ArrayTypeValue>(&a)) {
            const auto* other = std::get_if<ArrayTypeValue>(&b);
            return other && array->extent == other->extent && self(array->element, other->element);
        }
        const auto callable = [](const CanonicalTypeValue& type) static noexcept {
            return std::holds_alternative<FunctionTypeValue>(type)
                || std::holds_alternative<ClosureTypeValue>(type)
                || std::holds_alternative<CallableViewTypeValue>(type);
        };
        if (!callable(a)
            || !callable(b)
            || (!std::holds_alternative<CallableViewTypeValue>(a)
                && !std::holds_alternative<CallableViewTypeValue>(b))) {
            return false;
        }
        const auto& first = program.callable_signatures().signature(signature_for_type(left));
        const auto& second = program.callable_signatures().signature(signature_for_type(right));
        return self(first.result, second.result)
            && std::ranges::equal(
                   first.parameters,
                   second.parameters,
                   [&](const CallableParameter& x, const CallableParameter& y) noexcept {
                       return x.access == y.access && self(x.type, y.type);
                   }
            );
    };
    return compatible(left, right);
}
