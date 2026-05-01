export module zero.driver.pipeline;

import zero.common.source;
import zero.frontend.lexer;
import zero.frontend.parser;
import zero.backend.codegen;

import std;

export struct Driver final {
    std::uint8_t target_standard;
    bool auto_import_std = true;
    std::string_view output_dir;

    auto run_single_file(SourceFile file) const noexcept -> int;
};

export constexpr auto transpile(std::string_view source, TranspileOptions opts) noexcept -> std::string {
    const auto tokens = tokenize(source);
    const auto items  = parse(tokens, source);

    return generate(items, source, opts);
}
