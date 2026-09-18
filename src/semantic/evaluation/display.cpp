module carven:semantic.evaluation.display.impl;

import :semantic.evaluation.display;
import :semantic.evaluation.limits;
import :semantic.format.builtin;
import std;

namespace {

class ExecutionDisplay final {
public:
    explicit ExecutionDisplay(const ExecutionValueAccess& values) noexcept;
    auto write(const ExecutionValue& value, std::size_t depth, bool nested) noexcept -> bool;
    auto finish() noexcept -> std::string;

private:
    auto text(std::string_view value) noexcept -> void;
    auto quoted(std::string_view value, bool character_literal = false) noexcept -> void;
    auto line(std::size_t depth) noexcept -> void;
    const ExecutionValueAccess& values;
    std::string bytes;
    bool truncated = false;
};

ExecutionDisplay::ExecutionDisplay(const ExecutionValueAccess& values) noexcept
    : values(values) {}

auto ExecutionDisplay::text(std::string_view value) noexcept -> void {
    if (truncated) {
        return;
    }
    constexpr auto limit = 16384uz;
    if (value.size() > limit - bytes.size()) {
        bytes.append(value.substr(0, limit - bytes.size()));
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
        truncated = true;
        return;
    }
    bytes.append(value);
}

auto ExecutionDisplay::quoted(std::string_view value, bool character_literal) noexcept -> void {
    text(character_literal ? "'" : "\"");
    for (const auto character : value) {
        if (truncated) {
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

auto ExecutionDisplay::line(std::size_t depth) noexcept -> void {
    text("\n");
    for (auto level = 0uz; level < depth; ++level) {
        text("    ");
    }
}

auto ExecutionDisplay::write(const ExecutionValue& value, std::size_t depth, bool nested) noexcept
    -> bool {
    if (depth == 8) {
        text("...");
        return true;
    }
    if (const auto string = execution_text(values, value)) {
        if (nested) {
            quoted(*string);
        } else {
            text(*string);
        }
        return true;
    }
    const auto type = execution_value_type(values, value);
    const auto names = values.display_names(type);
    const auto compound = execution_compound_view(values, value);
    auto enum_case = compound ? compound->enum_case : std::nullopt;
    const auto atom = execution_atom(values, value);
    if (atom) {
        if (const auto* enumeration = std::get_if<NumericEnumConstant>(&atom->value)) {
            enum_case = enumeration->enum_case;
        }
    }
    if (enum_case) {
        text(names.name);
        text("::");
        for (const auto& [id, name] : names.cases) {
            if (id == *enum_case) {
                text(name);
                break;
            }
        }
        if (!compound || compound->size() == 0) {
            return true;
        }
    }
    if (compound) {
        const auto structure =
            std::holds_alternative<StructTypeValue>(values.type_copy(type).value);
        if (structure) {
            text(names.name);
            text(" {");
        } else {
            text(enum_case ? "(" : "[");
        }
        const auto count =
            structure || enum_case ? compound->size() : std::min(compound->size(), 64uz);
        for (auto index = 0uz; index < count; ++index) {
            line(depth + 1);
            if (structure) {
                text(names.fields.at(index));
                text(": ");
            }
            const auto success = compound->elements.visit([&](const auto elements) noexcept {
                return write(ExecutionValue(elements[index]), depth + 1, true);
            });
            if (!success) {
                return false;
            }
            text(",");
            if (truncated) {
                break;
            }
        }
        if (count < compound->size()) {
            line(depth + 1);
            text("...,");
        }
        if (count != 0) {
            line(depth);
        }
        text(structure ? "}" : enum_case ? ")" : "]");
        return true;
    }
    if (atom) {
        if (const auto* range = std::get_if<RangeConstant>(&atom->value)) {
            const auto canonical = values.type_copy(type);
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
            text(range->inclusive ? "..=" : "..");
            return write(
                ConstantAtom {.type = range_type->element, .value = range->end},
                depth + 1,
                true
            );
        }
        if (std::holds_alternative<NullPointerConstant>(atom->value)) {
            text("nullptr");
            return true;
        }
    }
    if (atom) {
        auto formatted =
            format_builtin_value(builtin_format_value(values, constant_fact(*atom)), {}, 16384);
        if (formatted) {
            if (nested && std::holds_alternative<CharacterConstant>(atom->value)) {
                quoted(*formatted, true);
            } else {
                text(*formatted);
            }
            return true;
        }
    }
    return false;
}

auto ExecutionDisplay::finish() noexcept -> std::string {
    return std::move(bytes);
}

} // namespace

auto display_execution_value(
    const ExecutionValueAccess& values,
    const ExecutionValue& value,
    bool nested
) noexcept -> std::optional<std::string> {
    if (!nested) {
        if (const auto text = execution_text(values, value)) {
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
    auto display = ExecutionDisplay(values);
    if (!display.write(value, 0, nested)) {
        return std::nullopt;
    }
    return display.finish();
}
