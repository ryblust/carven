module carven:test.internal.source.fixture;

import :source.batch;
import :source.manager;
import std;

auto append_standard_sources(
    SourceManager& sources,
    std::vector<SourceModuleInput>& inputs,
    std::span<const std::string_view> modules
) noexcept -> void;
