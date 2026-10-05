module carven:test.internal.support.timing;

import :support.timing;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Timing: stopped intervals are delivered once"_test = [] static noexcept {
        auto stages = std::vector<TimingStage>();
        const auto output = TimingOutput([&](TimingStage stage,
                                             std::chrono::steady_clock::duration elapsed) noexcept {
            expect(elapsed >= std::chrono::steady_clock::duration::zero());
            stages.push_back(stage);
        });
        {
            auto scope = TimingScope(output, TimingStage::SourceLoading);
            expect(stages.empty());
            scope.stop();
            expect_equal(stages.size(), 1uz);
            scope.stop();
        }
        expect(stages == std::vector {TimingStage::SourceLoading});
    };
});

} // namespace
