module carven:semantic.semir.delegation;

import :semantic.semir.ids;
import :semantic.semir.operation;
import :support.invariant;
import std;

enum class CppNameLookup {
    Global,
    ModuleScope,
};

struct CppNameReference final {
    ModuleID context_module;
    CppNameLookup lookup;
    std::vector<std::string> components;
    auto operator<=>(const CppNameReference&) const noexcept = default;
};

auto valid_cpp_name(const CppNameReference& name) noexcept -> bool;

struct CppNamedType final {
    CppNameReference name;
    std::vector<TypeID> arguments;
    auto operator==(const CppNamedType&) const noexcept -> bool = default;
};

struct CppConstCharPointerType final {
    auto operator==(const CppConstCharPointerType&) const noexcept -> bool = default;
};

struct CppNameOperation final {
    CppNameReference name;

    auto operator==(const CppNameOperation&) const noexcept -> bool = default;
};

struct CppStaticMemberOperation final {
    ConstructionTypeRef type;
    std::string member;
    auto operator==(const CppStaticMemberOperation&) const noexcept -> bool = default;
};

struct CppConstructOperation final {
    ConstructionTypeRef target;
    auto operator==(const CppConstructOperation&) const noexcept -> bool = default;
};

struct CppMemberOperation final {
    std::string name;
    auto operator==(const CppMemberOperation&) const noexcept -> bool = default;
};

struct CppIndexOperation final {
    auto operator==(const CppIndexOperation&) const noexcept -> bool = default;
};

struct CppConvertOperation final {
    bool explicit_cast;
    auto operator==(const CppConvertOperation&) const noexcept -> bool = default;
};

struct CppUpdateOperation final {
    bool increment;
    auto operator==(const CppUpdateOperation&) const noexcept -> bool = default;
};

struct CppBinaryOperation final {
    BinaryOperator operation;
    auto operator==(const CppBinaryOperation&) const noexcept -> bool = default;
};

struct CppUnaryOperation final {
    UnaryOperator operation;
    auto operator==(const CppUnaryOperation&) const noexcept -> bool = default;
};

using CppOperation = std::variant<
    CppNameOperation,
    CppStaticMemberOperation,
    CppConstructOperation,
    CppMemberOperation,
    CppIndexOperation,
    CppConvertOperation,
    CppBinaryOperation,
    CppUnaryOperation,
    CppUpdateOperation>;

auto cpp_operation_accepts_arity(const CppOperation& operation, std::size_t arity) noexcept -> bool;

template<typename Type>
struct BasicCppTypeOperand final {
    using TypeReference = Type;
    Type type;
    AccessMode access;
    auto operator<=>(const BasicCppTypeOperand&) const noexcept = default;
};

template<typename Operand>
struct CppMemberCallee final {
    Operand receiver;
    std::string member;
    auto operator<=>(const CppMemberCallee&) const noexcept = default;
};

template<typename Type>
struct CppStaticMemberCallee final {
    Type type;
    std::string member;
    auto operator<=>(const CppStaticMemberCallee&) const noexcept = default;
};

template<typename Operand>
using CppCallee = std::variant<
    CppNameReference,
    CppStaticMemberCallee<typename Operand::TypeReference>,
    CppMemberCallee<Operand>,
    Operand>;

template<typename Operand, typename Visitor>
auto visit_cpp_callee_operand(const CppCallee<Operand>& callee, Visitor visit) noexcept -> void {
    callee.visit([&](const auto& value) noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, Operand>) {
            visit(value);
        } else if constexpr (std::same_as<Value, CppMemberCallee<Operand>>) {
            visit(value.receiver);
        }
    });
}

template<typename Type>
struct BasicCppCallQuery final {
    CppCallee<BasicCppTypeOperand<Type>> callee;
    std::vector<BasicCppTypeOperand<Type>> arguments;
    auto operator<=>(const BasicCppCallQuery&) const noexcept = default;
};

template<typename Type>
struct BasicCppConstructArgument final {
    BasicCppTypeOperand<Type> operand;
    // A known scalar delivered after the source argument's evaluation obligations.
    std::optional<ConstantID> constant;
    auto operator<=>(const BasicCppConstructArgument&) const noexcept = default;
};

template<typename Type>
struct BasicCppConstructQuery final {
    Type target;
    std::vector<BasicCppConstructArgument<Type>> arguments;
    auto operator<=>(const BasicCppConstructQuery&) const noexcept = default;
};

template<typename Type>
struct BasicCppMemberQuery final {
    BasicCppTypeOperand<Type> receiver;
    std::string member;
    auto operator<=>(const BasicCppMemberQuery&) const noexcept = default;
};

template<typename Type>
struct BasicCppStaticMemberQuery final {
    Type type;
    std::string member;
    auto operator<=>(const BasicCppStaticMemberQuery&) const noexcept = default;
};

