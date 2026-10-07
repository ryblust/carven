module carven:semantic.semir.passing;

import :semantic.semir.ids;
import std;

// A retained parameter holder must remain the caller's storage. This fact does
// not predict native copy traits or introduce another source access mode.
struct ReadPassingFacts final {
    std::map<CallableID, std::vector<bool>> stable_parameters;
};
