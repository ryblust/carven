module carven:source.standard;

import std;

enum class ProvidedModule { Core, Io, Testing, Async, Simd, Time };

enum class ProvidedExport {
    Bool,
    Char,
    Void,
    Str,
    String,
    I8,
    I16,
    I32,
    I64,
    Isize,
    U8,
    U16,
    U32,
    U64,
    Usize,
    F32,
    F64,
    Vector,
    Range,
    Assert,
    Addressof,
    Print,
    Println,
    Eprint,
    Eprintln,
    Stdin,
    StdinType,
    InputRead,
    IOError,
    Check,
    Require,
    Fail,
    Operation,
    CancelRequested,
    CancelPoint,
    YieldOnce,
    WaitReady,
    Close,
    WhenAll,
    WhenAny,
    FirstSuccessful,
    U8x16,
    Mask16,
    F32x4,
    Mask4,
    U8x32,
    Mask32,
    F32x8,
    Mask8,
    Timeout,
};

struct ProvidedExportInfo final {
    ProvidedExport identity;
    ProvidedModule module;
    std::string_view name;
    bool default_available;
};

auto provided_module_path(ProvidedModule module) noexcept -> std::string_view;
auto find_provided_module(std::string_view path) noexcept -> std::optional<ProvidedModule>;
auto provided_exports() noexcept -> std::span<const ProvidedExportInfo>;
auto provided_export(ProvidedExport identity) noexcept -> const ProvidedExportInfo&;

enum class StandardRecord { Duration, Timeout, TimeRangeError };

struct StandardRecordKey final {
    std::string_view module;
    std::string_view name;
};

auto provided_source_key(ProvidedExport identity) noexcept -> std::optional<StandardRecordKey>;

auto standard_record_key(StandardRecord record) noexcept -> StandardRecordKey;
auto provided_contract_dependencies(ProvidedExport identity) noexcept
    -> std::span<const StandardRecord>;
