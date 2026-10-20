module carven:semantic.semir.content.impl;

import :semantic.semir.constant;
import :semantic.semir.content;
import :semantic.semir.generic;
import :semantic.semir.program;
import :semantic.semir.stage;
import :support.invariant;
import :support.task;
import std;

namespace {

using ContentTask = ContinuationTask<std::monostate>;

class ContentWriter final {
public:
    explicit ContentWriter(const SemIRProgram& program) noexcept;
    auto type(TypeID id) noexcept -> ContentTask;
    auto query(const CppQueryType& value) noexcept -> ContentTask;
    auto constant(ConstantID id) noexcept -> ContentTask;
    auto callable(CallableID id) noexcept -> ContentTask;
    auto finish() && noexcept -> std::string;

private:
    auto token(std::string_view value) noexcept -> void;
    auto number(std::uint64_t value) noexcept -> void;
    auto integer(IntegerConstant value) noexcept -> void;
    auto module_path(ModuleID id) noexcept -> void;
    auto nominal(NominalDeclarationRef declaration) noexcept -> ContentTask;
    auto signature(CallableSignatureID id) noexcept -> ContentTask;
    auto name(const CppNameReference& value) noexcept -> void;
    auto operand(const CppTypeOperand& value) noexcept -> ContentTask;

    template<typename Value>
    auto query_expression(const Value& value, std::uint64_t index) noexcept -> ContentTask;

    const SemIRProgram& program;
    CompilationProvenanceView provenance;
    std::string contents;
    // Map insertion keeps these definition references valid through finish().
    std::vector<const std::string*> definitions;
    std::map<std::string, std::uint64_t> nodes;
    std::map<TypeID, std::uint64_t> type_nodes;
    std::map<ConstantID, std::uint64_t> constant_nodes;

