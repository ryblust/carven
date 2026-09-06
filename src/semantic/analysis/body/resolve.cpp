module carven:semantic.analysis.body.resolve.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.body.resolve;
import :support.unique_indirect;
import :support.visit;
import std;

namespace {

class BodyResolver final {
public:
    explicit BodyResolver(ProgramDraft& draft) noexcept
        : draft(draft) {}

    auto warning(DiagnosticCode code, std::string message, ProgramOriginID origin) const noexcept
        -> void {
        draft.diagnostics().warning(
            DiagnosticBuilder(code, std::move(message)).primary(draft.source_span(origin)).build()
        );
    }

    auto operator()(ConstructionTypeRef type) const noexcept -> TypeID {
        return draft.concrete_type(type);
    }
    auto operator()(FailureTermID failures) const noexcept -> FailureSetID {
        return draft.concrete_failure_set(failures);
    }

    template<typename T>
    auto operator()(std::vector<T>&& values) const noexcept {
        using Result = decltype((*this)(std::declval<T&&>()));
        auto result = std::vector<Result>();
        result.reserve(values.size());
        for (auto& value : values) {
            result.push_back((*this)(std::move(value)));
        }
        return result;
    }
    template<typename T>
    auto operator()(std::optional<T>&& value) const noexcept {
        using Result = decltype((*this)(std::declval<T&&>()));
        return value.has_value() ? std::optional<Result>((*this)(std::move(*value)))
                                 : std::optional<Result>();
    }
    template<typename T>
    auto operator()(UniqueIndirect<T>&& value) const noexcept {
        return UniqueIndirect((*this)(std::move(*value)));
    }

    auto operator()(DraftExpression&& value) const noexcept -> SemIRExpression {
        return {
            .type = (*this)(value.type),
            .lifetime = value.lifetime,
            .origin = value.origin,
            .constant = value.constant,
            .failures = (*this)(value.failures),
            .exits_test = value.exits_test,
            .category = value.category,
            .value = std::visit(
                [&](auto&& operation) noexcept -> decltype(SemIRExpression::value) {
                    return (*this)(std::forward<decltype(operation)>(operation));
                },
                std::move(value.value)
            ),
        };
    }
    auto operator()(DraftStatement&& value) const noexcept -> SemIRStatement {
        return {
            .origin = value.origin,
            .lifetime = value.lifetime,
            .value = std::visit(
                [&](auto&& operation) noexcept -> decltype(SemIRStatement::value) {
                    return (*this)(std::forward<decltype(operation)>(operation));
                },
                std::move(value.value)
            ),
        };
    }
    auto operator()(DraftRegion&& value) const noexcept -> SemIRRegion {
        return {
            .scope = value.scope,
            .lifetime = value.lifetime,
            .origin = value.origin,
            .statements = (*this)(std::move(value.statements)),
            .result = (*this)(std::move(value.result)),
            .failures = (*this)(value.failures),
            .exits_test = value.exits_test,
        };
    }

