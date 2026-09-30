module carven:semantic.analysis.stage.specialization.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.program;
import :semantic.analysis.stage.session;
import :semantic.analysis.stage.specialization;
import :semantic.evaluation.operation;
import :semantic.evaluation.value;
import :semantic.semir.children;
import :semantic.semir.completion;
import :semantic.semir.decl;
import :semantic.semir.simd;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace {

class Specializer final {
public:
    Specializer(StaticStage& stage, BodyID body, StaticEnvironment environment) noexcept;
    auto region(SemanticRegion& source, bool retain_bindings = false) noexcept
        -> AnalysisTask<void>;
    auto changed() const noexcept -> bool;

private:
    auto expression(SemanticExpression& source) noexcept -> AnalysisTask<void>;
    auto statement(SemanticStatement& source) noexcept -> AnalysisTask<void>;
    auto pattern_bounds(
        std::span<const PatternID> roots,
        std::vector<SemPatternBounds>& bounds
    ) noexcept -> std::map<PatternID, SemPatternBounds*>;
    auto pattern(
        PatternID root,
        const std::map<PatternID, SemPatternBounds*>& bounds,
        CompletionQuery& completion
    ) noexcept -> AnalysisTask<void>;
    auto unreachable(SemanticExpression& source) noexcept -> void;
    auto unreachable(SemanticRegion& source) noexcept -> void;
    // Replaces the static bindings a tree reads by their values.
    template<typename Tree>
    auto substitute(Tree& source) noexcept -> void;
    // Executes a static expression and freezes its result.
    auto evaluate(
        SemanticExpression& expression,
        std::optional<ConstructionTypeRef> frozen_type = std::nullopt
    ) noexcept -> AnalysisTask<ConstantID>;
    auto select(SemIf& value) noexcept -> AnalysisTask<std::optional<SemanticRegion>>;
    auto expand(SemRangeLoop& loop, ProgramOriginID origin) noexcept
        -> AnalysisTask<SemExpandedLoop>;
    auto completion_patterns() const noexcept -> CompletionPatterns;

    StaticStage& stage;
    ProgramDraft& draft;
    BodyID body;
    StaticEnvironment environment;
    bool rewritten = false;
};

Specializer::Specializer(StaticStage& stage, BodyID body, StaticEnvironment environment) noexcept
    : stage(stage),
      draft(stage.draft()),
      body(body),
      environment(std::move(environment)) {}

auto Specializer::changed() const noexcept -> bool {
    return rewritten;
}

auto Specializer::completion_patterns() const noexcept -> CompletionPatterns {
    return {
        .read =
            [this](PatternID id) noexcept -> std::variant<PatternValue, ElaboratedPatternValue> {
            return draft.body_draft(body).patterns.get(id).value;
        },
        .single_case =
            [this](EnumCaseID id) noexcept {
                const auto owner = draft.construction_enum_case_declaration_copy(id).owner;
                return draft.enum_cases(owner).size() == 1uz;
            },
    };
}

template<typename Tree>
auto Specializer::substitute(Tree& source) noexcept -> void {
    auto expressions = std::vector<SemanticExpression*>();
    visit_semantic_nodes(source, [&](SemanticExpression& expression) noexcept {
        expressions.push_back(&expression);
    });
    for (auto* expression : expressions) {
        const auto* binding = std::get_if<SemBinding>(&expression->value);
        if (!binding) {
            continue;
        }
        if (const auto found = environment.find(binding->binding); found != environment.end()) {
            expression->constant = found->second;
            expression->category = SemanticValueCategory::Value;
            expression->value = SemConstant {found->second};
        }
    }
}

auto Specializer::evaluate(
    SemanticExpression& source,
    std::optional<ConstructionTypeRef> frozen_type
) noexcept -> AnalysisTask<ConstantID> {
    substitute(source);
    auto value = co_await stage.evaluate(source);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    const auto actual = execution_value_type(draft, *value);
    const auto frozen = freeze_constant_value(draft, std::move(*value));
    // Keep explicit type selection for the Clang 23 coroutine workaround.
    const auto expected = frozen_type ? *frozen_type : actual;
    if (!frozen
        || !std::holds_alternative<TypeID>(actual)
        || ConstructionTypeRef(draft.constant(*frozen).type) != expected) {
        co_return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::ConstAdmission,
                "static expression has no frozen representation of its required type"
            )
                .primary(draft.source_span(source.origin))
                .build()
        ));
    }
    co_return *frozen;
}

