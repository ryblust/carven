module carven:backend.target.unit;

import :artifacts;
import :backend.target.ids;
import std;

struct TargetDirective final {
    std::string bytes;
};

struct TargetDirectiveGroup final {
    std::vector<TargetDirective> directives;
};

struct TargetUnitSections final {
    std::vector<TargetItemID> preamble;
    std::vector<TargetItemID> body;
    std::vector<TargetItemID> epilogue;
};

struct TargetUnitRoot final {
    std::string logical_path;
    GeneratedArtifactRole role;
    ArtifactSourceMappingPolicy source_mapping;
    std::vector<TargetDirectiveGroup> directive_groups;
    TargetUnitSections sections;
};
