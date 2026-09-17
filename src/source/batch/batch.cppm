module carven:source.batch;

import :source.module_path;
import :source.text;
import std;

struct SourceModuleInput final {
    SourceID source_id;
    CanonicalModulePath module_path;
};

struct SourceBatch final {
    std::span<const SourceModuleInput> modules;
};