auto Specializer::select(SemIf& conditional) noexcept
    -> AnalysisTask<std::optional<SemanticRegion>> {
    for (auto& branch : conditional.branches) {
        auto constant = co_await evaluate(branch.condition);
        if (!constant) {
            co_return std::unexpected(constant.error());
        }
        const auto* boolean = std::get_if<BooleanConstant>(&draft.constant(*constant).value);
        if (boolean == nullptr) {
            invariant_violation("static conditional did not produce a Boolean");
        }
        if (boolean->value) {
            co_return std::move(branch.body);
        }
    }
    if (conditional.otherwise) {
        co_return std::move(**conditional.otherwise);
    }
    co_return std::nullopt;
}

auto Specializer::pattern_bounds(
    std::span<const PatternID> roots,
    std::vector<SemPatternBounds>& bounds
) noexcept -> std::map<PatternID, SemPatternBounds*> {
    auto pending = std::vector<PatternID>(roots.begin(), roots.end());
    auto selected = std::set<PatternID>();
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!selected.insert(id).second) {
            continue;
        }
        draft.body_draft(body).patterns.get(id).value.visit([&](const auto& operation) noexcept {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, OrPattern>) {
                pending.insert(
                    pending.end(),
                    operation.alternatives.begin(),
                    operation.alternatives.end()
                );
            } else if constexpr (std::same_as<Operation, EnumCasePattern>) {
                pending.insert(pending.end(), operation.payload.begin(), operation.payload.end());
            }
        });
    }
    rewritten |= std::erase_if(
                     bounds,
                     [&](const SemPatternBounds& bound) noexcept {
                         return !selected.contains(bound.pattern);
                     }
                 )
        != 0uz;
    auto result = std::map<PatternID, SemPatternBounds*>();
    for (auto& bound : bounds) {
        result.emplace(bound.pattern, &bound);
    }
    return result;
}

auto Specializer::unreachable(SemanticExpression& source) noexcept -> void {
    source.constant.reset();
    source.failures = BodyFailures(draft.add_empty_failure_term());
    source.exits_test = false;
    source.operation_reachable = false;
    source.category = SemanticValueCategory::Value;
    source.value = SemUnreachable {};
    rewritten = true;
}

auto Specializer::unreachable(SemanticRegion& source) noexcept -> void {
    rewritten |= !source.statements.empty() || source.result.has_value();
    source.statements.clear();
    source.result.reset();
    source.result_reachable = false;
}

auto Specializer::pattern(
    PatternID root,
    const std::map<PatternID, SemPatternBounds*>& bounds,
    CompletionQuery& completion
) noexcept -> AnalysisTask<void> {
    struct Pending final {
        PatternID id;
        std::vector<PatternID> children;
        std::size_t next;
        bool disjunction;
        bool entered;
        bool next_entered;
        bool child_pending;
    };

    const auto enter = [&](PatternID id, bool entered) noexcept {
        auto result = Pending {
            .id = id,
            .children = {},
            .next = 0uz,
            .disjunction = false,
            .entered = entered,
            .next_entered = entered,
            .child_pending = false,
        };
        draft.body_draft(body).patterns.get(id).value.visit([&](const auto& operation) noexcept {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, OrPattern>) {
                result.children = operation.alternatives;
                result.disjunction = true;
            } else if constexpr (std::same_as<Operation, EnumCasePattern>) {
                result.children = operation.payload;
            }
        });
        return result;
    };
    auto pending = std::vector<Pending> {enter(root, true)};
    while (!pending.empty()) {
        auto& item = pending.back();
        if (item.child_pending) {
            const auto selected = completion.pattern(item.children[item.next - 1uz]);
            item.next_entered &= item.disjunction ? selected.rejected : selected.accepted;
            item.child_pending = false;
        }
        if (item.next < item.children.size()) {
            const auto child = item.children[item.next++];
            item.child_pending = true;
            pending.push_back(enter(child, item.next_entered));
            continue;
        }
        const auto found = bounds.find(item.id);
        if (found != bounds.end()) {
            auto normal = item.entered;
            for (auto* value : {&found->second->begin, &found->second->end}) {
                if (!*value) {
                    continue;
                }
                if (!normal) {
                    unreachable(**value);
                    continue;
                }
                auto result = co_await expression(**value);
                if (!result) {
                    co_return std::unexpected(result.error());
                }
                normal = exits(**value, completion_patterns()).contains(Exit::Normal);
            }
            static_cast<void>(completion.pattern(item.id, std::span(found->second, 1)));
        } else {
            static_cast<void>(completion.pattern(item.id));
        }
        pending.pop_back();
    }
    co_return {};
}

