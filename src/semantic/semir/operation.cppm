module carven:semantic.semir.operation;

import std;

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

// Floating operations retain receiver precision and apply lane-wise to SIMD values.
enum class FloatIntrinsic { IsFinite, Sqrt, Abs, Min, Max, Clamp };

constexpr auto float_intrinsic_arguments(FloatIntrinsic intrinsic) noexcept -> std::size_t {
    switch (intrinsic) {
        case FloatIntrinsic::IsFinite:
        case FloatIntrinsic::Sqrt:
        case FloatIntrinsic::Abs:      return 0uz;
        case FloatIntrinsic::Min:
        case FloatIntrinsic::Max:      return 1uz;
        case FloatIntrinsic::Clamp:    return 2uz;
    }
    std::unreachable();
}
