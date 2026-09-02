module carven:semantic.hir.interop;

import :source.provenance.ids;

enum class HIRCppHeaderDelimiter {
    AngleBrackets,
    Quotes,
};

struct HIRCppHeaderDependency final {
    HIRCppHeaderDelimiter delimiter;
    ProgramSpellingID name;
};

struct HIRCppImportImplementation final {
    ProgramOriginID form_origin;
};
