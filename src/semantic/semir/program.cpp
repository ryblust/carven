module carven:semantic.semir.program.impl;

import :semantic.semir.async;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.passing;
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

auto SemIRProgram::publish_read_passing_facts(ReadPassingFacts facts) noexcept -> void {
    for (const auto& [callable, parameters] : facts.stable_parameters) {
        const auto& declaration = declaration_store.callable(callable);
        const auto& signature = callable_signature_store.signature(declaration.signature);
        if (parameters.size() != signature.parameters.size()) {
            invariant_violation("Read passing facts disagree with callable parameter arity");
        }
        for (const auto [index, stable] : std::views::enumerate(parameters)) {
            if (stable && signature.parameters[index].access != AccessMode::Read) {
                invariant_violation("retained Read storage requires a Read parameter");
            }
        }
    }
    read_passing_facts = std::move(facts);
}

auto SemIRProgram::read_requires_stable_storage(
    CallableID callable,
    std::size_t parameter
) const noexcept -> bool {
    const auto& declaration = declaration_store.callable(callable);
    const auto& signature = callable_signature_store.signature(declaration.signature);
    if (parameter >= signature.parameters.size()) {
        invariant_violation("Read passing query used an unknown parameter");
    }
    const auto found = read_passing_facts.stable_parameters.find(callable);
    return found != read_passing_facts.stable_parameters.end() && found->second[parameter];
}

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
    std::vector<GenericFunctionInstance> generic_functions,
    std::vector<CallStopEffect> call_stops
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
      generic_declaration_store(identity, std::move(generic_declarations)),
      generic_instance_store(std::move(generic_instances)),
      generic_function_store(std::move(generic_functions)),
      call_stops(std::move(call_stops)) {
    for (const auto entry : generic_declaration_store.entries()) {
        const auto& contract = entry.value;

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
        for (const auto type : contract.implementation_dependencies.types) {
            static_cast<void>(type_store.type(type));
        }
        for (const auto callable : contract.implementation_dependencies.callables) {
            static_cast<void>(declaration_store.callable(callable));
        }
        for (const auto definition : contract.implementation_dependencies.definitions) {
            static_cast<void>(generic_declaration_contract(definition));
        }
    }
    auto generic_keys = std::set<std::pair<GenericDeclarationID, CanonicalGenericEnvironment>>();

    for (auto generic_index = 0uz; generic_index < generic_instance_store.size(); ++generic_index) {
        const auto& instance = generic_instance_store[generic_index];
        const auto& definition = generic_declaration_contract(instance.definition);
        if (instance.arguments.size() != definition.parameters.size()
            || !generic_keys.emplace(instance.definition, instance.arguments).second
            || !generic_instance_nominals.emplace(instance.declaration, generic_index).second) {
            invariant_violation("published generic instances have noncanonical identities");
        }
        for (const auto [index, argument] : std::views::enumerate(instance.arguments)) {
            if (!generic_argument_matches(definition.parameter_kinds[index], argument)) {
                invariant_violation("published generic argument has a mismatched parameter domain");
            }
            argument.visit([&](const auto& value) noexcept {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<T, TypeID>) {
                    static_cast<void>(type_store.type(value));
                } else if constexpr (std::same_as<T, FailureSetID>) {
                    static_cast<void>(failure_set_store.failure_set(value));
                }
            });
        }
        instance.declaration.visit(
            Overloaded {
                [&](StructID id) noexcept { static_cast<void>(declaration_store.structure(id)); },
                [&](EnumID id) noexcept { static_cast<void>(declaration_store.enumeration(id)); },
            }
        );
    }
    for (auto index = 0uz; index < generic_function_store.size(); ++index) {
        const auto& instance = generic_function_store[index];
        const auto& definition = generic_declaration_contract(instance.definition);
        const auto function = declaration_store.function_for_callable(instance.callable);
        if (!function
            || declaration_store.function(*function).module_id != definition.module_id
            || instance.arguments.size() != definition.parameters.size()
            || !generic_keys.emplace(instance.definition, instance.arguments).second
            || !generic_instance_functions.emplace(instance.callable, index).second) {
            invariant_violation(
                "published generic function instances have noncanonical identities"
            );
        }
        for (const auto [parameter, argument] : std::views::enumerate(instance.arguments)) {
            if (!generic_argument_matches(definition.parameter_kinds[parameter], argument)) {
                invariant_violation(
                    "published generic function argument has a mismatched parameter domain"
                );
            }
            argument.visit([&](const auto value) noexcept {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, TypeID>) {
                    static_cast<void>(type_store.type(value));
                } else {
                    static_cast<void>(failure_set_store.failure_set(value));
                }
            });
        }
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

