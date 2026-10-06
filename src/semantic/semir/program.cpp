module carven:semantic.semir.program.impl;

import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.publication;
import :semantic.semir.stage;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.module_path;
import :source.provenance;
import :source.provenance.ids;
import :source.text;
import :support.invariant;
import :support.visit;
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
    std::vector<StaticInstance> static_instances,
    std::vector<GenericDeclarationContract> generic_declarations,
    std::vector<GenericNominalInstance> generic_instances,
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
      static_instance_store(std::move(static_instances)),
      generic_declaration_store(std::move(generic_declarations)),
      generic_instance_store(std::move(generic_instances)),
      test_stops(std::move(test_stops)) {
    for (const auto& contract : generic_declaration_store) {
        static_cast<void>(declaration_store.module_decl(contract.module_id));
        if (!compilation_provenance.view().contains(contract.name)
            || !compilation_provenance.view().contains(contract.origin)) {
            invariant_violation("generic declaration contract has foreign provenance");
        }
        for (const auto& dependency : contract.audience_dependencies) {
            if (!compilation_provenance.view().contains(dependency.origin)) {
                invariant_violation("generic audience dependency has foreign provenance");
            }
            dependency.reference.visit(
                Overloaded {
                    [&](TypeID id) noexcept { static_cast<void>(type_store.type(id)); },
                    [&](GenericDeclarationID id) noexcept {
                        static_cast<void>(generic_declaration_contract(id));
                    },
                }
            );
        }
    }
    auto generic_keys = std::set<std::pair<GenericDeclarationID, std::vector<TypeID>>>();

    for (auto generic_index = 0uz; generic_index < generic_instance_store.size(); ++generic_index) {
        const auto& instance = generic_instance_store[generic_index];
        const auto& definition = generic_declaration_contract(instance.definition);
        if (instance.arguments.size() != definition.parameters.size()
            || !generic_keys.emplace(instance.definition, instance.arguments).second
            || !generic_instance_nominals.emplace(instance.declaration, generic_index).second) {
            invariant_violation("published generic instances have noncanonical identities");
        }
        for (const auto argument : instance.arguments) {
            static_cast<void>(type_store.type(argument));
        }
        instance.declaration.visit(
            Overloaded {
                [&](StructID id) noexcept { static_cast<void>(declaration_store.structure(id)); },
                [&](EnumID id) noexcept { static_cast<void>(declaration_store.enumeration(id)); },
            }
        );
    }
    for (auto index = 0uz; index < static_instance_store.size(); ++index) {
        const auto& instance = static_instance_store[index];
        const auto& function = declaration_store.function(instance.function);
        const auto& source = declaration_store.callable(function.callable);
        const auto& signature = callable_signature_store.signature(source.signature);
        const auto& callable = declaration_store.callable(instance.callable);
        const auto& runtime = callable_signature_store.signature(callable.signature);
        const auto source_body = callable_body_id(source);
        const auto body = callable_body_id(callable);
        if (!source_body
            || !body
            || !signature.has_static_parameters()
            || runtime.has_static_parameters()
            || body_store.body(*body).specialized() != source_body) {
            invariant_violation("published instance does not specialize a staged function body");
        }
        auto argument = 0uz;
        auto parameter_index = 0uz;
        for (const auto& parameter : signature.parameters) {
            if (parameter.stage == ParameterStage::Static) {
                if (argument >= instance.arguments.size()
                    || constant_store.constant(instance.arguments[argument++]).type
                        != parameter.type) {
                    invariant_violation("published static arguments differ from their signature");
                }
            } else if (parameter_index >= runtime.parameters.size()
                       || runtime.parameters[parameter_index++].type != parameter.type) {
                invariant_violation("published instance signature differs from its function");
            }
        }
        if (argument != instance.arguments.size() || parameter_index != runtime.parameters.size()) {
            invariant_violation("published static instance has surplus arguments");
        }
        if (!static_instance_index
                 .emplace(std::pair(instance.function, instance.arguments), instance.callable)
                 .second
            || !instance_callables.emplace(instance.callable, index).second) {
            invariant_violation("published static instance key is duplicated");
        }
    }
    executed_bodies.assign(body_store.size(), true);
    for (const auto entry : body_store.entries()) {
        if (entry.value.kind() == BodyKind::ConstBlock) {
            executed_bodies[entry.id.index()] = false;
        }
    }
    for (const auto entry : test_store.entries()) {
        if (entry.value.is_const) {
            executed_bodies[entry.value.body->index()] = false;
        }
    }
    for (const auto entry : declaration_store.functions()) {
        const auto& callable = declaration_store.callable(entry.value.callable);
        const auto body = callable_body_id(callable);
        if (body
            && callable_signature_store.signature(callable.signature).has_static_parameters()) {
            executed_bodies[body->index()] = false;
        }
    }
    validate_resolved_storage(*this);
    contents = compute_type_contents(type_store, declaration_store);
}

