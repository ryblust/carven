module carven:compilation.request;

import :source.module_path;
import :source.text;
import std;

struct CompilationInput final {
    SourceID source_id;
    CanonicalModulePath module_path;
};

enum class TestEmissionMode {
    None,
    ExternalRunner,
    DefaultRunner,
};

struct CompilationRequest final {
    std::span<const CompilationInput> inputs;
};

struct ContentAddressedLinkageForm final {};

struct ExplicitLinkageForm final {
    std::string domain;
};

using LinkageIdentity = std::variant<ContentAddressedLinkageForm, ExplicitLinkageForm>;

struct TargetGenerationRequest final {
    TestEmissionMode tests;
    LinkageIdentity linkage;
};