auto Specializer::expression(SemanticExpression& source) noexcept -> AnalysisTask<void> {
    if (auto charged = stage.charge(StageResource::Nodes, source.origin); !charged) {
        co_return std::unexpected(charged.error());
    }
    if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
        if (const auto found = environment.find(binding->binding); found != environment.end()) {
            source.constant = found->second;
            source.category = SemanticValueCategory::Value;
            source.value = SemConstant {found->second};
            rewritten = true;
        }
        co_return {};
    }
    if (auto* conditional = std::get_if<SemIf>(&source.value);
        conditional && conditional->is_static) {
        rewritten = true;
        auto selected = co_await select(*conditional);
        if (!selected) {
            co_return std::unexpected(selected.error());
        }
        if (!*selected) {
            source.value = SemIf {.branches = {}, .otherwise = std::nullopt, .is_static = false};
            co_return {};
        }
        auto arm = co_await region(**selected);
        if (!arm) {
            co_return std::unexpected(arm.error());
        }
        if ((*selected)->statements.empty() && (*selected)->result) {
            source = std::move(*(*selected)->result);
        } else {
            source.value = SemIf {
                .branches = {},
                .otherwise = OwnedSemanticRegion(std::move(**selected)),
                .is_static = false
            };
        }
        co_return {};
    }
    if (auto* call = std::get_if<SemCall>(&source.value)) {
        const auto function =
            call->target ? draft.function_for_callable(*call->target) : std::nullopt;
        if (function && draft.staged_function(*function)) {
            rewritten = true;
            const auto contract = draft.construction_callable_contract_copy(*call->target);
            auto constants = std::vector<ConstantID>();
            auto runtime = std::vector<SemCallArgument>();
            for (auto index = 0uz; index < call->arguments.size(); ++index) {
                auto& argument = call->arguments[index];
                if (contract.parameters[index].stage == ParameterStage::Static) {
                    auto value = co_await evaluate(argument.expression);
                    if (!value) {
                        co_return std::unexpected(value.error());
                    }
                    constants.push_back(*value);
                } else {
                    auto value = co_await expression(argument.expression);
                    if (!value) {
                        co_return std::unexpected(value.error());
                    }
                    runtime.push_back(std::move(argument));
                }
            }
            auto instance = co_await stage.instance(*function, std::move(constants), source.origin);
            if (!instance) {
                co_return std::unexpected(instance.error());
            }
            // The call names the instance as an ordinary callable.
            call->target = *instance;
            call->callee->type =
                BodyType(draft.intern_type({.value = FunctionTypeValue {.callable = *instance}}));
            call->callee->value = SemCallable {.callable = *instance};
            call->arguments = std::move(runtime);
            co_return {};
        }
    }
    if (auto* intrinsic = std::get_if<SemIntrinsic>(&source.value)) {
        if (const auto* operation = std::get_if<SIMDIntrinsic>(&intrinsic->operation)) {
            if (const auto index = simd_static_input(*operation)) {
                auto& operand = intrinsic->operands.at(*index).expression;
                const auto known = operand.constant.has_value();
                auto constant = co_await evaluate(operand);
                if (!constant) {
                    co_return std::unexpected(constant.error());
                }
                const auto owner = simd_owner(
                    *operation,
                    std::get<TypeID>(source.type.construction()),
                    std::get<TypeID>(intrinsic->operands.front().expression.type.construction()),
                    [&](TypeID type) noexcept { return draft.type_copy(type); }
                );
                const auto* integer =
                    std::get_if<IntegerConstant>(&draft.constant(*constant).value);
                if (!integer
                    || integer->negative()
                    || integer->magnitude() >= simd_static_limit(*operation, owner)) {
                    co_return std::unexpected(
                        draft.diagnostics().error(DiagnosticBuilder(
                                                      DiagnosticCode::ConstIndexBounds,
                                                      "SIMD immediate control is out of range"
                        )
                                                      .primary(draft.source_span(operand.origin))
                                                      .build())
                    );
                }
                if (!known) {
                    operand.constant = *constant;
                    operand.category = SemanticValueCategory::Value;
                    operand.value = SemConstant {*constant};
                    rewritten = true;
                }
            }
        }
    }
    if (auto* match = std::get_if<SemMatch>(&source.value)) {
        auto subject = co_await expression(*match->subject);
        if (!subject) {
            co_return std::unexpected(subject.error());
        }
        auto completion = CompletionQuery(completion_patterns());
        auto pending = exits(*match->subject, completion_patterns()).contains(Exit::Normal);
        for (auto& arm : match->arms) {
            if (!arm.reachable || !pending) {
                arm.reachable = false;
                continue;
            }
            auto bounds = pattern_bounds(std::span(&arm.pattern, 1), arm.pattern_bounds);
            auto projected = co_await pattern(arm.pattern, bounds, completion);
            if (!projected) {
                co_return std::unexpected(projected.error());
            }
            const auto selected = completion.pattern(arm.pattern);
            auto guard_normal = false;
            if (arm.guard) {
                if (!selected.accepted) {
                    unreachable(*arm.guard);
                } else {
                    auto guard = co_await expression(*arm.guard);
                    if (!guard) {
                        co_return std::unexpected(guard.error());
                    }
                    guard_normal = exits(*arm.guard, completion_patterns()).contains(Exit::Normal);
                }
            }
            if (selected.accepted && (!arm.guard || guard_normal)) {
                auto value = co_await region(arm.body);
                if (!value) {
                    co_return std::unexpected(value.error());
                }
            } else {
                unreachable(arm.body);
            }
            pending = (selected.rejected && !arm.pattern_always_matches)
                || (selected.accepted && guard_normal);
        }
        rewritten |= std::erase_if(
                         match->arms,
                         [](const SemMatchArm& arm) noexcept { return !arm.reachable; }
                     )
            != 0uz;
        co_return {};
    }
    if (auto* attempt = std::get_if<SemTry>(&source.value)) {
        auto protected_body = co_await region(*attempt->body);
        if (!protected_body) {
            co_return std::unexpected(protected_body.error());
        }
        auto completion = CompletionQuery(completion_patterns());
        completion.enter_try(*attempt->body);
        for (auto& arm : attempt->arms) {
            auto roots = std::vector<PatternID>();
            for (const auto& alternative : arm.alternatives) {
                if (alternative.reachable) {
                    if (const auto* typed =
                            std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                        roots.push_back(typed->inner);
                    }
                }
            }
            auto bounds = pattern_bounds(roots, arm.pattern_bounds);
            for (auto& alternative : arm.alternatives) {
                const auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern);
                const auto type = typed ? std::optional(typed->type.construction()) : std::nullopt;
                if (!alternative.reachable || !completion.catch_entry(type)) {
                    alternative.reachable = false;
                    continue;
                }
                if (typed) {
                    auto projected = co_await pattern(typed->inner, bounds, completion);
                    if (!projected) {
                        co_return std::unexpected(projected.error());
                    }
                }
                completion.consume_catch(type, typed ? std::optional(typed->inner) : std::nullopt);
            }
            rewritten |= std::erase_if(
                             arm.alternatives,
                             [](const SemCatchAlternative& alternative) noexcept {
                                 return !alternative.reachable;
                             }
                         )
                != 0uz;
            roots.clear();
            for (const auto& alternative : arm.alternatives) {
                if (const auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                    roots.push_back(typed->inner);
                }
            }
            static_cast<void>(pattern_bounds(roots, arm.pattern_bounds));
            const auto accepted = completion.catch_accepted();
            auto guard_normal = false;
            if (arm.guard) {
                if (!accepted) {
                    unreachable(*arm.guard);
                } else {
                    auto guard = co_await expression(*arm.guard);
                    if (!guard) {
                        co_return std::unexpected(guard.error());
                    }
                    guard_normal = exits(*arm.guard, completion_patterns()).contains(Exit::Normal);
                }
            }
            if (accepted && (!arm.guard || guard_normal)) {
                auto value = co_await region(arm.body);
                if (!value) {
                    co_return std::unexpected(value.error());
                }
            } else {
                unreachable(arm.body);
            }
            completion.finish_catch(guard_normal);
        }
        rewritten |= std::erase_if(
                         attempt->arms,
                         [](const SemCatchArm& arm) noexcept { return arm.alternatives.empty(); }
                     )
            != 0uz;
        if (const auto possible = completion.pending_failures()) {
            auto types = std::vector<TypeID>();
            for (const auto type : *possible) {
                if (const auto* concrete = std::get_if<TypeID>(&type)) {
                    types.push_back(*concrete);
                } else {
                    invariant_violation("known catch residual has no concrete failure type");
                }
            }
            attempt->residual_failures = BodyFailures(draft.add_intersection_failure_term(
                attempt->residual_failures.term(),
                std::move(types)
            ));
            rewritten = true;
        }
        co_return {};
    }
    // A closure keeps its callable; only its capture values specialize.
    using Child = std::variant<SemanticExpression*, SemanticRegion*>;
    auto children = std::vector<Child>();
    visit_semantic_children(source.value, [&](auto& child) noexcept {
        children.emplace_back(&child);
    });
    for (auto child : children) {
        auto result = co_await child.visit([&](auto* value) noexcept -> AnalysisTask<void> {
            using Value = std::remove_cvref_t<decltype(*value)>;
            if constexpr (std::same_as<Value, SemanticExpression>) {
                co_return co_await expression(*value);
            } else {
                co_return co_await region(*value);
            }
        });
        if (!result) {
            co_return std::unexpected(result.error());
        }
    }
    co_return {};
}

