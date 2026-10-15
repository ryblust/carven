module carven:test.internal.source.fixture.impl;

import :source.module_path;
import :test.harness.framework;
import :test.internal.source.fixture;
import std;

auto append_standard_sources(
    SourceManager& sources,
    std::vector<SourceModuleInput>& inputs,
    std::span<const std::string_view> modules
) noexcept -> void {
    for (const auto name : modules) {
        auto relative = std::string(name);
        std::ranges::replace(relative, '.', '/');
        const auto filename = std::format("crafts/carven/std/{}.cv", relative);
        auto file = std::ifstream(filename);
        require(file.is_open()).note("source = ", filename);
        auto text = std::string(std::istreambuf_iterator<char>(file), {});
        const auto id = sources.append_virtual(filename, std::move(text));
        require(id.has_value());
        inputs.push_back({
            .source_id = *id,
            .module_path =
                *CanonicalModulePath::from_value(std::format("crafts.carven.std.{}", name)),
        });
    }
}
