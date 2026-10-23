module carven:semantic.semir.seal.impl;

import :semantic.semir.children;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.program;
import :semantic.semir.simd;
import :semantic.semir.text;
import :semantic.semir.traversal;
import :semantic.semir.values;
import :support.invariant;
import std;

namespace {

auto floating_constant(const SemIRProgram& program, ConstantID constant) noexcept -> bool {
    auto pending = std::vector<ConstantID>();
    for (;;) {
        const auto& fact = program.constants().constant(constant);
        if (std::holds_alternative<F32Constant>(fact.value)
            || std::holds_alternative<F64Constant>(fact.value)) {
            return true;
        }
        if (std::holds_alternative<SIMDConstant>(fact.value)) {
            const auto kind =
                std::get<BuiltinTypeValue>(program.types().type(fact.type).value).kind;
            if (kind == BuiltinType::F32x4 || kind == BuiltinType::F32x8) {
                return true;
            }
        }
        if (const auto children = constant_children(fact.value)) {
            pending.append_range(*children);
        }
        if (pending.empty()) {
            return false;
        }
        constant = pending.back();
        pending.pop_back();
    }
}

auto constant_computation(const SemIRProgram& program, const SemanticExpression& source) noexcept
    -> bool {
    return source.value.visit([&](const auto& operation) noexcept -> bool {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemUnary>
                      || std::same_as<Operation, SemBinary>
                      || std::same_as<Operation, SemCast>) {
            // Runtime floating operations retain the native target's execution environment.
            auto floating = floating_constant(program, *source.constant);
            visit_evaluation_children(operation, [&](const auto& child) noexcept {
                floating |= floating_constant(program, std::get<SemConstant>(child.value).constant);
            });
            return !floating;
        } else if constexpr (std::same_as<Operation, SemIntrinsic>) {
            return operation.operation.visit([](const auto& intrinsic) noexcept -> bool {
                using Intrinsic = std::remove_cvref_t<decltype(intrinsic)>;
                if constexpr (std::same_as<Intrinsic, SIMDIntrinsic>
                              || std::same_as<Intrinsic, SliceIntrinsicOperation>) {
                    // Floating SIMD primitives copy lane bits; arithmetic uses scalar operators.
                    return true;
                } else if constexpr (std::same_as<Intrinsic, TextIntrinsic>) {
                    return std::ranges::all_of(
                        text_intrinsic_contract(intrinsic).parameters,
                        [](const auto& parameter) static noexcept {
                            return parameter.access == AccessMode::Read;
                        }
                    );
                } else {
                    return false;
                }
            });
        } else {
            return std::same_as<Operation, SemShortCircuit>
                || std::same_as<Operation, SemRange>
                || std::same_as<Operation, SemTuple>
                || std::same_as<Operation, SemTupleElement>
                || std::same_as<Operation, SemArray>
                || std::same_as<Operation, SemStruct>
                || std::same_as<Operation, SemEnumCase>
                || std::same_as<Operation, SemField>
                || std::same_as<Operation, SemIndex>;
        }
    });
}

class ResidualConstants final {
public:
    explicit ResidualConstants(const SemIRProgram& program) noexcept
        : program(program),
          values(program) {}

    auto operator()(SemanticExpression& source) noexcept -> void {
        if (const auto* report = std::get_if<SemReport>(&source.value);
            report && report->operand_sources) {
            observed.push_back(std::addressof(**report->condition));
        }
    }

    auto leave(SemanticExpression& source) noexcept -> void {
        if (const auto* report = std::get_if<SemReport>(&source.value);
            report && report->operand_sources) {
            observed.pop_back();
            return;
        }
        if ((!observed.empty() && observed.back() == std::addressof(source))
            || !source.constant
            || source.category != SemanticValueCategory::Value) {
            return;
        }
        auto closed = true;
        visit_evaluation_children(source.value, [&](const auto& child) noexcept {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(child)>, SemanticExpression>) {
                closed &= std::holds_alternative<SemConstant>(child.value);
            } else {
                closed = false;
            }
        });
        const auto type = source.type.resolved();
        if (closed
            && !constant_has_storage(values, *source.constant)
            && !std::holds_alternative<CStringConstant>(values.constant(*source.constant).value)
            && !std::holds_alternative<PointerTypeValue>(program.types().type(type).value)
            && program.type_contents(type).mask() == 0u
            && constant_computation(program, source)) {
            source.value = constant_operation(values, *source.constant);
        }
    }

