module carven:compiler.request;

import :source.module_path;
import :source.text;
import std;

struct CompilationModuleInput final {
    SourceID source_id;
    CanonicalModulePath module_path;
};

struct CompilationRequest final {
    std::span<const CompilationModuleInput> modules;
};