    template<typename ID, typename Write>
    auto node(ID id, std::map<ID, std::uint64_t>& identities, Write write) noexcept -> ContentTask {
        if (const auto found = identities.find(id); found != identities.end()) {
            token("node");
            number(found->second);
            co_return {};
        }
        auto outer = std::exchange(contents, {});
        co_await write();
        auto descriptor = std::move(contents);
        const auto [entry, inserted] = nodes.try_emplace(std::move(descriptor), nodes.size());
        if (inserted) {
            definitions.push_back(std::addressof(entry->first));
        }
        identities.emplace(id, entry->second);
        contents = std::move(outer);
        token("node");
        number(entry->second);
        co_return {};
    }
};

ContentWriter::ContentWriter(const SemIRProgram& program) noexcept
    : program(program),
      provenance(program.provenance()) {}

auto ContentWriter::token(std::string_view value) noexcept -> void {
    auto length = std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1>();
    const auto end = std::to_chars(length.data(), length.data() + length.size(), value.size()).ptr;
    contents.append(length.data(), end);
    contents += ':';
    contents += value;
}

auto ContentWriter::number(std::uint64_t value) noexcept -> void {
    auto digits = std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1>();
    const auto end = std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr;
    token(std::string_view(digits.data(), end));
}

auto ContentWriter::integer(IntegerConstant value) noexcept -> void {
    number(value.magnitude());
    number(value.negative());
}

auto ContentWriter::module_path(ModuleID id) noexcept -> void {
    token(provenance.module_record(program.declarations().module_decl(id).provenance_module)
              .path.value());
}

auto ContentWriter::nominal(NominalDeclarationRef reference) noexcept -> ContentTask {
    if (const auto* instance = program.generic_nominal_instance(reference)) {
        const auto& definition = program.generic_declaration_contract(instance->definition);
        token("generic");
        module_path(definition.module_id);
        token(provenance.spelling(definition.name));
        number(instance->arguments.size());
        for (const auto& argument : instance->arguments) {
            if (const auto* object = std::get_if<TypeID>(&argument)) {
                token("type");
                co_await type(*object);
            } else {
                token("failures");
                const auto& members =
                    program.failure_sets().failure_set(std::get<FailureSetID>(argument)).members;
                number(members.size());
                for (const auto member : members) {
                    co_await type(member);
                }
            }
        }
    } else {
        reference.visit([&](auto id) noexcept {
            const auto& declaration = [&]() noexcept -> const auto& {
                if constexpr (std::same_as<decltype(id), StructID>) {
                    return program.declarations().structure(id);
                } else {
                    return program.declarations().enumeration(id);
                }
            }();
            module_path(declaration.module_id);
            token(provenance.spelling(declaration.name));
        });
    }
    co_return {};
}

auto ContentWriter::callable(CallableID id) noexcept -> ContentTask {
    const auto& declarations = program.declarations();
    if (const auto* instance = program.generic_function_instance(id)) {
        const auto& definition = program.generic_declaration_contract(instance->definition);
        token("generic instance");
        module_path(definition.module_id);
        token(provenance.spelling(definition.name));
        number(instance->arguments.size());
        for (const auto& argument : instance->arguments) {
            number(argument.index());
            if (const auto* target = std::get_if<TypeID>(&argument)) {
                co_await type(*target);
            } else {
                auto keys = std::vector<std::string>();
                for (const auto member :
                     program.failure_sets().failure_set(std::get<FailureSetID>(argument)).members) {
                    auto writer = ContentWriter(program);
                    co_await writer.type(member);
                    keys.push_back(std::move(writer).finish());
                }
                std::ranges::sort(keys);
                number(keys.size());
                for (const auto& key : keys) {
                    token(key);
                }
            }
        }
        co_return {};
    }
    if (const auto function = declarations.function_for_callable(id)) {
        const auto& declaration = declarations.function(*function);
        module_path(declaration.module_id);
        token(provenance.spelling(declaration.name));
        co_return {};
    }
    if (const auto* instance = program.static_instance(id)) {
        token("instance");
        co_await callable(declarations.function(instance->function).callable);
        number(instance->arguments.size());
        for (const auto argument : instance->arguments) {
            co_await constant(argument);
        }
        co_return {};
    }
    if (const auto* provided = std::get_if<ProvidedFunctionImplementation>(
            &declarations.callable(id).implementation
        )) {
        token("provided callable");
        number(static_cast<std::uint64_t>(provided->function));
        co_return {};
    }
    if (const auto* parser =
            std::get_if<NumberParseImplementation>(&declarations.callable(id).implementation)) {
        token("number parser");
        co_await signature(declarations.callable(id).signature);
        co_await constant(parser->invalid);
        co_return {};
    }
    const auto body = declarations.body_for_callable(id);
    if (!body) {
        invariant_violation("unnamed callable has no body provenance");
    }
    const auto origin = provenance.source_origin(program.bodies().body(*body).region().origin);
    token(provenance.source_snapshot(origin.source_id).display_origin());
    number(origin.span.start());
    number(origin.span.end());
    co_return {};
}

auto ContentWriter::signature(CallableSignatureID id) noexcept -> ContentTask {
    const auto& value = program.callable_signatures().signature(id);
    number(static_cast<std::uint64_t>(value.execution));
    number(value.parameters.size());
    for (const auto& parameter : value.parameters) {
        number(static_cast<std::uint64_t>(parameter.stage));
        number(static_cast<std::uint64_t>(parameter.access));
        co_await type(parameter.type);
    }
    co_await type(value.result);
    const auto& members = program.failure_sets().failure_set(value.failures).members;
    number(members.size());
    auto member_keys = std::vector<std::string>();
    for (const auto member : members) {
        auto writer = ContentWriter(program);
        co_await writer.type(member);
        member_keys.push_back(std::move(writer).finish());
    }
    std::ranges::sort(member_keys);
    for (const auto& key : member_keys) {
        token(key);
    }
    co_return {};
}

auto ContentWriter::name(const CppNameReference& value) noexcept -> void {
    number(static_cast<std::uint64_t>(value.lookup));
    // The declaration environment is part of a native reference's semantics,
    // including globally rooted names.
    module_path(value.context_module);
    const auto& environment = program.declarations().module_decl(value.context_module);
    number(environment.cpp_headers.size());
    for (const auto& header : environment.cpp_headers) {
        number(static_cast<std::uint64_t>(header.delimiter));
        token(provenance.spelling(header.name));
        number(header.namespace_opening.has_value());
        if (header.namespace_opening) {
            number(header.namespace_opening->components.size());
            for (const auto component : header.namespace_opening->components) {
                token(provenance.spelling(component));
            }
        }
    }
    number(environment.cpp_source_fragments.size());
    for (const auto& fragment : environment.cpp_source_fragments) {
        token(provenance.slice(fragment.payload_origin));
    }
    number(value.components.size());
    for (const auto& part : value.components) {
        token(part);
    }
}

auto ContentWriter::operand(const CppTypeOperand& value) noexcept -> ContentTask {
    number(static_cast<std::uint64_t>(value.access));
    co_await type(value.type);
    co_return {};
}

auto ContentWriter::query(const CppQueryType& value) noexcept -> ContentTask {
    return value.expression.visit([this, index = value.expression.index()](
                                      const auto& expression
                                  ) noexcept { return query_expression(expression, index); });
}

template<typename Value>
auto ContentWriter::query_expression(const Value& value, std::uint64_t index) noexcept
    -> ContentTask {
    number(index);
    if constexpr (std::same_as<Value, CppNameReference>) {
        name(value);
    } else if constexpr (std::same_as<Value, CppCallQuery>) {
        number(value.callee.index());
        co_await value.callee.visit([&](const auto& callee) noexcept -> ContentTask {
            using Callee = std::remove_cvref_t<decltype(callee)>;
            if constexpr (std::same_as<Callee, CppNameReference>) {
                name(callee);
            } else if constexpr (std::same_as<Callee, CppStaticMemberCallee<TypeID>>) {
                co_await type(callee.type);
                token(callee.member);
            } else if constexpr (std::same_as<Callee, CppMemberCallee<CppTypeOperand>>) {
                co_await operand(callee.receiver);
                token(callee.member);
            } else {
                static_assert(std::same_as<Callee, CppTypeOperand>);
                co_await operand(callee);
            }
            co_return {};
        });
        number(value.arguments.size());
        for (const auto& argument : value.arguments) {
            co_await operand(argument);
        }
    } else if constexpr (std::same_as<Value, CppConstructQuery>) {
        co_await type(value.target);
        number(value.arguments.size());
        for (const auto& argument : value.arguments) {
            co_await operand(argument.operand);
            number(argument.constant.has_value());
            if (argument.constant) {
                co_await constant(*argument.constant);
            }
        }
    } else if constexpr (std::same_as<Value, CppStaticMemberQuery>) {
        co_await type(value.type);
        token(value.member);
    } else if constexpr (std::same_as<Value, CppMemberQuery>) {
        co_await operand(value.receiver);
        token(value.member);
    } else if constexpr (std::same_as<Value, CppIndexQuery>) {
        co_await operand(value.receiver);
        co_await operand(value.index);
    } else if constexpr (std::same_as<Value, CppUnaryQuery>) {
        number(static_cast<std::uint64_t>(value.operation));
        co_await operand(value.operand);
    } else if constexpr (std::same_as<Value, CppBinaryQuery>) {
        number(static_cast<std::uint64_t>(value.operation));
        co_await operand(value.left);
        co_await operand(value.right);
    } else {
        static_assert(std::same_as<Value, CppBinaryQuery>);
    }
    co_return {};
}

auto ContentWriter::type(TypeID id) noexcept -> ContentTask {
    return node(id, type_nodes, [this, id]() noexcept -> ContentTask {
        token("type");
        const auto& value = program.types().type(id).value;
        number(value.index());
        co_await value.visit([&](const auto& value) noexcept -> ContentTask {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, BuiltinTypeValue>) {
                number(static_cast<std::uint64_t>(value.kind));
            } else if constexpr (std::same_as<Value, StructTypeValue>) {
                co_await nominal(value.structure);
            } else if constexpr (std::same_as<Value, EnumTypeValue>) {
                co_await nominal(value.enumeration);
            } else if constexpr (std::same_as<Value, ArrayTypeValue>) {
                co_await type(value.element);
                number(value.extent);
            } else if constexpr (std::same_as<Value, TupleTypeValue>) {
                number(value.elements.size());
                for (const auto element : value.elements) {
                    co_await type(element);
                }
            } else if constexpr (std::same_as<Value, VectorTypeValue>
                                 || std::same_as<Value, SliceTypeValue>
                                 || std::same_as<Value, RangeTypeValue>) {
                co_await type(value.element);
            } else if constexpr (std::same_as<Value, PointerTypeValue>) {
                co_await type(value.target);
                number(static_cast<std::uint64_t>(value.access));
            } else if constexpr (std::same_as<Value, FunctionTypeValue>
                                 || std::same_as<Value, ClosureTypeValue>) {
                co_await callable(value.callable);
            } else if constexpr (std::same_as<Value, OperationTypeValue>) {
                co_await type(value.success);
                const auto& failures = program.failure_sets().failure_set(value.failures).members;
                number(failures.size());
                for (const auto member : failures) {
                    co_await type(member);
                }
            } else if constexpr (std::same_as<Value, CallableViewTypeValue>) {
                co_await signature(value.signature);
            } else if constexpr (std::same_as<Value, CppTypeValue>) {
                number(value.form.index());
                if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                    name(named->name);
                    number(named->arguments.size());
                    for (const auto argument : named->arguments) {
                        co_await type(argument);
                    }
                } else if (const auto* query_value = std::get_if<CppQueryType>(&value.form)) {
                    co_await query(*query_value);
                }
            } else {
                static_assert(std::same_as<Value, CppTypeValue>);
            }
            co_return {};
        });
        co_return {};
    });
}

