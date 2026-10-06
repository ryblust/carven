module carven:backend.target.unit;

import :backend.target.header;
import :backend.target.item;
import :backend.target.origin;
import std;

struct TargetPragmaOnceDirective final {};

struct TargetIncludeDirective final {
    TargetHeader header;
};

struct TargetDirective final {
    std::variant<TargetIncludeDirective, TargetPragmaOnceDirective> value;
};

struct TargetDirectiveGroup final {
    std::vector<TargetDirective> directives;
    std::optional<TargetAttribution> attribution;
};

struct TargetHeaderOccurrence final {
    TargetHeader header;
    TargetRawSourceAttribution attribution;
};

struct TargetDirectiveInputs final {
    bool pragma_once;
    std::vector<TargetHeaderRequirement> requirements;
    std::vector<TargetHeaderOccurrence> native_headers;
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
