module carven:semantic.semir.program.impl;

import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.module_path;
import :source.provenance.ids;
import :source.provenance;
import :source.text;
import :support.invariant;
import std;

SemIRProgram::SemIRProgram(
    ProgramIdentity identity,
    CompilationProvenance provenance,
    CanonicalTypeStore types,
    ConstantStore constants,
    FailureSetStore failure_sets,
    CallableSignatureStore callable_signatures,
    DeclarationStore declarations,
    BodyStore bodies,
    TestStore tests,
    std::vector<bool> test_stops
) noexcept
    : program_identity(identity),
      compilation_provenance(std::move(provenance)),
      type_store(std::move(types)),
      constant_store(std::move(constants)),
      failure_set_store(std::move(failure_sets)),
      callable_signature_store(std::move(callable_signatures)),
      declaration_store(std::move(declarations)),
      body_store(std::move(bodies)),
      test_store(std::move(tests)),
      test_stops(std::move(test_stops)) {}

auto SemIRProgram::may_stop_test(CallableID callable_id) const noexcept -> bool {
    static_cast<void>(declaration_store.callable(callable_id));
    return test_stops.at(callable_id.index());
}

auto SemIRProgram::may_stop_test(TypeID type) const noexcept -> bool {
    const auto& value = type_store.type(type).value;
    if (const auto* function = std::get_if<FunctionTypeValue>(&value)) {
        return may_stop_test(function->callable);
    }
    if (const auto* closure = std::get_if<ClosureTypeValue>(&value)) {
        return may_stop_test(closure->callable);
    }
    if (std::holds_alternative<CallableViewTypeValue>(value)) {
        return true;
    }
    invariant_violation("test-stop query requires a callable type");
}

auto SemIRProgram::call_signature(TypeID type) const noexcept -> CallableSignatureID {
    return std::visit(
        [&](const auto& value) noexcept -> CallableSignatureID {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, CallableViewTypeValue>) {
                return value.signature;
            } else if constexpr (std::same_as<Value, FunctionTypeValue>
                                 || std::same_as<Value, ClosureTypeValue>) {
                return declaration_store.callable(value.callable).signature;
            } else {
                invariant_violation("non-callable has no signature");
            }
        },
        type_store.type(type).value
    );
}

auto SemIRProgram::may_stop_test(const SemanticExpression& expression) const noexcept -> bool {
    auto exits = expression.exits_test;
    visit_semantic_nodes(expression, [&](const SemanticExpression& node) noexcept {
        if (const auto* call = std::get_if<SemCall>(&node.value)) {
            exits |= may_stop_test(call->callee->type.resolved());
        }
    });
    return exits;
}

auto BodyStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto BodyStore::contains(BodyID id) const noexcept -> bool {
    return rows.contains(id);
}

auto BodyStore::body(BodyID id) const noexcept -> const SemIRBody& {
    return rows.get(id);
}

auto BodyStore::entries() const noexcept -> IDTableEntries<BodyID, SemIRBody, ProgramIdentity> {
    return rows.entries();
}

auto BodyStore::size() const noexcept -> std::size_t {
    return rows.size();
}

BodyStore::BodyStore(ImmutableProgramTable<SemIRBody, BodyID> values) noexcept
    : rows(std::move(values)) {}

auto TestStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto TestStore::contains(TestID id) const noexcept -> bool {
    return rows.contains(id);
}

auto TestStore::test(TestID id) const noexcept -> const TestDeclaration& {
    return rows.get(id);
}

auto TestStore::entries() const noexcept
    -> IDTableEntries<TestID, TestDeclaration, ProgramIdentity> {
    return rows.entries();
}

auto TestStore::size() const noexcept -> std::size_t {
    return rows.size();
}

TestStore::TestStore(ImmutableProgramTable<TestDeclaration, TestID> values) noexcept
    : rows(std::move(values)) {}

auto SemIRProgram::identity() const noexcept -> ProgramIdentity {
    return program_identity;
}

auto SemIRProgram::provenance() const noexcept -> CompilationProvenanceView {
    return compilation_provenance.view();
}

auto SemIRProgram::types() const noexcept -> const CanonicalTypeStore& {
    return type_store;
}

auto SemIRProgram::constants() const noexcept -> const ConstantStore& {
    return constant_store;
}

auto SemIRProgram::failure_sets() const noexcept -> const FailureSetStore& {
    return failure_set_store;
}

auto SemIRProgram::callable_signatures() const noexcept -> const CallableSignatureStore& {
    return callable_signature_store;
}

auto SemIRProgram::declarations() const noexcept -> const DeclarationStore& {
    return declaration_store;
}

auto SemIRProgram::bodies() const noexcept -> const BodyStore& {
    return body_store;
}

auto SemIRProgram::tests() const noexcept -> const TestStore& {
    return test_store;
}