    auto operator()(SemSequence<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemSequence<TypeID, FailureSetID> {
        return {.expressions = (*this)(std::move(value.expressions))};
    }
    auto operator()(SemArray<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemArray<TypeID, FailureSetID> {
        return {.elements = (*this)(std::move(value.elements))};
    }
    auto operator()(SemArrayAdopt<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemArrayAdopt<TypeID, FailureSetID> {
        return {.source = (*this)(std::move(value.source))};
    }
    auto operator()(SemFieldInitializer<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemFieldInitializer<TypeID, FailureSetID> {
        return {
            .declaration_index = value.declaration_index,
            .value = (*this)(std::move(value.value))
        };
    }
    auto operator()(SemStruct<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemStruct<TypeID, FailureSetID> {
        return {.structure = value.structure, .fields = (*this)(std::move(value.fields))};
    }
    auto operator()(SemEnumCase<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemEnumCase<TypeID, FailureSetID> {
        return {.enum_case = value.enum_case, .payload = (*this)(std::move(value.payload))};
    }
    auto operator()(SemUnary<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemUnary<TypeID, FailureSetID> {
        return {.operation = value.operation, .operand = (*this)(std::move(value.operand))};
    }
    auto operator()(SemBinary<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemBinary<TypeID, FailureSetID> {
        return {
            .left = (*this)(std::move(value.left)),
            .operation = value.operation,
            .right = (*this)(std::move(value.right)),
        };
    }
    auto operator()(SemShortCircuit<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemShortCircuit<TypeID, FailureSetID> {
        return {
            .left = (*this)(std::move(value.left)),
            .operation = value.operation,
            .right = (*this)(std::move(value.right)),
        };
    }
    auto operator()(SemCast<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemCast<TypeID, FailureSetID> {
        return {.operand = (*this)(std::move(value.operand)), .kind = value.kind};
    }
    auto operator()(SemField<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemField<TypeID, FailureSetID> {
        return {.source = (*this)(std::move(value.source)), .field = value.field};
    }
    auto operator()(SemIndex<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemIndex<TypeID, FailureSetID> {
        return {
            .source = (*this)(std::move(value.source)),
            .index = (*this)(std::move(value.index)),
            .bounds = value.bounds,
        };
    }
    auto operator()(SemTextIntrinsic<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemTextIntrinsic<TypeID, FailureSetID> {
        return {.source = (*this)(std::move(value.source)), .intrinsic = value.intrinsic};
    }
    auto operator()(SemCallArgument<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemCallArgument<TypeID, FailureSetID> {
        return {.access = value.access, .expression = (*this)(std::move(value.expression))};
    }
    auto operator()(SemCpp<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemCpp<TypeID, FailureSetID> {
        return {
            .operation = std::move(value.operation),
            .operands = (*this)(std::move(value.operands))
        };
    }
    auto operator()(SemCall<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemCall<TypeID, FailureSetID> {
        return {
            .callee = (*this)(std::move(value.callee)),
            .arguments = (*this)(std::move(value.arguments)),
            .callee_failures = (*this)(value.callee_failures),
        };
    }
    auto operator()(SemCapture<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemCapture<TypeID, FailureSetID> {
        return {.mode = value.mode, .expression = (*this)(std::move(value.expression))};
    }
    auto operator()(SemClosure<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemClosure<TypeID, FailureSetID> {
        return {.callable = value.callable, .captures = (*this)(std::move(value.captures))};
    }
    auto operator()(SemBorrowCallable<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemBorrowCallable<TypeID, FailureSetID> {
        return {.source = (*this)(std::move(value.source)), .loan_lifetime = value.loan_lifetime};
    }
    auto operator()(SemTake<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemTake<TypeID, FailureSetID> {
        return {.place = (*this)(std::move(value.place))};
    }
    auto operator()(SemPropagate<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemPropagate<TypeID, FailureSetID> {
        return {.operand = (*this)(std::move(value.operand))};
    }
    auto operator()(SemConditionalBranch<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemConditionalBranch<TypeID, FailureSetID> {
        return {
            .condition = (*this)(std::move(value.condition)),
            .body = (*this)(std::move(value.body))
        };
    }
    auto operator()(SemIf<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemIf<TypeID, FailureSetID> {
        return {
            .branches = (*this)(std::move(value.branches)),
            .otherwise = (*this)(std::move(value.otherwise))
        };
    }
    auto operator()(SemMatchArm<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemMatchArm<TypeID, FailureSetID> {
        return {
            .pattern = value.pattern,
            .bindings = std::move(value.bindings),
            .guard = (*this)(std::move(value.guard)),
            .body = (*this)(std::move(value.body)),
            .reachable = value.reachable,
        };
    }
    auto operator()(SemMatch<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemMatch<TypeID, FailureSetID> {
        return {
            .subject = (*this)(std::move(value.subject)),
            .subject_is_place = value.subject_is_place,
            .arms = (*this)(std::move(value.arms)),
        };
    }
    auto operator()(SemCatchAlternative<ConstructionTypeRef>&& value) const noexcept
        -> SemCatchAlternative<TypeID> {
        return {
            .origin = value.origin,
            .pattern = std::visit(
                Overloaded {
                    [](CatchAllPattern pattern) static noexcept
                        -> decltype(SemCatchAlternative<TypeID>::pattern) { return pattern; },
                    [&](SemTypedCatchPattern<ConstructionTypeRef> pattern) noexcept
                        -> decltype(SemCatchAlternative<TypeID>::pattern) {
                        return SemTypedCatchPattern<TypeID> {
                            .type = (*this)(pattern.type),
                            .inner = pattern.inner,
                        };
                    },
                },
                value.pattern
            ),
            .reachable = value.reachable,
        };
    }
    auto operator()(SemCatchArm<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemCatchArm<TypeID, FailureSetID> {
        return {
            .origin = value.origin,
            .accepted_failures = (*this)(value.accepted_failures),
            .alternatives = (*this)(std::move(value.alternatives)),
            .bindings = std::move(value.bindings),
            .guard = (*this)(std::move(value.guard)),
            .body = (*this)(std::move(value.body)),
        };
    }
    auto operator()(SemTry<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemTry<TypeID, FailureSetID> {
        const auto protected_failures = (*this)(value.protected_failures);
        if (!draft.failure_set_copy(protected_failures).members.empty()) {
            for (auto& arm : value.arms) {
                const auto accepted = draft.failure_set_copy((*this)(arm.accepted_failures));
                auto useful = false;
                for (auto& alternative : arm.alternatives) {
                    const auto matches = std::visit(
                        Overloaded {
                            [&](CatchAllPattern) noexcept { return !accepted.members.empty(); },
                            [&](const SemTypedCatchPattern<ConstructionTypeRef>& pattern) noexcept {
                                return std::ranges::contains(
                                    accepted.members,
                                    (*this)(pattern.type)
                                );
                            },
                        },
                        alternative.pattern
                    );
                    alternative.reachable = alternative.reachable && matches;
                    useful = useful || alternative.reachable;
                }
                if (!useful) {
                    warning(
                        DiagnosticCode::EffectCatchArmUnreachable,
                        "catch arm cannot match any remaining protected failure",
                        arm.origin
                    );
                } else {
                    for (const auto& alternative : arm.alternatives) {
                        if (!alternative.reachable) {
                            warning(
                                DiagnosticCode::EffectCatchAlternativeUnreachable,
                                "catch alternative cannot match a remaining protected failure",
                                alternative.origin
                            );
                        }
                    }
                }
            }
        }
        return {
            .body = (*this)(std::move(value.body)),
            .protected_failures = protected_failures,
            .residual_failures = (*this)(value.residual_failures),
            .arms = (*this)(std::move(value.arms)),
        };
    }
    auto operator()(SemReturn<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemReturn<TypeID, FailureSetID> {
        return {.value = (*this)(std::move(value.value))};
    }
    auto operator()(SemThrow<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemThrow<TypeID, FailureSetID> {
        return {.value = (*this)(std::move(value.value)), .failure_type = value.failure_type};
    }
    auto operator()(
        SemExpressionStatement<ConstructionTypeRef, FailureTermID>&& value
    ) const noexcept -> SemExpressionStatement<TypeID, FailureSetID> {
        return {.expression = (*this)(std::move(value.expression))};
    }
    auto operator()(SemInitialize<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemInitialize<TypeID, FailureSetID> {
        return {.binding = value.binding, .initializer = (*this)(std::move(value.initializer))};
    }
    auto operator()(SemAssign<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemAssign<TypeID, FailureSetID> {
        return {
            .target = (*this)(std::move(value.target)),
            .compound = value.compound,
            .value = (*this)(std::move(value.value)),
        };
    }
    auto operator()(SemLoop<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemLoop<TypeID, FailureSetID> {
        return {
            .initializer = (*this)(std::move(value.initializer)),
            .condition = (*this)(std::move(value.condition)),
            .body = (*this)(std::move(value.body)),
            .steps = (*this)(std::move(value.steps)),
        };
    }
    auto operator()(SemRangeLoop<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemRangeLoop<TypeID, FailureSetID> {
        return {
            .scope = value.scope,
            .lifetime = value.lifetime,
            .access = value.access,
            .binding = value.binding,
            .begin = (*this)(std::move(value.begin)),
            .end = (*this)(std::move(value.end)),
            .body = (*this)(std::move(value.body)),
        };
    }
    auto operator()(SemTestReport<ConstructionTypeRef, FailureTermID>&& value) const noexcept
        -> SemTestReport<TypeID, FailureSetID> {
        return {
            .kind = value.kind,
            .condition = (*this)(std::move(value.condition)),
            .message = (*this)(std::move(value.message)),
            .condition_source = value.condition_source,
        };
    }

    // These leaves contain no construction terms or recursively owned children.
    template<typename T>
        requires (
            std::same_as<T, SemLiteral>
            || std::same_as<T, SemConstant>
            || std::same_as<T, SemBinding>
            || std::same_as<T, SemCallable>
            || std::same_as<T, SemEnumConstructor>
            || std::same_as<T, SemBreak>
            || std::same_as<T, SemContinue>
            || std::same_as<T, SemRethrow>
        )
    auto operator()(T value) const noexcept -> T {
        return value;
    }

    auto operator()(ElaboratedLocalBinding&& value) const noexcept -> LocalBinding {
        return {
            .name = value.name,
            .type = (*this)(value.type),
            .scope = value.scope,
            .lifetime = value.lifetime,
            .storage = value.storage,
            .origin = value.origin,
        };
    }
    auto operator()(ElaboratedPattern&& value) const noexcept -> Pattern {
        return {
            .type = (*this)(value.type),
            .value = std::visit(
                Overloaded {
                    [&](ElaboratedTypeConstraintPattern pattern) noexcept -> PatternValue {
                        return TypeConstraintPattern {.type = (*this)(pattern.type)};
                    },
                    [](auto&& pattern) static noexcept -> PatternValue {
                        return std::forward<decltype(pattern)>(pattern);
                    },
                },
                std::move(value.value)
            ),
            .origin = value.origin,
        };
    }

private:
    ProgramDraft& draft;
};

} // namespace

auto resolve_body(StructuredBodyDraft&& body, ProgramDraft& draft) noexcept -> SemIRBody {
    const auto resolve = BodyResolver(draft);
    auto bindings = std::move(body.bindings)
                        .transform<LocalBinding>([&](LocalBindingID,
                                                     ElaboratedLocalBinding&& binding) noexcept {
                            return resolve(static_cast<ElaboratedLocalBinding&&>(binding));
                        });
    auto patterns = std::move(body.patterns)
                        .transform<Pattern>([&](PatternID, ElaboratedPattern&& pattern) noexcept {
                            return resolve(std::move(pattern));
                        });
    return SemIRBody({
        .id = body.id,
        .kind = body.kind,
        .provenance_identity = body.provenance_identity,
        .inputs = std::move(body.inputs),
        .scopes = std::move(body.scopes),
        .lifetime_regions = std::move(body.lifetime_regions),
        .bindings = std::move(bindings).seal(),
        .patterns = std::move(patterns).seal(),
        .region = resolve(std::move(body.region)),
    });
}