auto Specializer::expand(SemRangeLoop& loop, ProgramOriginID origin) noexcept
    -> AnalysisTask<SemExpandedLoop> {
    auto constant = co_await evaluate(loop.source);
    if (!constant) {
        co_return std::unexpected(constant.error());
    }
    const auto fact = draft.constant(*constant);
    const auto* range = std::get_if<RangeConstant>(&fact.value);
    if (!range) {
        invariant_violation("static loop source is not an integer range");
    }
    const auto range_type = draft.type_copy(fact.type);
    const auto element = std::get<RangeTypeValue>(range_type.value).element;
    const auto saved = environment;
    auto expanded = SemExpandedLoop {.iterations = {}};
    auto current = range->begin;
    const auto less = [](IntegerConstant a, IntegerConstant b) static noexcept {
        return a.negative() != b.negative() ? a.negative()
            : a.negative()                  ? a.magnitude() > b.magnitude()
                                            : a.magnitude() < b.magnitude();
    };
    while (less(current, range->end) || (range->inclusive && current == range->end)) {
        if (auto charged = stage.charge(StageResource::Iterations, origin); !charged) {
            co_return std::unexpected(charged.error());
        }
        environment = saved;
        if (loop.binding) {
            environment.insert_or_assign(
                *loop.binding,
                draft.intern_constant({.type = element, .value = current})
            );
        }
        auto iteration = *loop.body;
        auto result = co_await region(iteration);
        if (!result) {
            co_return std::unexpected(result.error());
        }
        const auto leaves = exits(iteration, completion_patterns());
        expanded.iterations.push_back(std::move(iteration));
        // Continue enters the next copy; a copy that neither completes nor
        // continues makes the later ones unreachable.
        if ((!leaves.contains(Exit::Normal) && !leaves.contains(Exit::Continue))
            || current == range->end) {
            break;
        }
        current = current.negative() ? IntegerConstant::from_parts(current.magnitude() - 1u, true)
                                     : IntegerConstant::from_parts(current.magnitude() + 1u, false);
    }
    environment = saved;
    co_return expanded;
}

