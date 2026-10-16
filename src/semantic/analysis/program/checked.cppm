module carven:semantic.analysis.program.checked;

import :semantic.semir.program;
import std;

struct CheckedUnit final {
    SemIRProgram program;
    // Producers retain construction rows when later source analysis will import them.
    // Generation-only consumers leave this empty.
    std::vector<std::byte> definitions;
    bool cacheable;
};
