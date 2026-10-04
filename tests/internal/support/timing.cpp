module carven:test.internal.support.timing;

import :support.timing;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Timing: stopped intervals are delivered once", [] static noexcept {
        auto stages = std::vector<TimingStage>();
        const auto output = TimingOutput([&](TimingStage stage,
                                             std::chrono::steady_clock::duration elapsed) noexcept {
            ct::expect(elapsed >= std::chrono::steady_clock::duration::zero());
            stages.push_back(stage);
        });
        {
            auto scope = TimingScope(output, TimingStage::SourceLoading);
            ct::expect(stages.empty());
            scope.stop();
            ct::expect_equal(stages.size(), 1uz);
            scope.stop();
        }
        ct::expect(stages == std::vector {TimingStage::SourceLoading});
    });
});

} // namespace
