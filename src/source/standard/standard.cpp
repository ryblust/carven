module carven:source.standard.impl;

import :source.standard;
import std;

namespace {

constexpr auto exports = std::array {
    ProvidedExportInfo {
        .identity = ProvidedExport::Hash,
        .provider = ProvidedModule::Collections,
        .name = "Hash",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Equal,
        .provider = ProvidedModule::Collections,
        .name = "Equal",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Bool,
        .provider = ProvidedModule::Core,
        .name = "bool",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Char,
        .provider = ProvidedModule::Core,
        .name = "char",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Void,
        .provider = ProvidedModule::Core,
        .name = "void",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Str,
        .provider = ProvidedModule::Core,
        .name = "str",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::String,
        .provider = ProvidedModule::Core,
        .name = "String",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I8,
        .provider = ProvidedModule::Core,
        .name = "i8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I16,
        .provider = ProvidedModule::Core,
        .name = "i16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I32,
        .provider = ProvidedModule::Core,
        .name = "i32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I64,
        .provider = ProvidedModule::Core,
        .name = "i64",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Isize,
        .provider = ProvidedModule::Core,
        .name = "isize",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8,
        .provider = ProvidedModule::Core,
        .name = "u8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U16,
        .provider = ProvidedModule::Core,
        .name = "u16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U32,
        .provider = ProvidedModule::Core,
        .name = "u32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U64,
        .provider = ProvidedModule::Core,
        .name = "u64",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Usize,
        .provider = ProvidedModule::Core,
        .name = "usize",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32,
        .provider = ProvidedModule::Core,
        .name = "f32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F64,
        .provider = ProvidedModule::Core,
        .name = "f64",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Vector,
        .provider = ProvidedModule::Core,
        .name = "Vector",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Range,
        .provider = ProvidedModule::Core,
        .name = "Range",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Assert,
        .provider = ProvidedModule::Core,
        .name = "assert",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Addressof,
        .provider = ProvidedModule::Core,
        .name = "addressof",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Print,
        .provider = ProvidedModule::Io,
        .name = "print",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Println,
        .provider = ProvidedModule::Io,
        .name = "println",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Eprint,
        .provider = ProvidedModule::Io,
        .name = "eprint",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Eprintln,
        .provider = ProvidedModule::Io,
        .name = "eprintln",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Check,
        .provider = ProvidedModule::Testing,
        .name = "check",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Require,
        .provider = ProvidedModule::Testing,
        .name = "require",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Fail,
        .provider = ProvidedModule::Testing,
        .name = "fail",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Operation,
        .provider = ProvidedModule::Async,
        .name = "Operation",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::CancelRequested,
        .provider = ProvidedModule::Async,
        .name = "cancel_requested",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::CancelPoint,
        .provider = ProvidedModule::Async,
        .name = "cancel_point",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::YieldOnce,
        .provider = ProvidedModule::Async,
        .name = "yield_once",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WaitReady,
        .provider = ProvidedModule::Async,
        .name = "wait_ready",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Close,
        .provider = ProvidedModule::Async,
        .name = "close",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WhenAll,
        .provider = ProvidedModule::Async,
        .name = "when_all",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WhenAny,
        .provider = ProvidedModule::Async,
        .name = "when_any",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::FirstSuccessful,
        .provider = ProvidedModule::Async,
        .name = "first_successful",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8x16,
        .provider = ProvidedModule::Simd,
        .name = "u8x16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask16,
        .provider = ProvidedModule::Simd,
        .name = "mask16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32x4,
        .provider = ProvidedModule::Simd,
        .name = "f32x4",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask4,
        .provider = ProvidedModule::Simd,
        .name = "mask4",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8x32,
        .provider = ProvidedModule::Simd,
        .name = "u8x32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask32,
        .provider = ProvidedModule::Simd,
        .name = "mask32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32x8,
        .provider = ProvidedModule::Simd,
        .name = "f32x8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask8,
        .provider = ProvidedModule::Simd,
        .name = "mask8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Timeout,
        .provider = ProvidedModule::Time,
        .name = "timeout",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::ReadInput,
        .provider = ProvidedModule::Io,
        .name = "read_input",
        .default_available = false,
        .module_local = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::MonotonicNow,
        .provider = ProvidedModule::Time,
        .name = "monotonic_now",
        .default_available = false,
        .module_local = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WaitUntil,
        .provider = ProvidedModule::Time,
        .name = "wait_until",
        .default_available = false,
        .module_local = true
    },
};

} // namespace

