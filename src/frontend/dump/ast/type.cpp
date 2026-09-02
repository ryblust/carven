module carven:frontend.dump.ast.type.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.interop;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.type;
import :frontend.dump.ast;
import :frontend.literal;
import :source.text;
import :support.visit;
import std;

namespace {

auto literal_kind_name(const IntegerLiteralValue& value) noexcept -> std::string_view {
    switch (value.base) {
        case IntegerBase::Decimal:     return "DecimalInteger";
        case IntegerBase::Hexadecimal: return "HexadecimalInteger";
        case IntegerBase::Binary:      return "BinaryInteger";
        case IntegerBase::Octal:       return "OctalInteger";
    }
    std::unreachable();
}

auto literal_kind_name(const FloatingLiteralValue&) noexcept -> std::string_view {
    return "DecimalFloating";
}

auto literal_kind_name(const StringLiteralValue&) noexcept -> std::string_view {
    return "String";
}

auto literal_kind_name(const CharacterLiteralValue&) noexcept -> std::string_view {
    return "Character";
}

auto literal_kind_name(const BooleanLiteralValue& value) noexcept -> std::string_view {
    return value.value ? "True" : "False";
}

template<typename... Values>
auto literal_kind_name(const std::variant<Values...>& value) noexcept -> std::string_view {
    return std::visit(
        [](const auto& alternative) static noexcept { return literal_kind_name(alternative); },
        value
    );
}

auto numeric_value_span(const NumericLiteralValue& value) noexcept -> Span {
    return std::visit(
        [](const auto& alternative) static noexcept { return alternative.value_span; },
        value
    );
}

auto literal_value_span(Span span, const ASTLiteralValue& value) noexcept -> Span {
    if (const auto* integer = std::get_if<IntegerLiteralValue>(&value)) {
        return integer->value_span;
    }
    if (const auto* floating = std::get_if<FloatingLiteralValue>(&value)) {
        return floating->value_span;
    }
    return span;
}

auto literal_numeric_suffix(const ASTLiteralValue& value) noexcept -> std::optional<NumericSuffix> {
    if (const auto* integer = std::get_if<IntegerLiteralValue>(&value)) {
        return integer->suffix;
    }
    if (const auto* floating = std::get_if<FloatingLiteralValue>(&value)) {
        return floating->suffix;
    }
    return std::nullopt;
}

} // namespace

auto ASTDumper::render_literal(
    const ASTLiteral& literal,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(prefix, is_last, std::format("{}Literal {}", field, source_label(literal.span)));
    const auto nested_prefix = child_prefix(prefix, is_last);
    append_line(nested_prefix, false, std::format("kind {}", literal_kind_name(literal.value)));
    append_line(
        nested_prefix,
        false,
        std::format("value {}", source_label(literal_value_span(literal.span, literal.value)))
    );
    const auto suffix = literal_numeric_suffix(literal.value);
    append_line(
        nested_prefix,
        true,
        std::format("suffix {}", suffix.value_or(NumericSuffix::None))
    );
}

auto ASTDumper::render_numeric_literal(
    Span span,
    const NumericLiteralValue& value,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(prefix, is_last, std::format("{}Literal {}", field, source_label(span)));
    const auto nested_prefix = child_prefix(prefix, is_last);
    append_line(nested_prefix, false, std::format("kind {}", literal_kind_name(value)));
    append_line(
        nested_prefix,
        false,
        std::format("value {}", source_label(numeric_value_span(value)))
    );
    append_line(nested_prefix, true, std::format("suffix {}", numeric_suffix(value)));
}

auto ASTDumper::render_cpp_source_fragment(
    const ASTCppSourceFragment& fragment,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    append_line(prefix, is_last, std::format("{}CppSourceFragment", field));
    const auto nested_prefix = child_prefix(prefix, is_last);
    append_line(nested_prefix, false, std::format("form {}", source_label(fragment.form_span)));
    append_line(
        nested_prefix,
        true,
        std::format("payload {}", source_label(fragment.payload_span))
    );
}

auto ASTDumper::render_named_type_children(
    const ASTNamedType& named,
    std::string_view prefix,
    bool is_last
) noexcept -> void {
    render_list(
        prefix,
        is_last,
        "components",
        named.components,
        [&](const ASTTypeNameComponent& component,
            std::string_view item_prefix,
            bool item_last) noexcept {
            append_line(item_prefix, item_last, "TypeNameComponent");
            const auto nested_prefix = child_prefix(item_prefix, item_last);
            render_span_field(nested_prefix, true, "name", component.name_span);
        }
    );
}

auto ASTDumper::render_function_type_children(
    const ASTFunctionType& function,
    std::string_view prefix
) noexcept -> void {
    render_list(
        prefix,
        false,
        "parameters",
        function.parameters,
        [&](const ASTFunctionTypeParameter& parameter,
            std::string_view item_prefix,
            bool item_last) noexcept {
            append_line(
                item_prefix,
                item_last,
                std::format("FunctionTypeParameter {}", format_dump_span(parameter.span))
            );
            const auto nested_prefix = child_prefix(item_prefix, item_last);
            if (parameter.access.marker.has_value()) {
                render_span_field(nested_prefix, false, "access marker", *parameter.access.marker);
            } else {
                append_line(nested_prefix, false, "access Read");
            }
            render_type(parameter.type, nested_prefix, true, "type ");
        }
    );
    render_type(function.result_type, prefix, false, "result ");
    render_throw_clause(function.throw_clause, prefix, true);
}

auto ASTDumper::render_type(
    ASTTypeID type_id,
    std::string_view prefix,
    bool is_last,
    std::string_view field
) noexcept -> void {
    const auto& type = ast.type(type_id);
    std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("{}NamedType {}", field, format_dump_span(type.span))
                );
                render_named_type_children(named, child_prefix(prefix, is_last), true);
            },
            [&](const ASTArrayType& array) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("{}ArrayType {}", field, format_dump_span(type.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_type(array.element_type, nested_prefix, false, "element ");
                render_expression(array.extent, nested_prefix, true, "extent ");
            },
            [&](const ASTFunctionType& function) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("{}FunctionType {}", field, format_dump_span(type.span))
                );
                render_function_type_children(function, child_prefix(prefix, is_last));
            },
        },
        type.value
    );
}

auto ASTDumper::render_construction_type(
    const ASTConstructionType& type,
    std::string_view prefix,
    bool is_last
) noexcept -> void {
    std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("type NamedType {}", format_dump_span(type.span))
                );
                render_named_type_children(named, child_prefix(prefix, is_last), true);
            },
            [&](const ASTFunctionType& function) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("type FunctionType {}", format_dump_span(type.span))
                );
                render_function_type_children(function, child_prefix(prefix, is_last));
            },
        },
        type.value
    );
}
