module carven:semantic.evaluation.display.impl;

import :semantic.evaluation.display;
import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.format.builtin;
import std;

namespace {

class ExecutionDisplayText final {
public:
    static constexpr auto byte_limit = 16384uz;
    auto text(std::string_view value) noexcept -> void;
    auto quoted(std::string_view value, bool character_literal = false) noexcept -> void;
    auto take() && noexcept -> std::string;
    auto truncated() const noexcept -> bool;
    auto line(std::size_t depth) noexcept -> void;

private:
    std::string bytes;
    bool is_truncated = false;
};

auto ExecutionDisplayText::text(std::string_view value) noexcept -> void {
    if (is_truncated) {
        return;
    }
    if (value.size() > byte_limit - bytes.size()) {
        bytes.append(value.substr(0, byte_limit - bytes.size()));
        auto start = bytes.size();
        while (start != 0 && (static_cast<unsigned char>(bytes[start - 1]) & 0xc0u) == 0x80u) {
            --start;
        }
        if (start != 0) {
            --start;
            const auto lead = static_cast<unsigned char>(bytes[start]);
            const auto width = lead < 0x80u ? 1u : lead < 0xe0u ? 2u : lead < 0xf0u ? 3u : 4u;
            if (bytes.size() - start < width) {
                bytes.resize(start);
            }
        }
        bytes.append("...");
        is_truncated = true;
        return;
    }
    bytes.append(value);
}

auto ExecutionDisplayText::quoted(std::string_view value, bool character_literal) noexcept -> void {
    text(character_literal ? "'" : "\"");
    for (const auto character : value) {
        if (is_truncated) {
            break;
        }
        switch (character) {
            case '\\': text("\\\\"); break;
            case '"':  text(character_literal ? "\"" : "\\\""); break;
            case '\'': text(character_literal ? "\\'" : "'"); break;
            case '\n': text("\\n"); break;
            case '\r': text("\\r"); break;
            case '\t': text("\\t"); break;
            case '\0': text("\\0"); break;
            default:   text(std::string_view(&character, 1)); break;
        }
    }
    text(character_literal ? "'" : "\"");
}

auto ExecutionDisplayText::take() && noexcept -> std::string {
    return std::move(bytes);
}

auto ExecutionDisplayText::truncated() const noexcept -> bool {
    return is_truncated;
}

auto ExecutionDisplayText::line(std::size_t depth) noexcept -> void {
    text("\n");
    for (auto level = 0uz; level < depth; ++level) {
        text("    ");
    }
}

template<typename Sink>
auto report_field(
    const Sink& write,
    std::string_view label,
    std::string_view text,
    std::string_view indent = "  "
) noexcept -> void {
    write(indent);
    write(label);
    if (text.empty()) {
        write(" \"\"");
    } else if (text.find('\n') == std::string_view::npos) {
        write(" ");
        write(text);
    } else {
        while (!text.empty()) {
            write("\n");
            write(indent);
            write("  ");
            const auto newline = text.find('\n');
            write(text.substr(0, newline));
            if (newline == std::string_view::npos) {
                break;
            }
            text.remove_prefix(newline + 1);
        }
    }
}

auto display_text(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<std::string_view> {
    if (const auto text = execution_text(values, value)) {
        return text;
    }
    if (const auto atom = execution_atom(values, value)) {
        if (const auto* text = std::get_if<CStringConstant>(&atom->value)) {
            return values.spelling(text->value);
        }
    }
    return std::nullopt;
}

class ExecutionDisplay final {
public:
    ExecutionDisplay(const ExecutionValueAccess& values, const ExecutionMemory* memory) noexcept;
    auto write(const ExecutionValue& value, std::size_t depth, bool nested) noexcept -> bool;
    auto finish() noexcept -> std::string;

private:
    static constexpr auto depth_limit = 8uz;
    static constexpr auto element_limit = 64uz;
    const ExecutionValueAccess& values;
    const ExecutionMemory* memory;
    ExecutionDisplayText buffer;
};

ExecutionDisplay::ExecutionDisplay(
    const ExecutionValueAccess& values,
    const ExecutionMemory* memory
) noexcept
    : values(values),
      memory(memory) {}

auto ExecutionDisplay::write(const ExecutionValue& value, std::size_t depth, bool nested) noexcept
    -> bool {
    if (depth >= depth_limit) {
        buffer.text("...");
        return true;
    }
    if (const auto string = display_text(values, value)) {
        if (nested) {
            buffer.quoted(*string);
        } else {
            buffer.text(*string);
        }
        return true;
    }
    const auto reference = execution_value_type(values, value);
    const auto* type = std::get_if<TypeID>(&reference);
    if (!type) {
        return false;
    }
    const auto names = values.display_names(*type);
    if (names.is_class) {
        buffer.text(names.name);
        return true;
    }
    const auto compound = execution_compound_view(values, value, memory);
    auto enum_case = compound ? compound->enum_case : std::nullopt;
    const auto atom = execution_atom(values, value);
    if (atom) {
        if (const auto* enumeration = std::get_if<NumericEnumConstant>(&atom->value)) {
            enum_case = enumeration->enum_case;
        }
    }
    if (enum_case) {
        buffer.text(names.name);
        buffer.text("::");
        for (const auto& [id, name] : names.cases) {
            if (id == *enum_case) {
                buffer.text(name);
                break;
            }
        }
        if (!compound || compound->size() == 0) {
            return true;
        }
    }
    if (compound) {
        const auto structure =
            std::holds_alternative<StructTypeValue>(values.type_copy(*type).value);
        if (structure) {
            buffer.text(names.name);
            buffer.text(" {");
        } else {
            buffer.text(enum_case ? "(" : "[");
        }
        const auto count =
            structure || enum_case ? compound->size() : std::min(compound->size(), element_limit);
        for (auto index = 0uz; index < count; ++index) {
            buffer.line(depth + 1);
            if (structure) {
                buffer.text(names.fields.at(index));
                buffer.text(": ");
            }
            const auto success = compound->elements.visit([&](const auto elements) noexcept {
                return write(elements[index], depth + 1, true);
            });
            if (!success) {
                return false;
            }
            buffer.text(",");
            if (buffer.truncated()) {
                break;
            }
        }
        if (count < compound->size()) {
            buffer.line(depth + 1);
            buffer.text("...,");
        }
        if (count != 0) {
            buffer.line(depth);
        }
        buffer.text(structure ? "}" : enum_case ? ")" : "]");
        return true;
    }
    if (atom) {
        if (const auto* range = std::get_if<RangeConstant>(&atom->value)) {
            const auto canonical = values.type_copy(*type);
            const auto* range_type = std::get_if<RangeTypeValue>(&canonical.value);
            if (range_type == nullptr) {
                return false;
            }
            if (!write(
                    ConstantAtom {.type = range_type->element, .value = range->begin},
                    depth + 1,
                    true
                )) {
                return false;
            }
            buffer.text(range->inclusive ? "..=" : "..");
            return write(
                ConstantAtom {.type = range_type->element, .value = range->end},
                depth + 1,
                true
            );
        }
        if (std::holds_alternative<NullPointerConstant>(atom->value)) {
            buffer.text("nullptr");
            return true;
        }
    }
    if (atom) {
        auto formatted = format_builtin_value(
            builtin_format_value(values, constant_fact(*atom)),
            {},
            ExecutionDisplayText::byte_limit
        );
        if (formatted) {
            if (nested && std::holds_alternative<CharacterConstant>(atom->value)) {
                buffer.quoted(*formatted, true);
            } else {
                buffer.text(*formatted);
            }
            return true;
        }
    }
    return false;
}

auto ExecutionDisplay::finish() noexcept -> std::string {
    return std::move(buffer).take();
}

} // namespace

auto display_execution_value(
    const ExecutionValueAccess& values,
    const ExecutionValue& value,
    bool nested,
    const ExecutionMemory* memory
) noexcept -> std::optional<std::string> {
    if (!nested) {
        if (const auto text = display_text(values, value)) {
            return std::string(*text);
        }
        if (const auto atom = execution_atom(values, value)) {
            if (std::holds_alternative<BuiltinTypeValue>(values.type_copy(atom->type).value)) {
                auto formatted = format_builtin_value(
                    builtin_format_value(values, constant_fact(*atom)),
                    {},
                    maximum_constant_text_bytes
                );
                return formatted ? std::optional(std::move(*formatted)) : std::nullopt;
            }
        }
    }
    auto display = ExecutionDisplay(values, memory);
    if (!display.write(value, 0, nested)) {
        return std::nullopt;
    }
    return display.finish();
}

auto append_report_field(
    std::string& output,
    std::string_view label,
    std::string_view text,
    std::string_view indent
) noexcept -> void {
    output += '\n';
    report_field([&](std::string_view bytes) noexcept { output += bytes; }, label, text, indent);
}

auto execution_message(const ExecutionEvent& event) noexcept -> std::string {
    auto output = std::string(event.message());
    for (const auto& field : event.fields) {
        append_report_field(output, field.label, field.text);
    }
    return output;
}

auto execution_message_size(const ExecutionEvent& event) noexcept -> std::size_t {
    auto size = event.message().size();
    for (const auto& field : event.fields) {
        ++size;
        report_field(
            [&](std::string_view bytes) noexcept { size += bytes.size(); },
            field.label,
            field.text
        );
    }
    return size;
}

auto SemanticExecutor::observe_condition(
    const SemanticExpression& source,
    const ExecutionValue& left,
    const ExecutionValue* right,
    bool passed
) noexcept -> void {
    if (passed
        || !current->condition_observation
        || current->condition_observation->condition != &source) {
        return;
    }
    auto output = ExecutionDisplayText();
    const auto observe = [&](ProgramSpellingID source, const std::string& value) noexcept {
        const auto spelling = values.spelling(source);
        if (spelling != value) {
            output.text(spelling);
            output.text(": ");
            output.text(value);
            output.text("\n");
        }
    };
    observe(
        current->condition_observation->sources[0],
        display_execution_value(values, left, true, &memory).value_or("<opaque>")
    );
    observe(
        current->condition_observation->sources[1],
        right ? display_execution_value(values, *right, true, &memory).value_or("<opaque>")
              : "<not evaluated>"
    );
    *current->condition_observation->explanation = std::move(output).take();
}