private:
    const SemIRProgram& program;
    ProgramValues values;
    std::vector<const SemanticExpression*> observed;
};

// Stored definitions, references, and pattern edges share the same remapping.
// Observation reads IDs; the remapping instantiation overwrites the same fields.
// NOLINTBEGIN(misc-const-correctness)
template<typename Value, typename BindingVisitor, typename PatternVisitor>
auto visit_local_ids(Value& value, BindingVisitor binding, PatternVisitor pattern) noexcept
    -> void {
    using Node = std::remove_cvref_t<Value>;
    if constexpr (std::same_as<Node, SemanticExpression>
                  || std::same_as<Node, SemanticStatement>
                  || std::same_as<Node, Pattern>) {
        value.value.visit([&](auto& operation) noexcept {
            visit_local_ids(operation, binding, pattern);
        });
    } else if constexpr (std::same_as<Node, BodyInputs>) {
        for (auto& id : value.parameters) {
            binding(id);
        }
        for (auto& id : value.captures) {
            binding(id);
        }
    } else if constexpr (std::same_as<Node, SemBinding>
                         || std::same_as<Node, SemInitialize>
                         || std::same_as<Node, SemStaticBinding>
                         || std::same_as<Node, BindingPattern>) {
        binding(value.binding);
    } else if constexpr (std::same_as<Node, SemWaitReady>) {
        for (auto& id : value.children) {
            binding(id);
        }
    } else if constexpr (std::same_as<Node, SemCloseChild> || std::same_as<Node, SemAsyncLet>) {
        binding(value.child);
    } else if constexpr (std::same_as<Node, SemAsyncIntrinsic>) {
        if (value.child) {
            binding(*value.child);
        }
    } else if constexpr (std::same_as<Node, SemDecompose>) {
        binding(value.owner);
        for (auto& selected : value.bindings) {
            binding(selected.binding);
        }
    } else if constexpr (std::same_as<Node, SemRangeLoop>) {
        if (value.binding) {
            binding(*value.binding);
        }
    } else if constexpr (std::same_as<Node, SemMatch> || std::same_as<Node, SemTry>) {
        for (auto& arm : value.arms) {
            for (auto& id : arm.bindings) {
                binding(id);
            }
            for (auto& bound : arm.pattern_bounds) {
                pattern(bound.pattern);
            }
            if constexpr (std::same_as<Node, SemMatch>) {
                pattern(arm.pattern);
            } else {
                for (auto& alternative : arm.alternatives) {
                    if (auto* typed = std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                        pattern(typed->inner);
                    }
                }
            }
        }
    } else if constexpr (std::same_as<Node, OrPattern>) {
        for (auto& id : value.alternatives) {
            pattern(id);
        }
    } else if constexpr (std::same_as<Node, EnumCasePattern>) {
        for (auto& id : value.payload) {
            pattern(id);
        }
    } else if constexpr (std::same_as<Node, TuplePattern>) {
        for (auto& id : value.elements) {
            pattern(id);
        }
    }
}

// NOLINTEND(misc-const-correctness)

} // namespace