auto Specializer::statement(SemanticStatement& source) noexcept -> AnalysisTask<void> {
    if (auto charged = stage.charge(StageResource::Nodes, source.origin); !charged) {
        co_return std::unexpected(charged.error());
    }
    co_return co_await source.value.visit([&](auto& operation) noexcept -> AnalysisTask<void> {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemStaticBinding>) {
            auto value = co_await evaluate(
                *operation.initializer,
                draft.body_draft(body).bindings.get(operation.binding).type
            );
            if (!value) {
                co_return std::unexpected(value.error());
            }
            environment.insert_or_assign(operation.binding, *value);
            co_return {};
        } else if constexpr (std::same_as<Operation, SemConstBlock>) {
            substitute(*operation.region);
            co_return co_await stage.run_block(
                body,
                *operation.region,
                {.label = operation.label, .origin = operation.source}
            );
        } else if constexpr (std::same_as<Operation, SemRangeLoop>) {
            if (operation.is_static) {
                rewritten = true;
                auto expanded = co_await expand(operation, source.origin);
                if (!expanded) {
                    co_return std::unexpected(expanded.error());
                }
                source.value = std::move(*expanded);
                co_return {};
            }
            auto input = co_await expression(operation.source);
            if (!input) {
                co_return std::unexpected(input.error());
            }
            co_return co_await region(*operation.body);
        } else if constexpr (std::same_as<Operation, SemLoop>) {
            const auto saved = environment;
            auto initial = co_await region(*operation.initializer, true);
            if (!initial) {
                co_return std::unexpected(initial.error());
            }
            if (operation.condition) {
                auto condition = co_await expression(*operation.condition);
                if (!condition) {
                    co_return std::unexpected(condition.error());
                }
            }
            auto nested = co_await region(*operation.body);
            if (!nested) {
                co_return std::unexpected(nested.error());
            }
            auto steps = co_await region(*operation.steps);
            environment = saved;
            co_return steps;
        } else if constexpr (std::same_as<Operation, SemExpandedLoop>) {
            invariant_violation("a checked body already contains an expanded loop");
        } else {
            using Child = std::variant<SemanticExpression*, SemanticRegion*>;
            auto children = std::vector<Child>();
            visit_semantic_children(operation, [&](auto& child) noexcept {
                children.emplace_back(&child);
            });
            for (auto child : children) {
                auto result = co_await child.visit([&](auto* value) noexcept -> AnalysisTask<void> {
                    using Value = std::remove_cvref_t<decltype(*value)>;
                    if constexpr (std::same_as<Value, SemanticExpression>) {
                        co_return co_await expression(*value);
                    } else {
                        co_return co_await region(*value);
                    }
                });
                if (!result) {
                    co_return std::unexpected(result.error());
                }
            }
            co_return {};
        }
    });
}

