module carven:backend.target.item;

import :backend.target.decl;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.raw;
import std;

enum class TargetVerticalSeparation {
    Line,
    BlankLine,
};

struct TargetNamespace final {
    std::optional<TargetName> name;
    std::vector<TargetItemID> items;
    TargetVerticalSeparation body_separation;
};

struct TargetItemGroup final {
    std::vector<TargetItemID> items;
    TargetVerticalSeparation separation;
};

using TargetItemValue =
    std::variant<TargetDecl, TargetNamespace, TargetRawFragment, TargetItemGroup>;

struct TargetItem final {
    TargetItemValue value;
    TargetAttribution attribution;
};