auto SemIRBody::publish(const SemIRProgram& program) noexcept -> void {
    if (data.residual) {
        data.region = std::move(*data.residual);
        data.residual.reset();
    }
    visit_semantic_nodes(data.region, ResidualConstants(program));
    auto live_bindings = std::vector<bool>(data.bindings.size());
    auto live_patterns = std::vector<bool>(data.patterns.size());
    auto pending_patterns = std::vector<PatternID>();
    const auto binding = [&](LocalBindingID id) noexcept {
        live_bindings[id.index()] = true;
    };
    const auto pattern = [&](PatternID id) noexcept {
        if (!live_patterns[id.index()]) {
            live_patterns[id.index()] = true;
            pending_patterns.push_back(id);
        }
    };
    visit_local_ids(data.inputs, binding, pattern);
    visit_semantic_nodes(data.region, [&](auto& node) noexcept {
        visit_local_ids(node, binding, pattern);
    });
    while (!pending_patterns.empty()) {
        const auto id = pending_patterns.back();
        pending_patterns.pop_back();
        visit_local_ids(data.patterns.get(id), binding, pattern);
    }
    const auto bindings =
        data.bindings.retain([&](LocalBindingID id) noexcept { return live_bindings[id.index()]; });
    const auto patterns =
        data.patterns.retain([&](PatternID id) noexcept { return live_patterns[id.index()]; });
    const auto remap_binding = [&](LocalBindingID& id) noexcept {
        id = *bindings[id.index()];
    };
    const auto remap_pattern = [&](PatternID& id) noexcept {
        id = *patterns[id.index()];
    };
    visit_local_ids(data.inputs, remap_binding, remap_pattern);
    visit_semantic_nodes(data.region, [&](auto& node) noexcept {
        visit_local_ids(node, remap_binding, remap_pattern);
    });
    for (auto& row : data.patterns.storage) {
        visit_local_ids(row, remap_binding, remap_pattern);
    }
    // Instance provenance belongs to the callable's program-level record.
    data.specialized.reset();
}

auto DeclarationStore::publish_bodies(std::span<const std::optional<BodyID>> bodies) noexcept
    -> void {
    auto rows = MutableProgramTable<CallableDeclaration, CallableID>(owner());
    body_callables.clear();
    for (auto& declaration : std::move(callable_rows).release()) {
        declaration.implementation.visit([&](auto& implementation) noexcept {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (std::same_as<Implementation, FunctionBodyImplementation>) {
                implementation.body = bodies[implementation.body->index()];
            } else if constexpr (std::same_as<Implementation, ClosureBodyImplementation>) {
                const auto body = bodies[implementation.body.index()];
                if (!body) {
                    invariant_violation("closure has no executable body at publication");
                }
                implementation.body = *body;
            }
        });
        const auto body = callable_body_id(declaration);
        const auto callable = rows.add(declaration);
        if (body) {
            body_callables.emplace(*body, callable);
        }
    }
    callable_rows.storage = std::move(rows).seal().release();
}

auto SemIRProgram::publish_bodies() noexcept -> void {
    auto mapping = std::vector<std::optional<BodyID>>(body_store.rows.size());
    for (auto& owned : body_store.rows.storage) {
        auto& body = *owned;
        const auto original = body.id();
        if (!executed_bodies[original.index()]) {
            owned.reset();
            continue;
        }
        body.publish(*this);
        visit_semantic_nodes(body.region(), [](const auto& node) static noexcept {
            using Node = std::remove_cvref_t<decltype(node)>;
            if constexpr (std::same_as<Node, SemanticStatement>) {
                if (std::holds_alternative<SemStaticBinding>(node.value)
                    || std::holds_alternative<SemConstBlock>(node.value)) {
                    invariant_violation("published executable body retains a static root");
                }
                if (const auto* loop = std::get_if<SemRangeLoop>(&node.value);
                    loop && loop->is_static) {
                    invariant_violation("published executable body retains static iteration");
                }
            } else if constexpr (std::same_as<Node, SemanticExpression>) {
                if (const auto* branch = std::get_if<SemIf>(&node.value);
                    branch && branch->is_static) {
                    invariant_violation("published executable body retains static selection");
                }
            }
        });
        mapping[original.index()] = original;
    }
    declaration_store.publish_bodies(mapping);
    for (const auto entry : declarations().callables()) {
        if (const auto body = callable_body_id(entry.value)) {
            if (!body_store.contains(*body)
                || callable_signatures().signature(entry.value.signature).has_static_parameters()) {
                invariant_violation("published callable does not have an executable contract");
            }
        }
    }
    auto tests = MutableProgramTable<TestDeclaration, TestID>(identity());
    for (auto& test : std::move(test_store.rows).release()) {
        test.body = mapping[test.body->index()];
        if ((test.execution == TestExecution::Static) == test.body.has_value()) {
            invariant_violation("test publication disagrees with its execution stage");
        }
        tests.add(test);
    }
    test_store.rows.storage = std::move(tests).seal().release();
}
