module carven:semantic.hir.pattern;

import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import std;

struct HIRWildcardPattern final {};

struct HIRLiteralPattern final {
    HIRLiteralValue literal;
    HIRTypeID type;
};

struct HIROrPattern final {
    HIRTypeID type;
    std::vector<HIRPatternID> alternatives;
};

struct HIRTypeConstraintPattern final {
    HIRTypeID type;
};

struct HIRBindingPattern final {
    HIRNamedBindingTarget target;
    HIRTypeID type;
};

struct HIRCasePattern final {
    EnumCaseID enum_case;
    std::vector<HIRPatternID> payload;
};

using HIRPatternValue = std::variant<
    HIRWildcardPattern,
    HIRLiteralPattern,
    HIROrPattern,
    HIRTypeConstraintPattern,
    HIRBindingPattern,
    HIRCasePattern>;

struct HIRPattern final {
    ProgramOriginID origin;
    HIRPatternValue value;
};
