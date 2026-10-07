module carven:semantic.semir.async;

import :semantic.semir.ids;
import std;

enum class AsyncIntrinsic { CancelChild, CancellationRequested, CancellationPoint, YieldOnce };

struct SemAwait;

// Occurrences borrow the final published bodies owned by the same program.
struct AsyncCancellationFacts final {
    std::vector<bool> callable_completion;
    std::map<BodyID, std::map<const SemAwait*, bool>> await_completion;
};
