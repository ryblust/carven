module carven:semantic.semir.publication.impl;

import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace {

template<typename Range>
auto range_size(Range range) noexcept -> std::size_t {
    return static_cast<std::size_t>(std::ranges::distance(range));
}

auto claim_once(
    std::vector<std::uint8_t>& claims,
    std::uint32_t index,
    std::string_view duplicate_fact
) noexcept -> void {
    if (static_cast<std::size_t>(index) >= claims.size()) {
        invariant_violation("publication topology used an invalid identity");
    }
    if (claims[index] != 0u) {
        invariant_violation(duplicate_fact);
    }
    claims[index] = 1u;
}

auto require_complete(std::span<const std::uint8_t> claims, std::string_view missing_fact) noexcept
    -> void {
    if (!std::ranges::all_of(claims, [](std::uint8_t claim) static noexcept {
            return claim == 1u;
        })) {
        invariant_violation(missing_fact);
    }
}

auto validate_publication_topology(
    ProgramIdentity owner,
    CompilationProvenanceReader provenance,
    const DeclarationStore& declarations,
    const BodyStore& bodies,
    const TestStore& tests
) noexcept -> void {
    if (declarations.owner() != owner || bodies.owner() != owner || tests.owner() != owner) {
        invariant_violation("published semantic stores do not share one program owner");
    }

    const auto module_count = range_size(declarations.modules());
    const auto function_count = range_size(declarations.functions());
    const auto structure_count = range_size(declarations.structures());
    const auto enum_count = range_size(declarations.enumerations());
    const auto enum_case_count = range_size(declarations.enum_cases());
    const auto constant_count = range_size(declarations.module_constants());
    const auto callable_count = range_size(declarations.callables());
    if (module_count != provenance.module_count()) {
        invariant_violation("published modules do not close the provenance module domain");
    }

    auto named_callables = std::vector<std::uint8_t>(callable_count, 0u);
    for (const auto [function_id, declaration] : declarations.functions()) {
        static_cast<void>(function_id);
        if (!declarations.contains(declaration.module_id)) {
            invariant_violation("function declaration used an invalid module");
        }
        if (!declarations.contains(declaration.callable)) {
            invariant_violation("function declaration used an invalid callable");
        }
        claim_once(
            named_callables,
            declaration.callable.index(),
            "callable was assigned to more than one function declaration"
        );
    }
    for (const auto [structure_id, declaration] : declarations.structures()) {
        static_cast<void>(structure_id);
        if (!declarations.contains(declaration.module_id)) {
            invariant_violation("struct declaration used an invalid module");
        }
    }
    for (const auto [enum_id, declaration] : declarations.enumerations()) {
        static_cast<void>(enum_id);
        if (!declarations.contains(declaration.module_id)) {
            invariant_violation("enum declaration used an invalid module");
        }
    }
    for (const auto [constant_id, declaration] : declarations.module_constants()) {
        static_cast<void>(constant_id);
        if (!declarations.contains(declaration.module_id)) {
            invariant_violation("module constant declaration used an invalid module");
        }
    }
    for (const auto [test_id, declaration] : tests.entries()) {
        static_cast<void>(test_id);
        if (!declarations.contains(declaration.module_id)) {
            invariant_violation("test declaration used an invalid module");
        }
        if (!bodies.contains(declaration.body)) {
            invariant_violation("test declaration used an invalid body");
        }
        if (bodies.body(declaration.body).kind() != BodyKind::Test) {
            invariant_violation("test declaration used a non-test body");
        }
    }

    auto enum_case_claims = std::vector<std::uint8_t>(enum_case_count, 0u);
    for (const auto [case_id, declaration] : declarations.enum_cases()) {
        static_cast<void>(case_id);
        if (!declarations.contains(declaration.owner)) {
            invariant_violation("enum case declaration used an invalid owner");
        }
    }
    for (const auto [enum_id, declaration] : declarations.enumerations()) {
        for (const auto case_id : declaration.cases) {
            if (!declarations.contains(case_id)) {
                invariant_violation("enum declaration used an invalid case");
            }
            if (declarations.enum_case(case_id).owner != enum_id) {
                invariant_violation("enum declaration listed a case owned by another enum");
            }
            claim_once(enum_case_claims, case_id.index(), "enum case was listed more than once");
        }
    }
    require_complete(enum_case_claims, "enum case was absent from its owner case list");

    auto function_claims = std::vector<std::uint8_t>(function_count, 0u);
    auto structure_claims = std::vector<std::uint8_t>(structure_count, 0u);
    auto enum_claims = std::vector<std::uint8_t>(enum_count, 0u);
    auto constant_claims = std::vector<std::uint8_t>(constant_count, 0u);
    auto test_claims = std::vector<std::uint8_t>(tests.size(), 0u);
    auto provenance_module_claims = std::vector<std::uint8_t>(module_count, 0u);
    for (const auto [module_id, declaration] : declarations.modules()) {
        if (!provenance.contains(declaration.provenance_module)) {
            invariant_violation("semantic module used an invalid provenance module");
        }
        claim_once(
            provenance_module_claims,
            declaration.provenance_module.index(),
            "provenance module was assigned to more than one semantic module"
        );
        for (const auto& item : declaration.items) {
            std::visit(
                [&](const auto item_id) noexcept {
                    using ID = std::remove_cvref_t<decltype(item_id)>;
                    if constexpr (std::same_as<ID, FunctionID>) {
                        if (!declarations.contains(item_id)) {
                            invariant_violation("module listed an invalid function");
                        }
                        if (declarations.function(item_id).module_id != module_id) {
                            invariant_violation("function disagreed with its containing module");
                        }
                        claim_once(
                            function_claims,
                            item_id.index(),
                            "function was listed by more than one module item"
                        );
                    } else if constexpr (std::same_as<ID, StructID>) {
                        if (!declarations.contains(item_id)) {
                            invariant_violation("module listed an invalid structure");
                        }
                        if (declarations.structure(item_id).module_id != module_id) {
                            invariant_violation("structure disagreed with its containing module");
                        }
                        claim_once(
                            structure_claims,
                            item_id.index(),
                            "structure was listed by more than one module item"
                        );
                    } else if constexpr (std::same_as<ID, EnumID>) {
                        if (!declarations.contains(item_id)) {
                            invariant_violation("module listed an invalid enum");
                        }
                        if (declarations.enumeration(item_id).module_id != module_id) {
                            invariant_violation("enum disagreed with its containing module");
                        }
                        claim_once(
                            enum_claims,
                            item_id.index(),
                            "enum was listed by more than one module item"
                        );
                    } else if constexpr (std::same_as<ID, ModuleConstantID>) {
                        if (!declarations.contains(item_id)) {
                            invariant_violation("module listed an invalid module constant");
                        }
                        if (declarations.module_constant(item_id).module_id != module_id) {
                            invariant_violation(
                                "module constant disagreed with its containing module"
                            );
                        }
                        claim_once(
                            constant_claims,
                            item_id.index(),
                            "module constant was listed by more than one module item"
                        );
                    } else if constexpr (std::same_as<ID, TestID>) {
                        if (!tests.contains(item_id)) {
                            invariant_violation("module listed an invalid test");
                        }
                        if (tests.test(item_id).module_id != module_id) {
                            invariant_violation("test disagreed with its containing module");
                        }
                        claim_once(
                            test_claims,
                            item_id.index(),
                            "test was listed by more than one module item"
                        );
                    } else {
                        static_assert(std::same_as<ID, void>);
                    }
                },
                item
            );
        }
    }
    require_complete(
        provenance_module_claims,
        "provenance module had no semantic module declaration"
    );
    require_complete(function_claims, "function was absent from every module item list");
    require_complete(structure_claims, "structure was absent from every module item list");
    require_complete(enum_claims, "enum was absent from every module item list");
    require_complete(constant_claims, "module constant was absent from every module item list");
    require_complete(test_claims, "test was absent from every module item list");

    auto closure_site_claims = std::vector<std::uint8_t>(callable_count, 0u);
    for (const auto [body_id, body] : bodies.entries()) {
        static_cast<void>(body_id);
        visit_semantic_nodes(body.region(), [&](const SemIRExpression& expression) noexcept {
            const auto* closure = std::get_if<SemClosure<TypeID, FailureSetID>>(&expression.value);
            if (closure == nullptr) {
                return;
            }
            if (!declarations.contains(closure->callable)) {
                invariant_violation("closure expression used an invalid callable");
            }
            if (!std::holds_alternative<ClosureBodyImplementation>(
                    declarations.callable(closure->callable).implementation
                )) {
                invariant_violation("closure expression used a non-closure callable");
            }
            claim_once(
                closure_site_claims,
                closure->callable.index(),
                "closure callable was assigned to more than one construction site"
            );
        });
    }

    auto body_claims = std::vector<std::uint8_t>(bodies.size(), 0u);
    for (const auto [callable_id, declaration] : declarations.callables()) {
        const auto named = named_callables[callable_id.index()] == 1u;
        std::visit(
            Overloaded {
                [&](const FunctionBodyImplementation& implementation) noexcept {
                    if (!named) {
                        invariant_violation(
                            "function body implementation had no function declaration"
                        );
                    }
                    if (!bodies.contains(implementation.body)) {
                        invariant_violation("function implementation used an invalid body");
                    }
                    if (bodies.body(implementation.body).kind() != BodyKind::Function) {
                        invariant_violation("function implementation used a non-function body");
                    }
                    claim_once(
                        body_claims,
                        implementation.body.index(),
                        "body was assigned to more than one declaration"
                    );
                },
                [&](const ClosureBodyImplementation& implementation) noexcept {
                    if (named) {
                        invariant_violation(
                            "function declaration used a closure body implementation"
                        );
                    }
                    if (closure_site_claims[callable_id.index()] != 1u) {
                        invariant_violation("closure callable had no closure operation");
                    }
                    if (!bodies.contains(implementation.body)) {
                        invariant_violation("closure implementation used an invalid body");
                    }
                    if (bodies.body(implementation.body).kind() != BodyKind::Closure) {
                        invariant_violation("closure implementation used a non-closure body");
                    }
                    claim_once(
                        body_claims,
                        implementation.body.index(),
                        "body was assigned to more than one declaration"
                    );
                },
                [&](const CppImportImplementation&) noexcept {
                    if (!named) {
                        invariant_violation("C++ import callable had no function declaration");
                    }
                },
            },
            declaration.implementation
        );
    }
    for (const auto [test_id, declaration] : tests.entries()) {
        static_cast<void>(test_id);
        claim_once(
            body_claims,
            declaration.body.index(),
            "body was assigned to more than one declaration"
        );
    }
    require_complete(body_claims, "published body had no callable or test declaration");
}

auto validate_publication_facts(
    ProgramIdentity owner,
    CompilationProvenanceReader provenance,
    const CanonicalTypeStore& types,
    const ConstantStore& constants,
    const FailureSetStore& failure_sets,
    const CallableSignatureStore& callable_signatures,
    const DeclarationStore& declarations
) noexcept -> void {
    if (types.owner() != owner
        || constants.owner() != owner
        || failure_sets.owner() != owner
        || callable_signatures.owner() != owner
        || declarations.owner() != owner) {
        invariant_violation("published semantic fact stores do not share one program owner");
    }

    for (const auto [type_id, type] : types.entries()) {
        static_cast<void>(type_id);
        std::visit(
            Overloaded {
                [&](const CppTypeValue& value) noexcept {
                    if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                        if (!declarations.contains(named->name.context_module)
                            || !valid_cpp_name(named->name)) {
                            invariant_violation("C++ type used an invalid binding");
                        }
                        for (const auto argument : named->arguments) {
                            if (!types.contains(argument)) {
                                invariant_violation("C++ type used an unpublished type argument");
                            }
                        }
                    } else {
                        const auto& query = std::get<CppDeducedType>(value.form);
                        if (!provenance.contains(query.origin)
                            || !cpp_operation_accepts_arity(query.operation, query.operands.size())
                            || std::holds_alternative<CppUpdateOperation>(query.operation)
                            || std::holds_alternative<CppConstructOperation>(query.operation)
                            || std::holds_alternative<CppConvertOperation>(query.operation)) {
                            invariant_violation("C++ query has an invalid derivation");
                        }
                        if (const auto* name = std::get_if<CppNameOperation>(&query.operation);
                            name != nullptr && !declarations.contains(name->name.context_module)) {
                            invariant_violation("C++ query has an unpublished module");
                        }
                        for (const auto& operand : query.operands) {
                            if (!types.contains(operand.type)) {
                                invariant_violation("C++ query used an unpublished operand type");
                            }
                        }
                    }
                },
                [](const BuiltinTypeValue&) static noexcept {},
                [&](const StructTypeValue& value) noexcept {
                    if (!declarations.contains(value.structure)) {
                        invariant_violation("canonical type used an unpublished structure");
                    }
                },
                [&](const EnumTypeValue& value) noexcept {
                    if (!declarations.contains(value.enumeration)) {
                        invariant_violation("canonical type used an unpublished enum");
                    }
                },
                [&](const ArrayTypeValue& value) noexcept {
                    if (!types.contains(value.element)) {
                        invariant_violation("canonical array used an unpublished element type");
                    }
                },
                [&](const FunctionTypeValue& value) noexcept {
                    if (!declarations.contains(value.callable)) {
                        invariant_violation("function type used an unpublished callable");
                    }
                },
                [&](const ClosureTypeValue& value) noexcept {
                    if (!declarations.contains(value.callable)
                        || !std::holds_alternative<ClosureBodyImplementation>(
                            declarations.callable(value.callable).implementation
                        )) {
                        invariant_violation("closure type used a non-closure callable");
                    }
                },
                [&](const CallableViewTypeValue& value) noexcept {
                    if (!callable_signatures.contains(value.signature)) {
                        invariant_violation("callable-view type used an unpublished signature");
                    }
                },
            },
            type.value
        );
    }

    for (const auto [failure_id, failure_set] : failure_sets.entries()) {
        static_cast<void>(failure_id);
        for (const auto member : failure_set.members) {
            if (!types.contains(member)) {
                invariant_violation("failure set used an unpublished member type");
            }
            const auto& type = types.type(member).value;
            if (!std::holds_alternative<StructTypeValue>(type)
                && !std::holds_alternative<EnumTypeValue>(type)) {
                invariant_violation("failure set used a non-nominal member type");
            }
        }
    }

    for (const auto [signature_id, signature] : callable_signatures.entries()) {
        static_cast<void>(signature_id);
        if (!types.contains(signature.result) || !failure_sets.contains(signature.failures)) {
            invariant_violation("callable signature used an unpublished result fact");
        }
        for (const auto& parameter : signature.parameters) {
            if (!types.contains(parameter.type)) {
                invariant_violation("callable signature used an unpublished parameter type");
            }
        }
    }

    for (const auto [constant_id, fact] : constants.entries()) {
        static_cast<void>(constant_id);
        if (!types.contains(fact.type)) {
            invariant_violation("constant used an unpublished type");
        }
        const auto& canonical = types.type(fact.type).value;
        const auto valid = std::visit(
            Overloaded {
                [&](const IntegerConstant& value) noexcept {
                    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical);
                    return builtin != nullptr
                        && builtin_is_integer(builtin->kind)
                        && integer_constant_fits(value, builtin->kind);
                },
                [&](const BooleanConstant&) noexcept {
                    return canonical == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}};
                },
                [&](const StringConstant& value) noexcept {
                    return provenance.contains(value.value)
                        && canonical == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Str}};
                },
                [&](const F32Constant&) noexcept {
                    return canonical == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::F32}};
                },
                [&](const F64Constant&) noexcept {
                    return canonical == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::F64}};
                },
                [&](const CharacterConstant& value) noexcept {
                    return canonical == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Char}}
                    && value.scalar <= 0x10ffffu
                        && (value.scalar < 0xd800u || value.scalar > 0xdfffu);
                },
                [&](const NumericEnumConstant& value) noexcept {
                    const auto* enumeration = std::get_if<EnumTypeValue>(&canonical);
                    return enumeration != nullptr
                        && declarations.contains(value.enum_case)
                        && declarations.enum_case(value.enum_case).owner
                        == enumeration->enumeration;
                },
                [&](const PayloadEnumConstant& value) noexcept {
                    const auto* enumeration = std::get_if<EnumTypeValue>(&canonical);
                    if (enumeration == nullptr || !declarations.contains(value.enum_case)) {
                        return false;
                    }
                    const auto& enum_case = declarations.enum_case(value.enum_case);
                    if (enum_case.owner != enumeration->enumeration
                        || enum_case.payload_types.size() != value.payload.size()) {
                        return false;
                    }
                    for (const auto [child, expected] :
                         std::views::zip(value.payload, enum_case.payload_types)) {
                        if (!constants.contains(child)
                            || constants.constant(child).type != expected) {
                            return false;
                        }
                    }
                    return true;
                },
            },
            fact.value
        );
        if (!valid) {
            invariant_violation("constant value differs from its canonical type");
        }
    }

    for (const auto [structure_id, declaration] : declarations.structures()) {
        static_cast<void>(structure_id);
        for (const auto& field : declaration.fields) {
            if (!types.contains(field.type)) {
                invariant_violation("structure field used an unpublished type");
            }
        }
    }
    for (const auto [enum_id, declaration] : declarations.enumerations()) {
        std::visit(
            Overloaded {
                [](const PayloadEnumRepresentation&) static noexcept {},
                [&](const NumericEnumRepresentation& representation) noexcept {
                    if (!types.contains(representation.underlying_type)) {
                        invariant_violation("numeric enum used an unpublished representation type");
                    }
                    const auto* builtin = std::get_if<BuiltinTypeValue>(
                        &types.type(representation.underlying_type).value
                    );
                    if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
                        invariant_violation("numeric enum representation is not an integer type");
                    }
                },
            },
            declaration.representation
        );
        for (const auto case_id : declaration.cases) {
            const auto& enum_case = declarations.enum_case(case_id);
            for (const auto payload : enum_case.payload_types) {
                if (!types.contains(payload)) {
                    invariant_violation("enum payload used an unpublished type");
                }
            }
            if (enum_case.constant.has_value()) {
                if (!constants.contains(*enum_case.constant)) {
                    invariant_violation("enum case used an unpublished constant");
                }
                const auto& constant = constants.constant(*enum_case.constant);
                const auto* constant_type =
                    std::get_if<EnumTypeValue>(&types.type(constant.type).value);
                if (constant_type == nullptr || constant_type->enumeration != enum_id) {
                    invariant_violation("enum case constant has the wrong nominal type");
                }
            }
        }
    }
    for (const auto [constant_id, declaration] : declarations.module_constants()) {
        static_cast<void>(constant_id);
        if (!types.contains(declaration.type)
            || !constants.contains(declaration.value)
            || constants.constant(declaration.value).type != declaration.type) {
            invariant_violation("module constant declaration has an unpublished fact");
        }
    }
    for (const auto [callable_id, declaration] : declarations.callables()) {
        static_cast<void>(callable_id);
        if (!callable_signatures.contains(declaration.signature)) {
            invariant_violation("callable declaration used an unpublished signature");
        }
    }
}

} // namespace

