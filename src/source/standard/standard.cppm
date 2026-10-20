module carven:source.standard;

import std;

enum class ProvidedModule { Core, Io, Testing, Async, Simd, Time, Collections };

enum class ProvidedExport {
    Hash,
    Equal,
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
    ReadInput,
    MonotonicNow,
    WaitUntil,
};

struct ProvidedExportInfo final {
    ProvidedExport identity;
    ProvidedModule provider;
    std::string_view name;
    bool default_available;
    bool module_local;
};

auto provided_module_path(ProvidedModule provider) noexcept -> std::string_view;
auto find_provided_module(std::string_view path) noexcept -> std::optional<ProvidedModule>;
auto provided_exports() noexcept -> std::span<const ProvidedExportInfo>;
auto provided_export(ProvidedExport identity) noexcept -> const ProvidedExportInfo&;

enum class StandardDeclaration {
    Stdin,
    CharFromU32,
    TextFromUTF8,
    Duration,
    TimeRangeError,
    Timeout,
    NumberParseError
};

enum class StandardDeclarationKind { Function, Struct, Enum };

struct StandardDeclarationInfo final {
    StandardDeclaration identity;
    StandardDeclarationKind kind;
    std::string_view module_path;
    std::string_view name;
    bool default_available;
};

auto standard_declarations() noexcept -> std::span<const StandardDeclarationInfo>;
auto standard_declaration(StandardDeclaration identity) noexcept -> const StandardDeclarationInfo&;

enum class StandardAssociatedReceiver { Character, Text, Number };
enum class StandardAssociatedKind { SourceFunction, NumberParse };

struct StandardAssociatedEntry final {
    StandardAssociatedReceiver receiver;
    std::string_view name;
    StandardAssociatedKind kind;
    StandardDeclaration declaration;
};

auto standard_associated_receiver(ProvidedExport type) noexcept
    -> std::optional<StandardAssociatedReceiver>;

auto find_standard_associated(StandardAssociatedReceiver receiver, std::string_view name) noexcept
    -> const StandardAssociatedEntry*;
