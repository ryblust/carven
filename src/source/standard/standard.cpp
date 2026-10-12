module carven:source.standard.impl;

import :source.standard;
import std;

namespace {

constexpr auto exports = std::array {
    ProvidedExportInfo {
        .identity = ProvidedExport::Bool,
        .module = ProvidedModule::Core,
        .name = "bool",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Char,
        .module = ProvidedModule::Core,
        .name = "char",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Void,
        .module = ProvidedModule::Core,
        .name = "void",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Str,
        .module = ProvidedModule::Core,
        .name = "str",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::String,
        .module = ProvidedModule::Core,
        .name = "String",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I8,
        .module = ProvidedModule::Core,
        .name = "i8",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I16,
        .module = ProvidedModule::Core,
        .name = "i16",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I32,
        .module = ProvidedModule::Core,
        .name = "i32",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::I64,
        .module = ProvidedModule::Core,
        .name = "i64",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Isize,
        .module = ProvidedModule::Core,
        .name = "isize",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8,
        .module = ProvidedModule::Core,
        .name = "u8",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U16,
        .module = ProvidedModule::Core,
        .name = "u16",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U32,
        .module = ProvidedModule::Core,
        .name = "u32",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U64,
        .module = ProvidedModule::Core,
        .name = "u64",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Usize,
        .module = ProvidedModule::Core,
        .name = "usize",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32,
        .module = ProvidedModule::Core,
        .name = "f32",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F64,
        .module = ProvidedModule::Core,
        .name = "f64",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Vector,
        .module = ProvidedModule::Core,
        .name = "Vector",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Range,
        .module = ProvidedModule::Core,
        .name = "Range",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Assert,
        .module = ProvidedModule::Core,
        .name = "assert",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Addressof,
        .module = ProvidedModule::Core,
        .name = "addressof",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Print,
        .module = ProvidedModule::Io,
        .name = "print",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Println,
        .module = ProvidedModule::Io,
        .name = "println",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Eprint,
        .module = ProvidedModule::Io,
        .name = "eprint",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Eprintln,
        .module = ProvidedModule::Io,
        .name = "eprintln",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Stdin,
        .module = ProvidedModule::Io,
        .name = "stdin",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::StdinType,
        .module = ProvidedModule::Io,
        .name = "Stdin",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::InputRead,
        .module = ProvidedModule::Io,
        .name = "Read",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::IOError,
        .module = ProvidedModule::Io,
        .name = "IOError",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Check,
        .module = ProvidedModule::Testing,
        .name = "check",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Require,
        .module = ProvidedModule::Testing,
        .name = "require",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Fail,
        .module = ProvidedModule::Testing,
        .name = "fail",
        .default_available = true
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Operation,
        .module = ProvidedModule::Async,
        .name = "Operation",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::CancelRequested,
        .module = ProvidedModule::Async,
        .name = "cancel_requested",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::CancelPoint,
        .module = ProvidedModule::Async,
        .name = "cancel_point",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::YieldOnce,
        .module = ProvidedModule::Async,
        .name = "yield_once",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WaitReady,
        .module = ProvidedModule::Async,
        .name = "wait_ready",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Close,
        .module = ProvidedModule::Async,
        .name = "close",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WhenAll,
        .module = ProvidedModule::Async,
        .name = "when_all",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::WhenAny,
        .module = ProvidedModule::Async,
        .name = "when_any",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::FirstSuccessful,
        .module = ProvidedModule::Async,
        .name = "first_successful",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8x16,
        .module = ProvidedModule::Simd,
        .name = "u8x16",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask16,
        .module = ProvidedModule::Simd,
        .name = "mask16",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32x4,
        .module = ProvidedModule::Simd,
        .name = "f32x4",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask4,
        .module = ProvidedModule::Simd,
        .name = "mask4",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::U8x32,
        .module = ProvidedModule::Simd,
        .name = "u8x32",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask32,
        .module = ProvidedModule::Simd,
        .name = "mask32",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::F32x8,
        .module = ProvidedModule::Simd,
        .name = "f32x8",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Mask8,
        .module = ProvidedModule::Simd,
        .name = "mask8",
        .default_available = false
    },
    ProvidedExportInfo {
        .identity = ProvidedExport::Timeout,
        .module = ProvidedModule::Time,
        .name = "timeout",
        .default_available = false
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

auto provided_source_key(ProvidedExport identity) noexcept -> std::optional<StandardRecordKey> {
    switch (identity) {
        case ProvidedExport::Stdin:
            return StandardRecordKey {"crafts.carven.std.io.stream", "stdin"};
        case ProvidedExport::StdinType:
            return StandardRecordKey {"crafts.carven.std.io.stream", "Stdin"};
        case ProvidedExport::InputRead:
            return StandardRecordKey {"crafts.carven.std.io.stream", "Read"};
        case ProvidedExport::IOError:
            return StandardRecordKey {"crafts.carven.std.io.stream", "IOError"};
        default: return std::nullopt;
    }
}

auto standard_record_key(StandardRecord record) noexcept -> StandardRecordKey {
    switch (record) {
        case StandardRecord::Duration: return {"crafts.carven.std.time.duration", "Duration"};
        case StandardRecord::Timeout:  return {"crafts.carven.std.time.timeout", "Timeout"};
        case StandardRecord::TimeRangeError:
            return {"crafts.carven.std.time.duration", "RangeError"};
    }
    std::unreachable();
}

auto provided_contract_dependencies(ProvidedExport identity) noexcept
    -> std::span<const StandardRecord> {
    static constexpr auto time = std::array {
        StandardRecord::Duration,
        StandardRecord::Timeout,
        StandardRecord::TimeRangeError
    };
    return identity == ProvidedExport::Timeout ? std::span<const StandardRecord>(time)
                                               : std::span<const StandardRecord>();
}
