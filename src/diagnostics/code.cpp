module carven:diagnostics.code.impl;

import :diagnostics.code;
import std;

namespace {

#define CARVEN_DIAGNOSTIC_CODES(X)                                                                 \
    X(Invalid, "CV-INVALID", Error, "Invalid diagnostic code.")                                    \
    X(AccessCallMismatch, "CV-ACCESS-CALL-MISMATCH", Error, "Call access mismatch.")               \
    X(AccessCaptureConflict,                                                                       \
      "CV-ACCESS-CAPTURE-CONFLICT",                                                                \
      Error,                                                                                       \
      "Take conflicts with a mutating capture.")                                                   \
    X(AccessExpression, "CV-ACCESS-EXPRESSION", Error, "Invalid access expression.")               \
    X(AccessForeign, "CV-ACCESS-FOREIGN", Error, "Access marker on a Foreign call.")               \
    X(AccessOperationConflict,                                                                     \
      "CV-ACCESS-OPERATION-CONFLICT",                                                              \
      Error,                                                                                       \
      "One operation both takes and otherwise accesses a binding.")                                \
    X(AccessTakeOperand, "CV-ACCESS-TAKE-OPERAND", Error, "Invalid Take operand.")                 \
    X(AccessUnavailable, "CV-ACCESS-UNAVAILABLE", Error, "Unavailable binding use.")               \
    X(AccessWriteArgument, "CV-ACCESS-WRITE-ARGUMENT", Error, "Invalid Write call argument.")      \
    X(AccessImmutable, "CV-ACCESS-IMMUTABLE", Error, "Update of an immutable value.")              \
    X(AccessNotAssignable, "CV-ACCESS-NOT-ASSIGNABLE", Error, "Non-assignable Update target.")     \
    X(AccessRangeBinding, "CV-ACCESS-RANGE-BINDING", Error, "Invalid Write range binding.")        \
    X(AccessRangeIterable, "CV-ACCESS-RANGE-ITERABLE", Error, "Invalid Write range iterable.")     \
    X(AccessTextRangeBinding,                                                                      \
      "CV-ACCESS-TEXT-RANGE-BINDING",                                                              \
      Error,                                                                                       \
      "Text range bindings only support Read access.")                                             \
    X(Catalog, "CV-CATALOG", Error, "Semantic catalog failure.")                                   \
    X(CompilationInput, "CV-COMPILATION-INPUT", Error, "Invalid closed-compilation input.")        \
    X(ConstArrayExtent, "CV-CONST-ARRAY-EXTENT", Error, "Invalid constant array extent.")          \
    X(ConstCycle, "CV-CONST-CYCLE", Error, "Constant elaboration cycle.")                          \
    X(ConstDivideByZero, "CV-CONST-DIVIDE-BY-ZERO", Error, "Constant division by zero.")           \
    X(ConstEnumCase, "CV-CONST-ENUM-CASE", Error, "Invalid enum case constant.")                   \
    X(ConstEnumOverflow, "CV-CONST-ENUM-OVERFLOW", Error, "Enum case constant overflow.")          \
    X(ConstEnumRange, "CV-CONST-ENUM-RANGE", Error, "Enum case constant is out of range.")         \
    X(ConstExportedType,                                                                           \
      "CV-CONST-EXPORTED-TYPE",                                                                    \
      Error,                                                                                       \
      "Exported constant is missing its declared type.")                                           \
    X(ConstInitializer, "CV-CONST-INITIALIZER", Error, "Invalid constant initializer.")            \
    X(ConstIndexBounds, "CV-CONST-INDEX-BOUNDS", Error, "Constant array index is out of bounds.")  \
    X(ConstLiteralRange, "CV-CONST-LITERAL-RANGE", Error, "Constant literal is out of range.")     \
    X(ConstNegativeArrayExtent, "CV-CONST-NEGATIVE-ARRAY-EXTENT", Error, "Negative array extent.") \
    X(ConstOverflow, "CV-CONST-OVERFLOW", Error, "Constant arithmetic overflow.")                  \
    X(ConstShiftRange, "CV-CONST-SHIFT-RANGE", Error, "Constant shift is out of range.")           \
    X(EntryDuplicate, "CV-ENTRY-DUPLICATE", Error, "Duplicate entry point.")                       \
    X(EntryParameters, "CV-ENTRY-PARAMETERS", Error, "Invalid entry-point parameters.")            \
    X(FlowBreakOutsideLoop, "CV-FLOW-BREAK-OUTSIDE-LOOP", Error, "Break outside a loop.")          \
    X(FlowContinueOutsideLoop, "CV-FLOW-CONTINUE-OUTSIDE-LOOP", Error, "Continue outside a loop.") \
    X(FlowMissingReturn, "CV-FLOW-MISSING-RETURN", Error, "Missing return path.")                  \
    X(FlowTransferValueBranch,                                                                     \
      "CV-FLOW-TRANSFER-VALUE-BRANCH",                                                             \
      Error,                                                                                       \
      "Control transfer crosses a value-expression boundary.")                                     \
    X(FlowUnreachable, "CV-FLOW-UNREACHABLE", Warning, "Unreachable statement.")                   \
    X(FlowUnreachableMatchArm, "CV-FLOW-UNREACHABLE-MATCH-ARM", Warning, "Unreachable match arm.") \
    X(FlowValueBranchResult,                                                                       \
      "CV-FLOW-VALUE-BRANCH-RESULT",                                                               \
      Error,                                                                                       \
      "Value branch is missing a result expression.")                                              \
    X(ImportResolution, "CV-IMPORT-RESOLUTION", Error, "Import resolution failure.")               \
    X(Lexical, "CV-LEXICAL", Error, "Lexical analysis failure.")                                   \
    X(LambdaCaptureMissing,                                                                        \
      "CV-LAMBDA-CAPTURE-MISSING",                                                                 \
      Error,                                                                                       \
      "Missing explicit lambda capture.")                                                          \
    X(LambdaCaptureDuplicate, "CV-LAMBDA-CAPTURE-DUPLICATE", Error, "Duplicate lambda capture.")   \
    X(LambdaCaptureInvalid, "CV-LAMBDA-CAPTURE-INVALID", Error, "Invalid lambda capture.")         \
    X(LambdaCaptureUnused, "CV-LAMBDA-CAPTURE-UNUSED", Warning, "Unused lambda capture.")          \
    X(LambdaSignatureInference,                                                                    \
      "CV-LAMBDA-SIGNATURE-INFERENCE",                                                             \
      Error,                                                                                       \
      "Lambda signature cannot be inferred.")                                                      \
    X(LintUnusedImport, "CV-LINT-UNUSED-IMPORT", Warning, "Unused import.")                        \
    X(LintUnusedLocal, "CV-LINT-UNUSED-LOCAL", Warning, "Unused local binding.")                   \
    X(LintUnusedParameter, "CV-LINT-UNUSED-PARAMETER", Warning, "Unused function parameter.")      \
    X(MatchDuplicateAlternative,                                                                   \
      "CV-MATCH-DUPLICATE-ALTERNATIVE",                                                            \
      Error,                                                                                       \
      "Duplicate match alternative.")                                                              \
    X(MatchNonExhaustive, "CV-MATCH-NON-EXHAUSTIVE", Error, "Non-exhaustive value match.")         \
    X(MatchBindingMismatch,                                                                        \
      "CV-MATCH-BINDING-MISMATCH",                                                                 \
      Error,                                                                                       \
      "Or-pattern bindings do not agree.")                                                         \
    X(NameAmbiguous, "CV-NAME-AMBIGUOUS", Error, "Ambiguous name.")                                \
    X(NameDuplicateEnumCase, "CV-NAME-DUPLICATE-ENUM-CASE", Error, "Duplicate enum case.")         \
    X(NameDuplicateField, "CV-NAME-DUPLICATE-FIELD", Error, "Duplicate field name.")               \
    X(NameDuplicateLocal, "CV-NAME-DUPLICATE-LOCAL", Error, "Duplicate local name.")               \
    X(NameDuplicateParameter, "CV-NAME-DUPLICATE-PARAMETER", Error, "Duplicate parameter name.")   \
    X(NameUnresolved, "CV-NAME-UNRESOLVED", Error, "Unresolved value name.")                       \
    X(NameUnresolvedPattern, "CV-NAME-UNRESOLVED-PATTERN", Error, "Unresolved pattern name.")      \
    X(ParseNestingTooDeep, "CV-PARSE-NESTING-TOO-DEEP", Error, "Parser nesting limit exceeded.")   \
    X(Syntax, "CV-SYNTAX", Error, "Syntax error.")                                                 \
    X(EffectThrowType, "CV-EFFECT-THROW-TYPE", Error, "Invalid thrown failure type.")              \
    X(EffectThrowDuplicate,                                                                        \
      "CV-EFFECT-THROW-DUPLICATE",                                                                 \
      Error,                                                                                       \
      "Duplicate failure in throw clause.")                                                        \
    X(EffectThrowPublished,                                                                        \
      "CV-EFFECT-THROW-PUBLISHED",                                                                 \
      Error,                                                                                       \
      "Published callable with failures requires an explicit throw clause.")                       \
    X(EffectSignatureBound,                                                                        \
      "CV-EFFECT-SIGNATURE-BOUND",                                                                 \
      Error,                                                                                       \
      "Callable body exceeds its declared failure contract.")                                      \
    X(EffectUnmarked,                                                                              \
      "CV-EFFECT-UNMARKED",                                                                        \
      Error,                                                                                       \
      "Failure-producing expression requires explicit propagation.")                               \
    X(EffectPropagateRedundant,                                                                    \
      "CV-EFFECT-PROPAGATE-REDUNDANT",                                                             \
      Error,                                                                                       \
      "Propagation applied to an infallible value.")                                               \
    X(EffectRootUnhandled,                                                                         \
      "CV-EFFECT-ROOT-UNHANDLED",                                                                  \
      Error,                                                                                       \
      "Effect root leaves failures unhandled.")                                                    \
    X(EffectCatchNonExhaustive,                                                                    \
      "CV-EFFECT-CATCH-NON-EXHAUSTIVE",                                                            \
      Error,                                                                                       \
      "Catch does not cover every protected failure.")                                             \
    X(EffectCatchUnreachable, "CV-EFFECT-CATCH-UNREACHABLE", Warning, "Unreachable catch arm.")    \
    X(EffectRethrowContext,                                                                        \
      "CV-EFFECT-RETHROW-CONTEXT",                                                                 \
      Error,                                                                                       \
      "Rethrow outside a catch handler.")                                                          \
    X(TestArgumentCount,                                                                           \
      "CV-TEST-ARGUMENT-COUNT",                                                                    \
      Error,                                                                                       \
      "Invalid inline-test operation arguments.")                                                  \
    X(TestConditionType,                                                                           \
      "CV-TEST-CONDITION-TYPE",                                                                    \
      Error,                                                                                       \
      "Inline-test condition must have type bool.")                                                \
    X(TestDuplicateName, "CV-TEST-DUPLICATE-NAME", Error, "Duplicate test name.")                  \
    X(TestMainName, "CV-TEST-MAIN-NAME", Error, "Reserved test-main name.")                        \
    X(TestMessageType, "CV-TEST-MESSAGE-TYPE", Error, "Inline-test message must have type str.")   \
    X(TypeArrayElement, "CV-TYPE-ARRAY-ELEMENT", Error, "Incompatible array element type.")        \
    X(TypeAssignmentInteger, "CV-TYPE-ASSIGNMENT-INTEGER", Error, "Integer assignment required.")  \
    X(TypeAssignmentNumeric, "CV-TYPE-ASSIGNMENT-NUMERIC", Error, "Numeric assignment required.")  \
    X(TypeBinary, "CV-TYPE-BINARY", Error, "Incompatible binary operand types.")                   \
    X(TypeBinaryInteger, "CV-TYPE-BINARY-INTEGER", Error, "Integer binary operands required.")     \
    X(TypeBinaryNumeric, "CV-TYPE-BINARY-NUMERIC", Error, "Numeric binary operands required.")     \
    X(TypeBinaryOrdered, "CV-TYPE-BINARY-ORDERED", Error, "Ordered operands required.")            \
    X(TypeCallArgument, "CV-TYPE-CALL-ARGUMENT", Error, "Invalid call argument type.")             \
    X(TypeCallArity, "CV-TYPE-CALL-ARITY", Error, "Invalid call arity.")                           \
    X(TypeCallableViewEscape,                                                                      \
      "CV-TYPE-CALLABLE-VIEW-ESCAPE",                                                              \
      Error,                                                                                       \
      "Non-owning callable view escapes its invocation lifetime.")                                 \
    X(TypeCast, "CV-TYPE-CAST", Error, "Invalid explicit conversion.")                             \
    X(TypeConditionBool, "CV-TYPE-CONDITION-BOOL", Error, "Boolean condition required.")           \
    X(TypeConstructArity, "CV-TYPE-CONSTRUCT-ARITY", Error, "Invalid construction arity.")         \
    X(TypeConstructDuplicateField,                                                                 \
      "CV-TYPE-CONSTRUCT-DUPLICATE-FIELD",                                                         \
      Error,                                                                                       \
      "Duplicate field initializer.")                                                              \
    X(TypeConstructField, "CV-TYPE-CONSTRUCT-FIELD", Error, "Invalid field initializer type.")     \
    X(TypeConstructNotStruct,                                                                      \
      "CV-TYPE-CONSTRUCT-NOT-STRUCT",                                                              \
      Error,                                                                                       \
      "Structure construction required.")                                                          \
    X(TypeConstructUnknownField,                                                                   \
      "CV-TYPE-CONSTRUCT-UNKNOWN-FIELD",                                                           \
      Error,                                                                                       \
      "Unknown construction field.")                                                               \
    X(TypeEmptyArray, "CV-TYPE-EMPTY-ARRAY", Error, "Empty array type cannot be inferred.")        \
    X(TypeEnumUnderlying, "CV-TYPE-ENUM-UNDERLYING", Error, "Invalid enum underlying type.")       \
    X(TypeEnumEmpty, "CV-TYPE-ENUM-EMPTY", Error, "Enum must declare at least one case.")          \
    X(TypeEnumProfile, "CV-TYPE-ENUM-PROFILE", Error, "Invalid enum profile.")                     \
    X(TypeEnumDuplicateCode, "CV-TYPE-ENUM-DUPLICATE-CODE", Error, "Duplicate enum numeric code.") \
    X(TypeEnumCaseArity, "CV-TYPE-ENUM-CASE-ARITY", Error, "Invalid enum case arity.")             \
    X(TypeEnumContext,                                                                             \
      "CV-TYPE-ENUM-CONTEXT",                                                                      \
      Error,                                                                                       \
      "Contextual enum case requires an expected enum type.")                                      \
    X(TypeEqualityUnsupported,                                                                     \
      "CV-TYPE-EQUALITY-UNSUPPORTED",                                                              \
      Error,                                                                                       \
      "Type does not support equality.")                                                           \
    X(TypeForeignEscape,                                                                           \
      "CV-TYPE-FOREIGN-ESCAPE",                                                                    \
      Error,                                                                                       \
      "Opaque C++ value escapes its local operation boundary.")                                    \
    X(TypeRecursiveStorage,                                                                        \
      "CV-TYPE-RECURSIVE-STORAGE",                                                                 \
      Error,                                                                                       \
      "Type has recursive by-value storage.")                                                      \
    X(TypeVisibilityLeak,                                                                          \
      "CV-TYPE-VISIBILITY-LEAK",                                                                   \
      Error,                                                                                       \
      "A declaration surface exposes a narrower-visibility identity.")                             \
    X(TypeIfBranch, "CV-TYPE-IF-BRANCH", Error, "Incompatible if branch types.")                   \
    X(TypeIfMissingElse, "CV-TYPE-IF-MISSING-ELSE", Error, "Value if is missing else.")            \
    X(TypeIndexInteger, "CV-TYPE-INDEX-INTEGER", Error, "Integer index required.")                 \
    X(TypeLogicalBool, "CV-TYPE-LOGICAL-BOOL", Error, "Boolean logical operands required.")        \
    X(TypeMatchArm, "CV-TYPE-MATCH-ARM", Error, "Incompatible match arm types.")                   \
    X(TypeMatchConstraint, "CV-TYPE-MATCH-CONSTRAINT", Error, "Invalid match type constraint.")    \
    X(TypeMatchPattern, "CV-TYPE-MATCH-PATTERN", Error, "Invalid match pattern type.")             \
    X(TypeMemberUnresolved, "CV-TYPE-MEMBER-UNRESOLVED", Error, "Unresolved member.")              \
    X(TypeMismatch, "CV-TYPE-MISMATCH", Error, "Type mismatch.")                                   \
    X(TypeMissingReturnValue, "CV-TYPE-MISSING-RETURN-VALUE", Error, "Missing return value.")      \
    X(TypeNotCallable, "CV-TYPE-NOT-CALLABLE", Error, "Expression is not callable.")               \
    X(TypeNotIndexable, "CV-TYPE-NOT-INDEXABLE", Error, "Expression is not indexable.")            \
    X(TypeParameterAnnotation,                                                                     \
      "CV-TYPE-PARAMETER-ANNOTATION",                                                              \
      Error,                                                                                       \
      "Missing parameter type annotation.")                                                        \
    X(TypePrefixBool, "CV-TYPE-PREFIX-BOOL", Error, "Boolean prefix operand required.")            \
    X(TypePrefixInteger, "CV-TYPE-PREFIX-INTEGER", Error, "Integer prefix operand required.")      \
    X(TypePrefixNumeric, "CV-TYPE-PREFIX-NUMERIC", Error, "Numeric prefix operand required.")      \
    X(TypeRangeBinding, "CV-TYPE-RANGE-BINDING", Error, "Invalid range binding type.")             \
    X(TypeRangeBounds, "CV-TYPE-RANGE-BOUNDS", Error, "Invalid range bound types.")                \
    X(TypeRangeInteger, "CV-TYPE-RANGE-INTEGER", Error, "Integer range required.")                 \
    X(TypeRangeIterable, "CV-TYPE-RANGE-ITERABLE", Error, "Invalid range iterable.")               \
    X(TypeReturnMismatch, "CV-TYPE-RETURN-MISMATCH", Error, "Return type mismatch.")               \
    X(TypeReturnValue, "CV-TYPE-RETURN-VALUE", Error, "Unexpected return value.")                  \
    X(TypeStrMethod, "CV-TYPE-STR-METHOD", Error, "Invalid str method.")                           \
    X(TypeStrMethodArity, "CV-TYPE-STR-METHOD-ARITY", Error, "Invalid str method arity.")          \
    X(TypeStrProperty, "CV-TYPE-STR-PROPERTY", Error, "Invalid str property.")                     \
    X(TypeUnresolved, "CV-TYPE-UNRESOLVED", Error, "Unresolved type.")                             \
    X(TypeUpdateInteger, "CV-TYPE-UPDATE-INTEGER", Error, "Integer update target required.")       \
    X(TypeValueRequired,                                                                           \
      "CV-TYPE-VALUE-REQUIRED",                                                                    \
      Error,                                                                                       \
      "A value-bearing position requires a value type.")

constexpr auto diagnostic_code_registry = std::to_array<DiagnosticCodeInfo>({
#define CARVEN_DIAGNOSTIC_INFO(identifier, spelling, severity, detail)                             \
    DiagnosticCodeInfo {                                                                           \
        .name = spelling,                                                                          \
        .default_severity = DiagnosticSeverity::severity,                                          \
        .description = detail,                                                                     \
    },
    CARVEN_DIAGNOSTIC_CODES(CARVEN_DIAGNOSTIC_INFO)
#undef CARVEN_DIAGNOSTIC_INFO
});

#undef CARVEN_DIAGNOSTIC_CODES

} // namespace

auto diagnostic_code_info(DiagnosticCode code) noexcept -> DiagnosticCodeInfo {
    const auto index = static_cast<std::size_t>(code);
    return diagnostic_code_registry[index < diagnostic_code_registry.size() ? index : 0uz];
}

auto lookup_diagnostic_code(std::string_view name) noexcept -> std::optional<DiagnosticCode> {
    for (auto index = 1uz; index < diagnostic_code_registry.size(); ++index) {
        if (diagnostic_code_registry[index].name == name) {
            return static_cast<DiagnosticCode>(index);
        }
    }
    return std::nullopt;
}

auto operator==(DiagnosticCode code, std::string_view name) noexcept -> bool {
    return diagnostic_code_info(code).name == name;
}

auto operator==(std::string_view name, DiagnosticCode code) noexcept -> bool {
    return code == name;
}

auto operator==(DiagnosticCode code, const char* name) noexcept -> bool {
    return code == std::string_view(name);
}

auto operator==(const char* name, DiagnosticCode code) noexcept -> bool {
    return code == name;
}
