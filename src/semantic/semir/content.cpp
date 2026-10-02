module carven:semantic.semir.content.impl;

import :semantic.semir.constant;
import :semantic.semir.content;
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
    auto finish() && noexcept -> std::string;

private:
    auto token(std::string_view value) noexcept -> void;
    auto number(std::uint64_t value) noexcept -> void;
    auto integer(IntegerConstant value) noexcept -> void;
    auto module_path(ModuleID id) noexcept -> void;
    auto callable(CallableID id) noexcept -> ContentTask;
    auto signature(CallableSignatureID id) noexcept -> ContentTask;
    auto name(const CppNameReference& value) noexcept -> void;
    auto operand(const CppTypeOperand& value) noexcept -> ContentTask;

    const SemIRProgram& program;
    CompilationProvenanceView provenance;
    std::string contents;
    // Map insertion keeps these definition references valid through finish().
    std::vector<const std::string*> definitions;
    std::map<std::string, std::uint64_t> nodes;
    std::map<TypeID, std::uint64_t> type_nodes;
    std::map<ConstantID, std::uint64_t> constant_nodes;

    template<typename Write>
    auto node(Write write) noexcept -> ContinuationTask<std::uint64_t> {
        auto outer = std::exchange(contents, {});
        co_await write();
        auto descriptor = std::move(contents);
        const auto [entry, inserted] = nodes.try_emplace(std::move(descriptor), nodes.size());
        if (inserted) {
            definitions.push_back(std::addressof(entry->first));
        }
        contents = std::move(outer);
        token("node");
        number(entry->second);
        co_return entry->second;
    }
};

ContentWriter::ContentWriter(const SemIRProgram& program) noexcept
    : program(program),
      provenance(program.provenance()) {}

auto ContentWriter::token(std::string_view value) noexcept -> void {
    contents += std::format("{}:", value.size());
    contents += value;
}

auto ContentWriter::number(std::uint64_t value) noexcept -> void {
    token(std::format("{}", value));
}

auto ContentWriter::integer(IntegerConstant value) noexcept -> void {
    number(value.magnitude());
    number(value.negative());
}

auto ContentWriter::module_path(ModuleID id) noexcept -> void {
    token(provenance.module_record(program.declarations().module_decl(id).provenance_module)
              .path.value());
}

auto ContentWriter::callable(CallableID id) noexcept -> ContentTask {
    const auto& declarations = program.declarations();
    if (const auto function = declarations.function_for_callable(id)) {
        const auto& declaration = declarations.function(*function);
        module_path(declaration.module_id);
        token(provenance.spelling(declaration.name));
        co_return {};
    }
    const auto instance =
        std::ranges::find(program.static_instances(), id, &StaticInstance::callable);
    if (instance != program.static_instances().end()) {
        token("instance");
        co_await callable(declarations.function(instance->function).callable);
        number(instance->arguments.size());
        for (const auto argument : instance->arguments) {
            co_await constant(argument);
        }
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
    number(value.expression.index());
    co_await value.expression.visit([&](const auto& value) noexcept -> ContentTask {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, CppNameReference>) {
            name(value);
        } else if constexpr (std::same_as<Value, CppCallQuery>) {
            number(value.callee.index());
            co_await value.callee.visit([&](const auto& callee) noexcept -> ContentTask {
                using Callee = std::remove_cvref_t<decltype(callee)>;
                if constexpr (std::same_as<Callee, CppNameReference>) {
                    name(callee);
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
    });
    co_return {};
}

auto ContentWriter::type(TypeID id) noexcept -> ContentTask {
    if (const auto found = type_nodes.find(id); found != type_nodes.end()) {
        token("node");
        number(found->second);
        co_return {};
    }
    const auto index = co_await node([&]() noexcept -> ContentTask {
        token("type");
        const auto& value = program.types().type(id).value;
        number(value.index());
        co_await value.visit([&](const auto& value) noexcept -> ContentTask {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, BuiltinTypeValue>) {
                number(static_cast<std::uint64_t>(value.kind));
            } else if constexpr (std::same_as<Value, StructTypeValue>) {
                const auto& declaration = program.declarations().structure(value.structure);
                module_path(declaration.module_id);
                token(provenance.spelling(declaration.name));
            } else if constexpr (std::same_as<Value, EnumTypeValue>) {
                const auto& declaration = program.declarations().enumeration(value.enumeration);
                module_path(declaration.module_id);
                token(provenance.spelling(declaration.name));
            } else if constexpr (std::same_as<Value, ArrayTypeValue>) {
                co_await type(value.element);
                number(value.extent);
            } else if constexpr (std::same_as<Value, SliceTypeValue>
                                 || std::same_as<Value, RangeTypeValue>) {
                co_await type(value.element);
            } else if constexpr (std::same_as<Value, PointerTypeValue>) {
                co_await type(value.target);
                number(static_cast<std::uint64_t>(value.access));
            } else if constexpr (std::same_as<Value, FunctionTypeValue>
                                 || std::same_as<Value, ClosureTypeValue>) {
                co_await callable(value.callable);
            } else if constexpr (std::same_as<Value, CallableViewTypeValue>) {
                co_await signature(value.signature);
            } else if constexpr (std::same_as<Value, CppTypeValue>) {
                number(value.form.index());
                co_await value.form.visit([&](const auto& form) noexcept -> ContentTask {
                    using Form = std::remove_cvref_t<decltype(form)>;
                    if constexpr (std::same_as<Form, CppNamedType>) {
                        name(form.name);
                        number(form.arguments.size());
                        for (const auto argument : form.arguments) {
                            co_await type(argument);
                        }
                    } else if constexpr (std::same_as<Form, CppQueryType>) {
                        co_await query(form);
                    } else {
                        static_assert(std::same_as<Form, CppConstCharPointerType>);
                    }
                    co_return {};
                });
            } else {
                static_assert(std::same_as<Value, CppTypeValue>);
            }
            co_return {};
        });
        co_return {};
    });
    type_nodes.emplace(id, index);
    co_return {};
}

auto ContentWriter::constant(ConstantID id) noexcept -> ContentTask {
    if (const auto found = constant_nodes.find(id); found != constant_nodes.end()) {
        token("node");
        number(found->second);
        co_return {};
    }
    const auto index = co_await node([&]() noexcept -> ContentTask {
        token("constant");
        const auto& fact = program.constants().constant(id);
        co_await type(fact.type);
        number(fact.value.index());
        fact.value.visit([&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, StringConstant>
                          || std::same_as<Value, CStringConstant>) {
                token(provenance.spelling(value.value));
            } else if constexpr (std::same_as<Value, IntegerConstant>) {
                integer(value);
            } else if constexpr (std::same_as<Value, RangeConstant>) {
                integer(value.begin);
                integer(value.end);
                number(value.inclusive);
            } else if constexpr (std::same_as<Value, SIMDConstant>) {
                number(value.lanes.size());
                for (const auto lane : value.lanes) {
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
    constant_nodes.emplace(id, index);
    co_return {};
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
