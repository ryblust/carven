export module zero.driver.pipeline;

import zero.common.source;
import zero.frontend.parser.ast;
import std;

export struct TranspileResult final {
    std::string output;
    std::vector<ParseError> errors;

    auto has_errors() const noexcept -> bool;
};

export struct Driver final {
    std::uint8_t language_standard = 23;
    bool default_include_std = true;
    bool only_tokens = false;
    bool only_ast = false;
    std::filesystem::path output_dir = ".zero/build";
#if defined(_WIN32)
    std::string compiler = "clang-cl";
#else
    std::string compiler = "c++";
#endif
    std::vector<std::string_view> input_files;

    auto parse_flags(std::span<const char* const> args) noexcept -> bool;
    auto has_input_files() const noexcept -> bool { return !input_files.empty(); }
    auto transpile(SourceFile file) const noexcept -> TranspileResult;
    auto run_single_file(SourceFile file) const noexcept -> int;
};