auto provided_module_path(ProvidedModule provider) noexcept -> std::string_view {
    switch (provider) {
        case ProvidedModule::Core:        return "crafts.carven.std.core";
        case ProvidedModule::Io:          return "crafts.carven.std.io";
        case ProvidedModule::Testing:     return "crafts.carven.std.testing";
        case ProvidedModule::Async:       return "crafts.carven.std.async";
        case ProvidedModule::Simd:        return "crafts.carven.std.simd";
        case ProvidedModule::Time:        return "crafts.carven.std.time";
        case ProvidedModule::Collections: return "crafts.carven.std.collections";
    }
    std::unreachable();
}

auto find_provided_module(std::string_view path) noexcept -> std::optional<ProvidedModule> {
    for (const auto provider :
         {ProvidedModule::Core,
          ProvidedModule::Io,
          ProvidedModule::Testing,
          ProvidedModule::Async,
          ProvidedModule::Simd,
          ProvidedModule::Time,
          ProvidedModule::Collections}) {
        if (provided_module_path(provider) == path) {
            return provider;
        }
    }
    return std::nullopt;
}

auto provided_exports() noexcept -> std::span<const ProvidedExportInfo> {
    return exports;
}

auto provided_export(ProvidedExport identity) noexcept -> const ProvidedExportInfo& {
    return exports[static_cast<std::size_t>(identity)];
}

namespace {

constexpr auto declarations = std::array {
    StandardDeclarationInfo {
        .identity = StandardDeclaration::Stdin,
        .kind = StandardDeclarationKind::Function,
        .module_path = "crafts.carven.std.io",
        .name = "stdin",
        .default_available = true
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::CharFromU32,
        .kind = StandardDeclarationKind::Function,
        .module_path = "crafts.carven.std.utf",
        .name = "char_from_u32",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::TextFromUTF8,
        .kind = StandardDeclarationKind::Function,
        .module_path = "crafts.carven.std.utf",
        .name = "from_utf8",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::Duration,
        .kind = StandardDeclarationKind::Struct,
        .module_path = "crafts.carven.std.time",
        .name = "Duration",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::TimeRangeError,
        .kind = StandardDeclarationKind::Struct,
        .module_path = "crafts.carven.std.time",
        .name = "TimeRangeError",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::Timeout,
        .kind = StandardDeclarationKind::Struct,
        .module_path = "crafts.carven.std.time",
        .name = "Timeout",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::NumberParseError,
        .kind = StandardDeclarationKind::Enum,
        .module_path = "crafts.carven.std.core",
        .name = "NumberParseError",
        .default_available = false
    }
};

} // namespace

auto standard_declarations() noexcept -> std::span<const StandardDeclarationInfo> {
    return declarations;
}

auto standard_declaration(StandardDeclaration identity) noexcept -> const StandardDeclarationInfo& {
    return declarations[static_cast<std::size_t>(identity)];
}

auto standard_associated_receiver(ProvidedExport type) noexcept
    -> std::optional<StandardAssociatedReceiver> {
    switch (type) {
        case ProvidedExport::Char:  return StandardAssociatedReceiver::Character;
        case ProvidedExport::Str:   return StandardAssociatedReceiver::Text;
        case ProvidedExport::I8:
        case ProvidedExport::I16:
        case ProvidedExport::I32:
        case ProvidedExport::I64:
        case ProvidedExport::Isize:
        case ProvidedExport::U8:
        case ProvidedExport::U16:
        case ProvidedExport::U32:
        case ProvidedExport::U64:
        case ProvidedExport::Usize:
        case ProvidedExport::F32:
        case ProvidedExport::F64:   return StandardAssociatedReceiver::Number;
        default:                    return std::nullopt;
    }
}

auto find_standard_associated(StandardAssociatedReceiver receiver, std::string_view name) noexcept
    -> const StandardAssociatedEntry* {
    static constexpr auto entries = std::array {
        StandardAssociatedEntry {
            .receiver = StandardAssociatedReceiver::Character,
            .name = "from_u32",
            .kind = StandardAssociatedKind::SourceFunction,
            .declaration = StandardDeclaration::CharFromU32
        },
        StandardAssociatedEntry {
            .receiver = StandardAssociatedReceiver::Text,
            .name = "from_utf8",
            .kind = StandardAssociatedKind::SourceFunction,
            .declaration = StandardDeclaration::TextFromUTF8
        },
        StandardAssociatedEntry {
            .receiver = StandardAssociatedReceiver::Number,
            .name = "parse",
            .kind = StandardAssociatedKind::NumberParse,
            .declaration = StandardDeclaration::NumberParseError
        }
    };
    const auto found = std::ranges::find_if(entries, [&](const auto& entry) noexcept {
        return entry.receiver == receiver && entry.name == name;
    });
    return found == entries.end() ? nullptr : std::addressof(*found);
}
