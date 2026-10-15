module carven:source.standard.impl;

import :source.standard;
import std;

namespace {

constexpr auto exports = std::array {
    ProvidedExportInfo {
        .identity = ProvidedExport::Bool,
        .module = ProvidedModule::Core,
        .name = "bool",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Char,
        .module = ProvidedModule::Core,
        .name = "char",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Void,
        .module = ProvidedModule::Core,
        .name = "void",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Str,
        .module = ProvidedModule::Core,
        .name = "str",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::String,
        .module = ProvidedModule::Core,
        .name = "String",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I8,
        .module = ProvidedModule::Core,
        .name = "i8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I16,
        .module = ProvidedModule::Core,
        .name = "i16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I32,
        .module = ProvidedModule::Core,
        .name = "i32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I64,
        .module = ProvidedModule::Core,
        .name = "i64",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Isize,
        .module = ProvidedModule::Core,
        .name = "isize",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8,
        .module = ProvidedModule::Core,
        .name = "u8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U16,
        .module = ProvidedModule::Core,
        .name = "u16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U32,
        .module = ProvidedModule::Core,
        .name = "u32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U64,
        .module = ProvidedModule::Core,
        .name = "u64",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Usize,
        .module = ProvidedModule::Core,
        .name = "usize",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32,
        .module = ProvidedModule::Core,
        .name = "f32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F64,
        .module = ProvidedModule::Core,
        .name = "f64",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Vector,
        .module = ProvidedModule::Core,
        .name = "Vector",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Range,
        .module = ProvidedModule::Core,
        .name = "Range",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Assert,
        .module = ProvidedModule::Core,
        .name = "assert",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Addressof,
        .module = ProvidedModule::Core,
        .name = "addressof",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Print,
        .module = ProvidedModule::Io,
        .name = "print",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Println,
        .module = ProvidedModule::Io,
        .name = "println",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Eprint,
        .module = ProvidedModule::Io,
        .name = "eprint",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Eprintln,
        .module = ProvidedModule::Io,
        .name = "eprintln",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Check,
        .module = ProvidedModule::Testing,
        .name = "check",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Require,
        .module = ProvidedModule::Testing,
        .name = "require",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Fail,
        .module = ProvidedModule::Testing,
        .name = "fail",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Operation,
        .module = ProvidedModule::Async,
        .name = "Operation",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::CancelRequested,
        .module = ProvidedModule::Async,
        .name = "cancel_requested",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::CancelPoint,
        .module = ProvidedModule::Async,
        .name = "cancel_point",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::YieldOnce,
        .module = ProvidedModule::Async,
        .name = "yield_once",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WaitReady,
        .module = ProvidedModule::Async,
        .name = "wait_ready",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Close,
        .module = ProvidedModule::Async,
        .name = "close",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WhenAll,
        .module = ProvidedModule::Async,
        .name = "when_all",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WhenAny,
        .module = ProvidedModule::Async,
        .name = "when_any",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::FirstSuccessful,
        .module = ProvidedModule::Async,
        .name = "first_successful",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8x16,
        .module = ProvidedModule::Simd,
        .name = "u8x16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask16,
        .module = ProvidedModule::Simd,
        .name = "mask16",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32x4,
        .module = ProvidedModule::Simd,
        .name = "f32x4",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask4,
        .module = ProvidedModule::Simd,
        .name = "mask4",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8x32,
        .module = ProvidedModule::Simd,
        .name = "u8x32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask32,
        .module = ProvidedModule::Simd,
        .name = "mask32",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32x8,
        .module = ProvidedModule::Simd,
        .name = "f32x8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask8,
        .module = ProvidedModule::Simd,
        .name = "mask8",
        .default_available = true,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Timeout,
        .module = ProvidedModule::Time,
        .name = "timeout",
        .default_available = false,
        .module_local = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::ReadInput,
        .module = ProvidedModule::Io,
        .name = "read_input",
        .default_available = false,
        .module_local = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::MonotonicNow,
        .module = ProvidedModule::Time,
        .name = "monotonic_now",
        .default_available = false,
        .module_local = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WaitUntil,
        .module = ProvidedModule::Time,
        .name = "wait_until",
        .default_available = false,
        .module_local = true
    },
};

} // namespace

auto provided_module_path(ProvidedModule module) noexcept -> std::string_view {
    switch (module) {
        case ProvidedModule::Core:    return "crafts.carven.std.core";
        case ProvidedModule::Io:      return "crafts.carven.std.io";
        case ProvidedModule::Testing: return "crafts.carven.std.testing";
        case ProvidedModule::Async:   return "crafts.carven.std.async";
        case ProvidedModule::Simd:    return "crafts.carven.std.simd";
        case ProvidedModule::Time:    return "crafts.carven.std.time";
    }
    std::unreachable();
}

auto find_provided_module(std::string_view path) noexcept -> std::optional<ProvidedModule> {
    for (const auto module :
         {ProvidedModule::Core,
          ProvidedModule::Io,
          ProvidedModule::Testing,
          ProvidedModule::Async,
          ProvidedModule::Simd,
          ProvidedModule::Time}) {
        if (provided_module_path(module) == path) {
            return module;
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
        .module = "crafts.carven.std.io",
        .name = "stdin",
        .default_available = true
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::CharFromU32,
        .kind = StandardDeclarationKind::Function,
        .module = "crafts.carven.std.utf",
        .name = "char_from_u32",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::TextFromUTF8,
        .kind = StandardDeclarationKind::Function,
        .module = "crafts.carven.std.utf",
        .name = "from_utf8",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::Duration,
        .kind = StandardDeclarationKind::Struct,
        .module = "crafts.carven.std.time",
        .name = "Duration",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::RangeError,
        .kind = StandardDeclarationKind::Struct,
        .module = "crafts.carven.std.time",
        .name = "RangeError",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::Timeout,
        .kind = StandardDeclarationKind::Struct,
        .module = "crafts.carven.std.time",
        .name = "Timeout",
        .default_available = false
    },
    StandardDeclarationInfo {
        .identity = StandardDeclaration::NumberParseError,
        .kind = StandardDeclarationKind::Enum,
        .module = "crafts.carven.std.core",
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