auto SemIRProgram::may_complete_cancelled(CallableID callable) const noexcept -> bool {
    static_cast<void>(declaration_store.callable(callable));
    if (!cancellation_facts) {
        invariant_violation("cancellation query requires a published program");
    }
    return cancellation_facts->callable_completion.at(callable.index());
}

auto SemIRProgram::await_completion_may_be_cancelled(
    BodyID body,
    const SemAwait& occurrence
) const noexcept -> bool {
    static_cast<void>(body_store.body(body));
    if (!cancellation_facts) {
        invariant_violation("await cancellation query requires a published program");
    }
    const auto found_body = cancellation_facts->await_completion.find(body);
    if (found_body == cancellation_facts->await_completion.end()) {
        invariant_violation("await cancellation query has no published body facts");
    }
    const auto found = found_body->second.find(&occurrence);
    if (found == found_body->second.end()) {
        invariant_violation("await cancellation query used a foreign occurrence");
    }
    return found->second;
}

auto SemIRProgram::may_stop_test(CallableID callable_id) const noexcept -> bool {
    static_cast<void>(declaration_store.callable(callable_id));
    return includes_test_stop(call_stops.at(callable_id.index()));
}

auto SemIRProgram::may_stop(CallableID callable_id) const noexcept -> bool {
    static_cast<void>(declaration_store.callable(callable_id));
    return call_stops.at(callable_id.index()) != CallStopEffect::None;
}

auto SemIRProgram::may_stop(TypeID type) const noexcept -> bool {
    const auto& value = type_store.type(type).value;
    if (const auto* function = std::get_if<FunctionTypeValue>(&value)) {
        return may_stop(function->callable);
    }
    if (const auto* closure = std::get_if<ClosureTypeValue>(&value)) {
        return may_stop(closure->callable);
    }
    if (std::holds_alternative<CallableViewTypeValue>(value)) {
        return true;
    }
    invariant_violation("stop query requires a callable type");
}