auto SemIRProgram::type_contents(TypeID type) const noexcept -> const TypeContents& {
    static_cast<void>(type_store.type(type));
    return contents.at(type.index());
}

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

auto SemIRProgram::may_stop_test(const SemCall& call) const noexcept -> bool {
    return call.target ? may_stop_test(*call.target) : may_stop_test(call.callee->type.resolved());
}

auto SemIRProgram::call_signature(const SemCall& call) const noexcept -> CallableSignatureID {
    return call.target ? declaration_store.callable(*call.target).signature
                       : call_signature(call.callee->type.resolved());
}

auto SemIRProgram::call_signature(TypeID type) const noexcept -> CallableSignatureID {
    return type_store.type(type).value.visit(
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
        }
    );
}

auto BodyStore::owner() const noexcept -> ProgramIdentity {
    return rows.owner();
}

auto BodyStore::contains(BodyID id) const noexcept -> bool {
    return rows.contains(id) && rows.get(id) != nullptr;
}

auto BodyStore::body(BodyID id) const noexcept -> const SemIRBody& {
    if (!contains(id)) {
        invariant_violation("body lookup used an absent or foreign body identity");
    }
    return *rows.get(id);
}

auto BodyStore::size() const noexcept -> std::size_t {
    return static_cast<std::size_t>(std::ranges::distance(entries()));
}

BodyStore::BodyStore(ImmutableProgramTable<SemIRBody, BodyID> values) noexcept
    : rows([&]() noexcept {
          auto bodies = MutableProgramTable<std::unique_ptr<SemIRBody>, BodyID>(values.owner());
          for (auto& body : std::move(values).release()) {
              bodies.add(std::make_unique<SemIRBody>(std::move(body)));
          }
          return std::move(bodies).seal();
      }()) {}

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

auto SemIRProgram::generic_declaration_contracts() const noexcept
    -> std::span<const GenericDeclarationContract> {
    return generic_declaration_store;
}

auto SemIRProgram::generic_declaration_contract(GenericDeclarationID id) const noexcept
    -> const GenericDeclarationContract& {
    if (id.owner() != program_identity || id.index() >= generic_declaration_store.size()) {
        invariant_violation("published generic definition belongs to another program");
    }
    return generic_declaration_store[id.index()];
}

auto SemIRProgram::generic_nominal_instances() const noexcept
    -> std::span<const GenericNominalInstance> {
    return generic_instance_store;
}

auto SemIRProgram::generic_nominal_instance(NominalDeclarationRef declaration) const noexcept
    -> const GenericNominalInstance* {
    const auto found = generic_instance_nominals.find(declaration);
    return found == generic_instance_nominals.end() ? nullptr
                                                    : &generic_instance_store[found->second];
}

auto SemIRProgram::static_instances() const noexcept -> std::span<const StaticInstance> {
    return static_instance_store;
}

auto SemIRProgram::static_instance(CallableID callable) const noexcept -> const StaticInstance* {
    const auto found = instance_callables.find(callable);
    return found == instance_callables.end() ? nullptr : &static_instance_store[found->second];
}

auto SemIRProgram::source_function(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    if (const auto* instance = static_instance(callable)) {
        return instance->function;
    }
    return declaration_store.function_for_callable(callable);
}

auto SemIRProgram::definition_placement(CallableID callable) const noexcept -> DefinitionPlacement {
    if (static_instance(callable) != nullptr) {
        return DefinitionPlacement::Use;
    }
    const auto signature = declaration_store.callable(callable).signature;
    return callable_signature_store.signature(signature).has_static_parameters()
        ? DefinitionPlacement::None
        : DefinitionPlacement::Owner;
}

auto SemIRProgram::executes(BodyID body) const noexcept -> bool {
    static_cast<void>(body_store.body(body));
    return executed_bodies.at(body.index());
}

auto SemIRProgram::find_static_instance(
    FunctionID function,
    std::span<const ConstantID> arguments
) const noexcept -> std::optional<CallableID> {
    const auto found = static_instance_index.find(
        std::pair(function, std::vector<ConstantID>(arguments.begin(), arguments.end()))
    );
    return found == static_instance_index.end() ? std::nullopt : std::optional(found->second);
}

auto SemIRProgram::is_staged(FunctionID function) const noexcept -> bool {
    const auto& declaration = declaration_store.function(function);
    const auto& callable = declaration_store.callable(declaration.callable);
    return callable_signature_store.signature(callable.signature).has_static_parameters();
}
