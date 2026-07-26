module carven:backend.target.unit;

import :backend.target.ids;
import :backend.target.name;
import std;

struct TargetUnitSections final {
    std::vector<TargetItemID> preamble;
    std::vector<TargetItemID> body;
    std::vector<TargetItemID> epilogue;
};

struct TargetInterfaceComponentUnit final {
    std::string logical_path;
    std::vector<std::string> prerequisite_header_paths;
    TargetUnitSections sections;
};

struct TargetModuleImplementationUnit final {
    std::string logical_path;
    std::vector<std::string> interface_header_paths;
    bool testing_support;
    TargetUnitSections sections;
};

struct TargetTestEntryUnit final {
    std::string logical_path;
    std::vector<TargetItemID> items;
};

using TargetUnitRoot =
    std::variant<TargetInterfaceComponentUnit, TargetModuleImplementationUnit, TargetTestEntryUnit>;