template<typename Type>
struct BasicCppIndexQuery final {
    BasicCppTypeOperand<Type> receiver;
    BasicCppTypeOperand<Type> index;
    auto operator<=>(const BasicCppIndexQuery&) const noexcept = default;
};

template<typename Type>
struct BasicCppUnaryQuery final {
    UnaryOperator operation;
    BasicCppTypeOperand<Type> operand;
    auto operator<=>(const BasicCppUnaryQuery&) const noexcept = default;
};

template<typename Type>
struct BasicCppBinaryQuery final {
    BinaryOperator operation;
    BasicCppTypeOperand<Type> left;
    BasicCppTypeOperand<Type> right;
    auto operator<=>(const BasicCppBinaryQuery&) const noexcept = default;
};

template<typename Type>
struct BasicCppQueryType final {
    std::variant<
        CppNameReference,
        BasicCppCallQuery<Type>,
        BasicCppConstructQuery<Type>,
        BasicCppStaticMemberQuery<Type>,
        BasicCppMemberQuery<Type>,
        BasicCppIndexQuery<Type>,
        BasicCppUnaryQuery<Type>,
        BasicCppBinaryQuery<Type>>
        expression;
    auto operator<=>(const BasicCppQueryType&) const noexcept = default;
};

using CppTypeOperand = BasicCppTypeOperand<TypeID>;
using CppCallQuery = BasicCppCallQuery<TypeID>;
using CppConstructArgument = BasicCppConstructArgument<TypeID>;
using CppConstructQuery = BasicCppConstructQuery<TypeID>;
using CppStaticMemberQuery = BasicCppStaticMemberQuery<TypeID>;
using CppMemberQuery = BasicCppMemberQuery<TypeID>;
using CppIndexQuery = BasicCppIndexQuery<TypeID>;
using CppUnaryQuery = BasicCppUnaryQuery<TypeID>;
using CppBinaryQuery = BasicCppBinaryQuery<TypeID>;
using CppQueryType = BasicCppQueryType<TypeID>;

template<typename Type, typename Visitor>
auto visit_cpp_query_types(const BasicCppQueryType<Type>& query, Visitor visit) noexcept -> void {
    const auto operand = [&](const BasicCppTypeOperand<Type>& value) noexcept {
        visit(value.type);
    };
    query.expression.visit([&](const auto& value) noexcept {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, BasicCppCallQuery<Type>>) {
            if (const auto* member = std::get_if<CppStaticMemberCallee<Type>>(&value.callee)) {
                visit(member->type);
            }
            visit_cpp_callee_operand(value.callee, operand);
            for (const auto& argument : value.arguments) {
                operand(argument);
            }
        } else if constexpr (std::same_as<Value, BasicCppConstructQuery<Type>>) {
            visit(value.target);
            for (const auto& argument : value.arguments) {
                operand(argument.operand);
            }
        } else if constexpr (std::same_as<Value, BasicCppStaticMemberQuery<Type>>) {
            visit(value.type);
        } else if constexpr (std::same_as<Value, BasicCppMemberQuery<Type>>) {
            operand(value.receiver);
        } else if constexpr (std::same_as<Value, BasicCppIndexQuery<Type>>) {
            operand(value.receiver);
            operand(value.index);
        } else if constexpr (std::same_as<Value, BasicCppUnaryQuery<Type>>) {
            operand(value.operand);
        } else if constexpr (std::same_as<Value, BasicCppBinaryQuery<Type>>) {
            operand(value.left);
            operand(value.right);
        }
    });
}

