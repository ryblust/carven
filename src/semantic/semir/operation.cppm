module carven:semantic.semir.operation;

enum class AccessMode {
    Read,
    Write,
    Take,
};

enum class UnaryOperator {
    LogicalNot,
    Negate,
    BitwiseNot,
};

enum class BinaryOperator {
    BitwiseOr,
    BitwiseXor,
    BitwiseAnd,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LeftShift,
    RightShift,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
};
