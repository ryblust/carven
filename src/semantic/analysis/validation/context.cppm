module carven:semantic.analysis.validation.context;
import :semantic.analysis.operations;
import :semantic.analysis.validation;
import :semantic.semir.constant;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace validation_detail {
class BodyContractVerifier final {
public:
    BodyContractVerifier(
        const SemIRBody& source,
        ProgramDraft& builder,
        std::span<const SemIRBody> all_bodies
    ) noexcept
        : body(source),
          draft(&builder),
          bodies(all_bodies) {}
    auto verify() noexcept -> void;

private:
    auto related_body(BodyID id) const noexcept -> const SemIRBody&;
    auto require_top_level_owners() const noexcept -> void;
    auto require_origin(ProgramOriginID origin) const noexcept -> void;
    auto require_type(TypeID type) const noexcept -> CanonicalType;
    auto require_failure_set(FailureSetID failures) const noexcept -> FailureSet;
    auto require_structure(StructID structure) const noexcept -> ConstructionStructDeclaration;
    auto require_enumeration(EnumID enumeration) const noexcept -> ConstructionEnumDeclaration;
    auto require_enum_case(EnumCaseID enum_case) const noexcept -> ConstructionEnumCaseDeclaration;
    auto require_nominal_failure_member(TypeID type) const noexcept -> void;
    auto body_callable() const noexcept -> std::optional<CallableID>;
    auto concrete_failures(FailureTermID failures) const noexcept -> FailureSetID;
    auto verify_body_inputs() const noexcept -> void;
    auto require_body_failure_set(FailureSetID failures) const noexcept -> void;
    auto verify_trees() const noexcept -> void;
    auto verify_rows() noexcept -> void;
    auto verify_patterns() const noexcept -> void;
    auto pattern_bindings(PatternID id) const noexcept -> std::vector<LocalBindingID>;
    auto signature_for_callable(CallableID id) const noexcept -> CallableSignatureID;
    auto signature_for_type(TypeID type) const noexcept -> CallableSignatureID;
    auto verify_computations() const noexcept -> void;
    auto verify_expression(const SemIRExpression& source) const noexcept -> void;
    auto verify_region(const SemIRRegion& source) const noexcept -> void;
    const SemIRBody& body;
    ProgramDraft* draft;
    std::span<const SemIRBody> bodies;
};
}