auto SemIRProgram::may_stop(const SemCall& call) const noexcept -> bool {
    return call.target ? may_stop(*call.target) : may_stop(call.callee->type.resolved());
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

namespace {

auto source_type_identity(
    const SemIRProgram& program,
    TypeID type,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string>;

auto source_module_name(const SemIRProgram& program, ModuleID id) noexcept -> std::string_view {
    return program.provenance()
        .module_record(program.declarations().module_decl(id).provenance_module)
        .path.value();
}

auto source_failure_identity(
    const SemIRProgram& program,
    FailureSetID failures,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    auto ordered = std::vector<std::pair<std::string, TypeID>>();
    for (const auto member : program.failure_sets().failure_set(failures).members) {
        auto plain_seen = std::set<NominalDeclarationRef>();
        auto name = source_type_identity(program, member, false, plain_seen);
        if (!name) {
            return std::nullopt;
        }
        ordered.emplace_back(std::move(*name), member);
    }
    std::ranges::sort(ordered, {}, &std::pair<std::string, TypeID>::first);
    auto result = std::string("throw(");
    for (const auto [index, entry] : std::views::enumerate(ordered)) {
        if (index != 0) {
            result += ',';
        }
        auto name = source_type_identity(program, entry.second, fields, seen);
        if (!name) {
            return std::nullopt;
        }
        result += *name;
    }
    return result + ')';
}

auto source_callable_identity(
    const SemIRProgram& program,
    CallableSignatureID id,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    const auto& signature = program.callable_signatures().signature(id);
    auto result =
        std::string(signature.execution == CallableExecutionKind::Async ? "async fn(" : "fn(");
    for (const auto [index, parameter] : std::views::enumerate(signature.parameters)) {
        if (index != 0) {
            result += ',';
        }
        if (parameter.stage == ParameterStage::Static) {
            result += "const ";
        }
        if (parameter.access == AccessMode::Write) {
            result += '&';
        }
        if (parameter.access == AccessMode::Take) {
            result += "&&";
        }
        auto name = source_type_identity(program, parameter.type, fields, seen);
        if (!name) {
            return std::nullopt;
        }
        result += *name;
    }
    auto value = source_type_identity(program, signature.result, fields, seen);
    auto failures = source_failure_identity(program, signature.failures, fields, seen);
    if (!value || !failures) {
        return std::nullopt;
    }
    return result + ")->" + *value + ";" + *failures;
}

auto source_native_identity(
    const SemIRProgram& program,
    const CppNamedType& named,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    auto result = named.name.lookup == CppNameLookup::ModuleScope
        ? "[" + std::string(source_module_name(program, named.name.context_module)) + "]::"
        : std::string();
    for (const auto& component : named.name.components) {
        if (!result.empty() && !result.ends_with("::")) {
            result += "::";
        }
        result += component;
    }
    if (!named.arguments.empty()) {
        result += '<';
        for (const auto [index, argument] : std::views::enumerate(named.arguments)) {
            if (index != 0) {
                result += ',';
            }
            auto name = source_type_identity(program, argument, fields, seen);
            if (!name) {
                return std::nullopt;
            }
            result += *name;
        }
        result += '>';
    }
    return result;
}

auto source_nominal_identity(
    const SemIRProgram& program,
    NominalDeclarationRef nominal,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    auto name = nominal.visit([&](auto id) noexcept {
        const auto& declaration = [&]() noexcept -> const auto& {
            if constexpr (std::same_as<decltype(id), StructID>) {
                return program.declarations().structure(id);
            } else {
                return program.declarations().enumeration(id);
            }
        }();
        return std::string(source_module_name(program, declaration.module_id)) + "."
            + std::string(program.provenance().spelling(declaration.name));
    });
    if (const auto* instance = program.generic_nominal_instance(nominal)) {
        const auto& declaration = program.generic_declaration_contract(instance->definition);
        name = std::string(source_module_name(program, declaration.module_id)) + "."
            + std::string(program.provenance().spelling(declaration.name)) + '<';
        auto plain_seen = std::set<NominalDeclarationRef>();
        for (const auto [index, argument] : std::views::enumerate(instance->arguments)) {
            if (index != 0) {
                name += ',';
            }
            auto nested = argument.visit([&](const auto id) noexcept {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(id)>, TypeID>) {
                    return source_type_identity(program, id, false, plain_seen);
                } else {
                    return source_failure_identity(program, id, false, plain_seen);
                }
            });
            if (!nested) {
                return std::nullopt;
            }
            name += *nested;
        }
        name += '>';
    }
    if (!fields || !seen.insert(nominal).second) {
        return name;
    }
    return nominal.visit([&](auto id) noexcept -> std::optional<std::string> {
        if constexpr (std::same_as<decltype(id), StructID>) {
            const auto& declaration = program.declarations().structure(id);
            name += declaration.kind == RecordKind::Struct ? "{struct:" : "{class:";
            for (const auto& field : declaration.fields) {
                auto child = source_type_identity(program, field.type, true, seen);
                if (!child) {
                    return std::nullopt;
                }
                name += std::string(program.provenance().spelling(field.name)) + ":" + *child + ";";
            }
        } else {
            const auto& declaration = program.declarations().enumeration(id);
            name += "{enum:";
            if (const auto* numeric =
                    std::get_if<NumericEnumRepresentation>(&declaration.representation)) {
                auto underlying =
                    source_type_identity(program, numeric->underlying_type, true, seen);
                if (!underlying) {
                    return std::nullopt;
                }
                name += *underlying;
            }
            name += declaration.supports_equality ? ";equal;" : ";opaque;";
            for (const auto case_id : declaration.cases) {
                const auto& member = program.declarations().enum_case(case_id);
                name += std::string(program.provenance().spelling(member.name)) + '(';
                for (const auto payload : member.payload_types) {
                    auto child = source_type_identity(program, payload, true, seen);
                    if (!child) {
                        return std::nullopt;
                    }
                    name += *child + ";";
                }
                name += ')';
                if (member.constant
                    && std::holds_alternative<NumericEnumRepresentation>(
                        declaration.representation
                    )) {
                    const auto* value = std::get_if<NumericEnumConstant>(
                        &program.constants().constant(*member.constant).value
                    );
                    if (value == nullptr) {
                        return std::nullopt;
                    }
                    name += '=';
                    if (value->value.negative()) {
                        name += '-';
                    }
                    name += std::to_string(value->value.magnitude());
                }
                name += ';';
            }
        }
        return name + '}';
    });
}

auto source_type_identity(
    const SemIRProgram& program,
    TypeID type,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    const auto unary = [&](std::string_view prefix,
                           TypeID element,
                           std::string_view suffix) noexcept -> std::optional<std::string> {
        auto name = source_type_identity(program, element, fields, seen);
        if (!name) {
            return std::nullopt;
        }
        name->insert(0uz, prefix);
        name->append(suffix);
        return name;
    };
    return program.types().type(type).value.visit(
        [&](const auto& shape) noexcept -> std::optional<std::string> {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
                return std::string(builtin_name(shape.kind));
            } else if constexpr (std::same_as<Shape, StructTypeValue>) {
                return source_nominal_identity(
                    program,
                    NominalDeclarationRef(shape.structure),
                    fields,
                    seen
                );
            } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
                return source_nominal_identity(
                    program,
                    NominalDeclarationRef(shape.enumeration),
                    fields,
                    seen
                );
            } else if constexpr (std::same_as<Shape, CppTypeValue>) {
                if (const auto* named = std::get_if<CppNamedType>(&shape.form)) {
                    auto name = source_native_identity(program, *named, fields, seen);
                    if (!name) {
                        return std::nullopt;
                    }
                    return "::" + *name;
                }
                if (std::holds_alternative<CppConstCharPointerType>(shape.form)) {
                    return "const char*";
                }
                return std::nullopt;
            } else if constexpr (std::same_as<Shape, VectorTypeValue>) {
                return unary("Vector<", shape.element, ">");
            } else if constexpr (std::same_as<Shape, RangeTypeValue>) {
                return unary("Range<", shape.element, ">");
            } else if constexpr (std::same_as<Shape, SliceTypeValue>) {
                return unary("[", shape.element, "]");
            } else if constexpr (std::same_as<Shape, ArrayTypeValue>) {
                return unary("[", shape.element, ";" + std::to_string(shape.extent) + "]");
            } else if constexpr (std::same_as<Shape, PointerTypeValue>) {
                return unary(
                    shape.access == PointerAccess::Write ? "ptr<&" : "ptr<",
                    shape.target,
                    ">"
                );
            } else if constexpr (std::same_as<Shape, TupleTypeValue>) {
                auto name = std::string("(");
                for (const auto [index, element] : std::views::enumerate(shape.elements)) {
                    if (index != 0) {
                        name += ',';
                    }
                    auto nested = source_type_identity(program, element, fields, seen);
                    if (!nested) {
                        return std::nullopt;
                    }
                    name += *nested;
                }
                if (shape.elements.size() == 1uz) {
                    name += ',';
                }
                return name + ')';
            } else if constexpr (std::same_as<Shape, OperationTypeValue>) {
                auto success = source_type_identity(program, shape.success, fields, seen);
                auto failures = source_failure_identity(program, shape.failures, fields, seen);
                if (!success || !failures) {
                    return std::nullopt;
                }
                return "Operation<" + *success + ";" + *failures + ">";
            } else if constexpr (std::same_as<Shape, CallableViewTypeValue>) {
                return source_callable_identity(program, shape.signature, fields, seen);
            } else {
                static_assert(
                    std::same_as<Shape, FunctionTypeValue> || std::same_as<Shape, ClosureTypeValue>
                );
                return std::nullopt;
            }
        }
    );
}

} // namespace