auto ContentWriter::constant(ConstantID id) noexcept -> ContentTask {
    return node(id, constant_nodes, [this, id]() noexcept -> ContentTask {
        token("constant");
        const auto& fact = program.constants().constant(id);
        co_await type(fact.type);
        number(fact.value.index());
        fact.value.visit([&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, TextConstant>
                          || std::same_as<Value, CStringConstant>) {
                token(provenance.spelling(value.value));
            } else if constexpr (std::same_as<Value, VectorConstant>) {
                number(value.capacity);
            } else if constexpr (std::same_as<Value, ByteSequenceConstant>) {
                number(value.capacity);
                token(
                    std::string_view(
                        reinterpret_cast<const char*>(value.bytes.data()),
                        value.bytes.size()
                    )
                );
            } else if constexpr (std::same_as<Value, IntegerConstant>) {
                integer(value);
            } else if constexpr (std::same_as<Value, RangeConstant>) {
                integer(value.begin);
                integer(value.end);
                number(value.inclusive);
            } else if constexpr (std::same_as<Value, SIMDConstant>) {
                for (const auto lane : value.words) {
                    number(lane);
                }
            } else if constexpr (std::same_as<Value, BooleanConstant>) {
                number(value.value);
            } else if constexpr (std::same_as<Value, F32Constant>) {
                number(std::bit_cast<std::uint32_t>(value.value));
            } else if constexpr (std::same_as<Value, F64Constant>) {
                number(std::bit_cast<std::uint64_t>(value.value));
            } else if constexpr (std::same_as<Value, CharacterConstant>) {
                number(value.scalar);
            } else if constexpr (std::same_as<Value, NumericEnumConstant>
                                 || std::same_as<Value, PayloadEnumConstant>) {
                token(provenance.spelling(program.declarations().enum_case(value.enum_case).name));
                if constexpr (std::same_as<Value, NumericEnumConstant>) {
                    integer(value.value);
                }
            }
        });
        if (const auto children = constant_children(fact.value)) {
            number(children->size());
            for (const auto child : *children) {
                co_await constant(child);
            }
        }
        co_return {};
    });
}

auto ContentWriter::finish() && noexcept -> std::string {
    const auto root = std::exchange(contents, {});
    number(definitions.size());
    for (const auto& definition : definitions) {
        token(*definition);
    }
    token(root);
    return std::move(contents);
}

} // namespace

auto type_content_key(const SemIRProgram& program, TypeID type) noexcept -> std::string {
    auto writer = ContentWriter(program);
    writer.type(type).run();
    return std::move(writer).finish();
}

auto constant_content_key(const SemIRProgram& program, ConstantID constant) noexcept
    -> std::string {
    auto writer = ContentWriter(program);
    writer.constant(constant).run();
    return std::move(writer).finish();
}

auto callable_content_key(const SemIRProgram& program, CallableID callable) noexcept
    -> std::string {
    auto writer = ContentWriter(program);
    writer.callable(callable).run();
    return std::move(writer).finish();
}
