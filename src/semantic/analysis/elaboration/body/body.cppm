module carven:semantic.analysis.elaboration.body;

import :frontend.ast.decl;
import :semantic.analysis.elaboration.scopes;
import :semantic.hir.decl;
import :semantic.hir.ids;
import std;

class ModuleAnalysis;

struct ReturnObservation final {
    ProgramOriginID origin;
    std::optional<HIRTypeID> type;
};

class ReturnTypeInference final {
public:
    auto record(ReturnObservation observation) noexcept -> void;
    auto observations() const noexcept -> std::span<const ReturnObservation>;

private:
    std::vector<ReturnObservation> values;
};

class ValueBranchState final {
public:
    auto enter(std::uint32_t loop_depth) noexcept -> void;
    auto reject_transfer() noexcept -> void;
    auto rejected_transfer() const noexcept -> bool;
    auto contains_loop(std::uint32_t loop_depth) const noexcept -> bool;

private:
    std::optional<std::uint32_t> entering_loop_depth;
    bool transfer_rejected = false;
};

struct BodyControl final {
    std::optional<HIRTypeID> expected_return;
    std::uint32_t loop_depth;
    bool permits_rethrow;
    ReturnTypeInference* return_inference;
    ValueBranchState* value_branch;
};

auto root_body_control(std::optional<HIRTypeID> expected_return = std::nullopt) noexcept
    -> BodyControl;

auto inferred_body_control(ReturnTypeInference& inference) noexcept -> BodyControl;

auto inside_loop(BodyControl context) noexcept -> BodyControl;

auto inside_value_branch(BodyControl context, ValueBranchState& state) noexcept -> BodyControl;

auto inside_catch(BodyControl context) noexcept -> BodyControl;

class BodyElaborator final {
public:
    explicit BodyElaborator(ModuleAnalysis& module_analysis) noexcept;
    BodyElaborator(const BodyElaborator&) = delete;
    BodyElaborator(BodyElaborator&&) = delete;
    ~BodyElaborator() = default;

    auto operator=(const BodyElaborator&) -> BodyElaborator& = delete;
    auto operator=(BodyElaborator&&) -> BodyElaborator& = delete;

    auto elaborate_function(const ASTFunctionDecl& function, CallableID callable) noexcept
        -> HIRBody;
    auto elaborate_test(ASTBlockID body, HIRTypeID result) noexcept -> HIRBlockID;

private:
    ModuleAnalysis& module_analysis;
    ScopeStack scopes;
};
