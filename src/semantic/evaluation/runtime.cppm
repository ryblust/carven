module carven:semantic.evaluation.runtime;

import std;

// Shared native carriers keep their header ABI and remain reachable across internal
// partitions. One boundary avoids duplicate platform declarations after import std.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
extern "C++" {
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/hosting/object.hpp>
#include <carven/runtime/call/passing.hpp>
#include <carven/runtime/text/string.hpp>
#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/hosting/service.hpp>
}
#pragma clang diagnostic pop
