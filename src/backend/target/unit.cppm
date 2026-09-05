module carven:backend.target.unit;

import :backend.target.item;
import :backend.target.origin;
import std;

struct TargetDirective final {
    std::string bytes;
};

struct TargetDirectiveGroup final {
    std::vector<TargetDirective> directives;
    std::optional<TargetAttribution> attribution;
};

struct TargetDirectiveInputs final {
    std::vector<TargetDirectiveGroup> prefix_groups;
    std::vector<TargetDirectiveGroup> suffix_groups;
};

struct TargetUnitSections final {
    std::vector<TargetItem> preamble;
    std::vector<TargetItem> body;
    std::vector<TargetItem> epilogue;
};

struct TargetUnitContents final {
    std::vector<TargetDirectiveGroup> directive_groups;
    TargetUnitSections sections;
};
