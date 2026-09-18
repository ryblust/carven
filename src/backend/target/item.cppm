module carven:backend.target.item;

import :backend.target.decl;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.raw;
import :support.tree_value;
import std;

struct TargetItem;

struct TargetNamespace final {
    std::optional<TargetName> name;
    std::vector<TargetItem> items;
    bool closing_comment;
};

struct TargetUsing final {
    TargetName name;
    bool opens_namespace;
};

struct TargetItemCleanup;
using TargetItemValue =
    TreeValue<TargetItemCleanup, TargetDecl, TargetNamespace, TargetUsing, TargetRawFragment>;

struct TargetItemCleanup final {
    static auto clear(TargetItemValue& value) noexcept -> void;
};

struct TargetItem final {
    TargetItemValue value;
    TargetAttribution attribution;
};