auto SemIRProgram::native_type_name(TypeID type) const noexcept -> std::optional<std::string> {
    const auto* native = std::get_if<CppTypeValue>(&types().type(type).value);
    const auto* named = native ? std::get_if<CppNamedType>(&native->form) : nullptr;
    if (named == nullptr) {
        return std::nullopt;
    }
    auto seen = std::set<NominalDeclarationRef>();
    return source_native_identity(*this, *named, false, seen);
}

auto SemIRProgram::callable_contract_identity(CallableSignatureID signature) const noexcept
    -> std::optional<std::string> {
    auto seen = std::set<NominalDeclarationRef>();
    return source_callable_identity(*this, signature, true, seen);
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
    -> IDTableEntries<GenericDeclarationID, GenericDeclarationContract, ProgramIdentity> {
    return generic_declaration_store.entries();
}

auto SemIRProgram::generic_declaration_contract(GenericDeclarationID id) const noexcept
    -> const GenericDeclarationContract& {
    return generic_declaration_store.get(id);
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

auto SemIRProgram::generic_function_instances() const noexcept
    -> std::span<const GenericFunctionInstance> {
    return generic_function_store;
}

auto SemIRProgram::generic_function_instance(CallableID callable) const noexcept
    -> const GenericFunctionInstance* {
    const auto found = generic_instance_functions.find(callable);
    return found == generic_instance_functions.end() ? nullptr
                                                     : &generic_function_store[found->second];
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

auto SemIRProgram::is_source_template(CallableID callable) const noexcept -> bool {
    const auto signature = declaration_store.callable(callable).signature;
    return static_instance(callable) == nullptr
        && callable_signature_store.signature(signature).has_static_parameters();
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