auto Specializer::region(SemanticRegion& source, bool retain_bindings) noexcept
    -> AnalysisTask<void> {
    const auto saved = environment;
    auto statements = std::vector<SemanticStatement>();
    auto completes = true;
    for (auto& item : source.statements) {
        if (!item.reachable) {
            rewritten = true;
            continue;
        }
        // The static stage leaves no statement in executable code.
        const auto consumed = std::holds_alternative<SemStaticBinding>(item.value)
            || std::holds_alternative<SemConstBlock>(item.value);
        auto result = co_await statement(item);
        if (!result) {
            co_return std::unexpected(result.error());
        }
        if (consumed) {
            rewritten = true;
            continue;
        }
        completes = exits(item, completion_patterns()).contains(Exit::Normal);
        statements.push_back(std::move(item));
        // Control never enters what follows a statement that does not complete.
        if (!completes) {
            break;
        }
    }
    if (!completes && (statements.size() != source.statements.size() || source.result)) {
        rewritten = true;
    }
    source.statements = std::move(statements);
    if (!completes || (source.result && !source.result_reachable)) {
        rewritten |= source.result.has_value();
        source.result.reset();
        source.result_reachable = false;
    } else if (source.result) {
        auto result = co_await expression(*source.result);
        if (!result) {
            co_return std::unexpected(result.error());
        }
    }
    if (!retain_bindings) {
        environment = saved;
    }
    co_return {};
}

} // namespace

auto specialize_region(
    StaticStage& stage,
    BodyID body,
    StaticEnvironment environment,
    SemanticRegion& region
) noexcept -> AnalysisTask<bool> {
    auto specializer = Specializer(stage, body, std::move(environment));
    auto result = co_await specializer.region(region);
    if (!result) {
        co_return std::unexpected(result.error());
    }
    co_return specializer.changed();
}
