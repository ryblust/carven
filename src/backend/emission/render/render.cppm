module carven:backend.emission.render;

import :backend.emission.layout;
import :backend.target;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.raw;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import std;

struct StableInterfaceEmission final {};

struct SourceAttributedEmission final {
    std::string_view generated_origin;
};

using EmissionPolicy = std::variant<StableInterfaceEmission, SourceAttributedEmission>;

class TargetRenderer final {
public:
    TargetRenderer(const TargetUnit& unit, EmissionPolicy policy) noexcept;

    auto render_unit() && noexcept -> LayoutDocument;

private:
    static constexpr auto indent_width = 4uz;

    enum class TargetItemCategory : std::uint8_t {
        ForwardOrFunctionDeclaration,
        OtherDeclaration,
    };

    enum class TargetContainerKind : std::uint8_t {
        TopLevel,
        Namespace,
    };

    struct SyntaxLayouts final {
        LayoutNodeID inline_qualified;
        LayoutNodeID wrapping;
    };

    const TargetUnit& unit;
    std::string generated_origin;
    bool stable_interface;
    LayoutBuilder builder;

    auto text(std::string_view value) noexcept -> LayoutNodeID;
    auto raw(std::string_view bytes) noexcept -> LayoutNodeID;
    auto concat(std::initializer_list<LayoutNodeID> children) noexcept -> LayoutNodeID;
    auto choice(std::initializer_list<LayoutNodeID> alternatives) noexcept -> LayoutNodeID;
    auto stack(std::span<const LayoutNodeID> children, std::size_t blank_lines = 0) noexcept
        -> LayoutNodeID;
    auto delimited_sequence(
        std::span<const LayoutNodeID> children,
        std::string_view open,
        std::string_view separator,
        std::string_view close
    ) noexcept -> LayoutNodeID;
    auto delimited_list(
        std::span<const LayoutNodeID> children,
        std::string_view open,
        std::string_view close
    ) noexcept -> LayoutNodeID;
    auto braced_block(std::span<const LayoutNodeID> body) noexcept -> LayoutNodeID;
    auto render_statement_block(std::span<const TargetStmt> body) noexcept -> LayoutNodeID;
    auto directive(LayoutNodeID value) noexcept -> LayoutNodeID;
    auto with_attribution(LayoutNodeID value, const TargetAttribution& attribution) noexcept
        -> LayoutNodeID;
    auto generated_transition() noexcept -> LayoutNodeID;
    auto render_raw_fragment(const TargetRawFragment& value) noexcept -> LayoutNodeID;
    auto render_identifier(const TargetIdentifier& value) noexcept -> LayoutNodeID;
    auto qualified_sequence(
        std::span<const SyntaxLayouts> components,
        bool globally_qualified
    ) noexcept -> SyntaxLayouts;
    auto render_name_layouts(const TargetName& value) noexcept -> SyntaxLayouts;
    auto render_name(const TargetName& value) noexcept -> LayoutNodeID;
    auto render_member_function_name(const TargetMemberFunctionName& value) noexcept
        -> LayoutNodeID;
    auto render_member_function(const TargetMemberFunctionDecl& value) noexcept -> LayoutNodeID;
    auto render_parameter(const TargetParameter& value) noexcept -> LayoutNodeID;
    auto render_function_declarator(
        std::string_view prefix,
        SyntaxLayouts name,
        std::span<const TargetParameter> parameters,
        SyntaxLayouts result,
        bool const_qualified = false
    ) noexcept -> LayoutNodeID;
    auto render_trailing_return(SyntaxLayouts result, bool const_qualified) noexcept
        -> LayoutNodeID;

    auto render_type_layouts(TargetTypeID id) noexcept -> SyntaxLayouts;
    auto render_type(TargetTypeID id) noexcept -> LayoutNodeID;
    auto render_expression(
        const TargetExpr& expression,
        TargetPrecedence parent = TargetPrecedence::Lowest
    ) noexcept -> LayoutNodeID;
    auto render_statement(const TargetStmt& statement) noexcept -> LayoutNodeID;
    auto render_for_initializer(const TargetForInitializer& value) noexcept -> LayoutNodeID;
    auto render_for_step(const TargetForStep& value) noexcept -> LayoutNodeID;
    auto render_item(const TargetItem& item) noexcept -> LayoutNodeID;
    auto item_category(const TargetItem& item) const noexcept -> std::optional<TargetItemCategory>;
    auto separation_between(
        TargetItemCategory previous,
        TargetItemCategory current,
        TargetContainerKind container
    ) const noexcept -> std::size_t;
    auto render_items(std::span<const TargetItem> items, TargetContainerKind container) noexcept
        -> LayoutNodeID;
    auto render_sections(const TargetUnitSections& sections) noexcept
        -> std::optional<LayoutNodeID>;
    auto render_declaration(const TargetDecl& value) noexcept -> LayoutNodeID;
    auto render_record_member(const TargetRecordMember& value) noexcept -> LayoutNodeID;
    auto render_class_member(const TargetClassMember& value) noexcept -> LayoutNodeID;
};
