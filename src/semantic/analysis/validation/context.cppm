module carven:semantic.analysis.validation.context;
import :semantic.analysis.operations;
import :semantic.analysis.validation;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
import std;

class BodyContractVerifier final {
public:
    BodyContractVerifier(const SemIRBody& source, const SemIRProgram& semantic) noexcept
        : body(source),
          program(semantic) {}

    auto verify() noexcept -> void;

private:
    auto related_body(BodyID id) const noexcept -> const SemIRBody&;
    auto require_top_level_owners() const noexcept -> void;
    auto require_origin(ProgramOriginID origin) const noexcept -> void;
    auto require_type(TypeID type) const noexcept -> CanonicalType;
    auto require_failure_set(FailureSetID failures) const noexcept -> FailureSet;
    auto require_structure(StructID structure) const noexcept -> StructDeclaration;
    auto require_enumeration(EnumID enumeration) const noexcept -> EnumDeclaration;
    auto require_enum_case(EnumCaseID enum_case) const noexcept -> EnumCaseDeclaration;
    auto require_nominal_failure_member(TypeID type) const noexcept -> void;
    auto body_callable() const noexcept -> std::optional<CallableID>;
    auto verify_body_inputs() const noexcept -> void;
    auto require_body_failure_set(FailureSetID failures) const noexcept -> void;
    auto verify_lifetimes() const noexcept -> void;
    auto verify_rows() noexcept -> void;
    auto verify_patterns() const noexcept -> void;
    auto compatible_pattern_type(TypeID left, TypeID right) const noexcept -> bool;
    auto pattern_bindings(PatternID id) const noexcept -> std::vector<LocalBindingID>;
    auto signature_for_callable(CallableID id) const noexcept -> CallableSignatureID;
    auto signature_for_type(TypeID type) const noexcept -> CallableSignatureID;
    auto verify_computations() const noexcept -> void;
    auto verify_expression(const SemanticExpression& source) const noexcept -> void;
    auto verify_region(const SemanticRegion& source) const noexcept -> void;
    const SemIRBody& body;
    const SemIRProgram& program;
};