template<typename Type, typename Mapper>
auto map_cpp_query_types(const BasicCppQueryType<Type>& query, Mapper map) noexcept
    -> BasicCppQueryType<std::invoke_result_t<Mapper, Type>> {
    using Target = std::invoke_result_t<Mapper, Type>;
    const auto operand = [&](const BasicCppTypeOperand<Type>& value) noexcept {
        return BasicCppTypeOperand<Target> {.type = map(value.type), .access = value.access};
    };
    return query.expression.visit([&](const auto& value) noexcept -> BasicCppQueryType<Target> {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, CppNameReference>) {
            return {.expression = value};
        } else if constexpr (std::same_as<Value, BasicCppCallQuery<Type>>) {
            auto callee = value.callee.visit(
                [&](const auto& item) noexcept -> CppCallee<BasicCppTypeOperand<Target>> {
                    using Item = std::remove_cvref_t<decltype(item)>;
                    if constexpr (std::same_as<Item, CppNameReference>) {
                        return item;
                    } else if constexpr (std::same_as<Item, CppStaticMemberCallee<Type>>) {
                        return CppStaticMemberCallee<Target> {
                            .type = map(item.type),
                            .member = item.member
                        };
                    } else if constexpr (std::same_as<
                                             Item,
                                             CppMemberCallee<BasicCppTypeOperand<Type>>>) {
                        return CppMemberCallee<BasicCppTypeOperand<Target>> {
                            .receiver = operand(item.receiver),
                            .member = item.member
                        };
                    } else {
                        return operand(item);
                    }
                }
            );
            auto arguments = std::vector<BasicCppTypeOperand<Target>>();
            for (const auto& argument : value.arguments) {
                arguments.push_back(operand(argument));
            }
            return {
                .expression = BasicCppCallQuery<Target> {
                    .callee = std::move(callee),
                    .arguments = std::move(arguments)
                }
            };
        } else if constexpr (std::same_as<Value, BasicCppConstructQuery<Type>>) {
            auto arguments = std::vector<BasicCppConstructArgument<Target>>();
            for (const auto& argument : value.arguments) {
                arguments.push_back(
                    {.operand = operand(argument.operand), .constant = argument.constant}
                );
            }
            return {
                .expression = BasicCppConstructQuery<Target> {
                    .target = map(value.target),
                    .arguments = std::move(arguments)
                }
            };
        } else if constexpr (std::same_as<Value, BasicCppStaticMemberQuery<Type>>) {
            return {
                .expression = BasicCppStaticMemberQuery<Target> {
                    .type = map(value.type),
                    .member = value.member
                }
            };
        } else if constexpr (std::same_as<Value, BasicCppMemberQuery<Type>>) {
            return {
                .expression = BasicCppMemberQuery<Target> {
                    .receiver = operand(value.receiver),
                    .member = value.member
                }
            };
        } else if constexpr (std::same_as<Value, BasicCppIndexQuery<Type>>) {
            return {
                .expression = BasicCppIndexQuery<Target> {
                    .receiver = operand(value.receiver),
                    .index = operand(value.index)
                }
            };
        } else if constexpr (std::same_as<Value, BasicCppUnaryQuery<Type>>) {
            return {
                .expression = BasicCppUnaryQuery<Target> {
                    .operation = value.operation,
                    .operand = operand(value.operand)
                }
            };
        } else {
            return {
                .expression = BasicCppBinaryQuery<Target> {
                    .operation = value.operation,
                    .left = operand(value.left),
                    .right = operand(value.right)
                }
            };
        }
    });
}

struct CppTypeValue final {
    std::variant<CppNamedType, CppQueryType, CppConstCharPointerType> form;
    auto operator==(const CppTypeValue&) const noexcept -> bool = default;
};

template<typename Type>
auto cpp_query_type(
    const CppOperation& operation,
    std::span<const BasicCppTypeOperand<Type>> operands
) noexcept -> BasicCppQueryType<Type> {
    if (!cpp_operation_accepts_arity(operation, operands.size())) {
        invariant_violation("C++ query operands disagree with the operation");
    }
    return operation.visit([&](const auto& value) noexcept -> BasicCppQueryType<Type> {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, CppNameOperation>) {
            return {.expression = value.name};
        } else if constexpr (std::same_as<Value, CppStaticMemberOperation>) {
            const auto type = [&]() noexcept -> Type {
                if constexpr (std::same_as<Type, ConstructionTypeRef>) {
                    return value.type;
                } else {
                    return std::get<TypeID>(value.type);
                }
            }();
            return {
                .expression = BasicCppStaticMemberQuery<Type> {.type = type, .member = value.member}
            };
        } else if constexpr (std::same_as<Value, CppMemberOperation>) {
            return {
                .expression =
                    BasicCppMemberQuery<Type> {.receiver = operands[0], .member = value.name}
            };
        } else if constexpr (std::same_as<Value, CppIndexOperation>) {
            return {
                .expression =
                    BasicCppIndexQuery<Type> {.receiver = operands[0], .index = operands[1]}
            };
        } else if constexpr (std::same_as<Value, CppUnaryOperation>) {
            return {
                .expression =
                    BasicCppUnaryQuery<Type> {.operation = value.operation, .operand = operands[0]}
            };
        } else if constexpr (std::same_as<Value, CppBinaryOperation>) {
            return {
                .expression = BasicCppBinaryQuery<Type> {
                    .operation = value.operation,
                    .left = operands[0],
                    .right = operands[1]
                }
            };
        } else {
            invariant_violation("operation requires an explicit result type");
        }
    });
}

template<typename Visitor>
auto visit_cpp_type_references(const CppTypeValue& type, Visitor visit) noexcept -> void {
    if (const auto* named = std::get_if<CppNamedType>(&type.form)) {
        for (const auto argument : named->arguments) {
            visit(argument);
        }
    } else if (const auto* query = std::get_if<CppQueryType>(&type.form)) {
        visit_cpp_query_types(*query, std::move(visit));
    }
}

// The returned name borrows the supplied type description.
auto cpp_type_name(const CppTypeValue& type) noexcept -> const CppNameReference*;
auto valid_cpp_type(const CppTypeValue& type) noexcept -> bool;
