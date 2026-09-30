module carven:semantic.semir.stage;

import :semantic.semir.ids;
import std;

// A function with static parameters specialized for one list of static
// arguments. The instance is a callable of its own: its signature holds the
// runtime parameters and its body resolves every static selection, expansion,
// and local.
struct StaticInstance final {
    FunctionID function;
    // Ordered by the signature's Static parameter positions.
    std::vector<ConstantID> arguments;
    CallableID callable;
};
