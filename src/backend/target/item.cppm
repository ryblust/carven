module carven:backend.target.item;

import :backend.target.decl;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.raw;
import std;

struct TargetItem;

struct TargetNamespace final {
    std::optional<TargetName> name;
    std::vector<TargetItem> items;
};

using TargetItemValue = std::variant<TargetDecl, TargetNamespace, TargetRawFragment>;

struct TargetItem final {
    TargetItemValue value;
    TargetAttribution attribution;
};