auto ProgramDraft::seal() && noexcept -> SemIRProgram {
    require_state(State::Solved, "seal semantic program");
    if (!body_slots.all_defined() || !test_slots.all_defined()) {
        invariant_violation("semantic program was sealed with unfinished bodies or tests");
    }
    auto declaration_store = std::move(declarations).seal(*resolved_types);
    auto bodies = BodyStore(std::move(body_slots).seal());
    auto tests = TestStore(std::move(test_slots).seal());
    auto type_store = std::move(types).seal();
    auto constant_store = std::move(constants).seal();
    auto failure_set_store = std::move(failure_sets).seal();
    auto callable_signature_store = std::move(callable_signatures).seal();
    validate_publication_facts(
        program_identity,
        provenance_appender.reader(),
        type_store,
        constant_store,
        failure_set_store,
        callable_signature_store,
        declaration_store
    );
    validate_publication_topology(
        program_identity,
        provenance_appender.reader(),
        declaration_store,
        bodies,
        tests
    );
    state = State::Sealed;
    return SemIRProgram(
        program_identity,
        std::move(provenance_appender).finish(),
        std::move(type_store),
        std::move(constant_store),
        std::move(failure_set_store),
        std::move(callable_signature_store),
        std::move(declaration_store),
        std::move(bodies),
        std::move(tests)
    );
}
