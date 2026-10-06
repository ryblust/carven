module carven:backend.target.directives.impl;

import :backend.target.directives;
import :backend.target.header;
import :backend.target.origin;
import :backend.target.unit;
import :support.invariant;
import std;

namespace {

auto require_valid_header(const TargetHeader& header) noexcept -> void {
    if (header.path.empty()
        || header.path.find_first_of(std::string_view("\0\r\n", 3)) != std::string::npos
        || (header.delimiter == TargetHeaderDelimiter::AngleBrackets && header.path.contains('>'))
        || (header.delimiter == TargetHeaderDelimiter::Quotes && header.path.contains('"'))) {
        invariant_violation("invalid target header reference");
    }
}

auto compiler_group(std::vector<TargetDirective> directives) noexcept -> TargetDirectiveGroup {
    return {
        .directives = std::move(directives),
        .attribution =
            TargetCompilerOwnedAttribution {.reason = TargetCompilerReason::ArtifactScaffolding},
    };
}

} // namespace

auto plan_target_directives(
    TargetDirectiveInputs inputs,
    std::span<const TargetHeaderRequirement> dependencies
) noexcept -> std::vector<TargetDirectiveGroup> {
    inputs.requirements.append_range(dependencies);
    auto requirements = std::map<TargetHeader, TargetHeaderGroup>();
    for (auto& requirement : inputs.requirements) {
        require_valid_header(requirement.header);
        const auto [entry, inserted] =
            requirements.try_emplace(std::move(requirement.header), requirement.group);
        if (!inserted && entry->second != requirement.group) {
            invariant_violation("target header requirements disagree on their group");
        }
    }
    auto ordered = std::vector<TargetHeaderRequirement>();
    for (const auto& [header, group] : requirements) {
        ordered.push_back({.header = header, .group = group});
    }
    std::ranges::sort(ordered, [](const auto& left, const auto& right) static noexcept {
        return left.group == right.group ? left.header < right.header : left.group < right.group;
    });

    auto result = std::vector<TargetDirectiveGroup>();
    if (inputs.pragma_once) {
        result.push_back(compiler_group({TargetDirective {.value = TargetPragmaOnceDirective {}}}));
    }
    auto cursor = 0uz;
    const auto append_requirements = [&](TargetHeaderGroup group) noexcept {
        auto directives = std::vector<TargetDirective>();
        while (cursor < ordered.size() && ordered[cursor].group == group) {
            directives.push_back(
                {.value = TargetIncludeDirective {.header = std::move(ordered[cursor].header)}}
            );
            ++cursor;
        }
        if (!directives.empty()) {
            result.push_back(compiler_group(std::move(directives)));
        }
    };
    append_requirements(TargetHeaderGroup::Associated);
    for (auto& occurrence : inputs.native_headers) {
        require_valid_header(occurrence.header);
        result.push_back({
            .directives = {TargetDirective {
                .value = TargetIncludeDirective {.header = std::move(occurrence.header)}
            }},
            .attribution = std::move(occurrence.attribution),
        });
    }
    for (const auto group : {
             TargetHeaderGroup::Runtime,
             TargetHeaderGroup::Generated,
             TargetHeaderGroup::StandardLibrary,
         }) {
        append_requirements(group);
    }
    return result;
}
