#pragma once

#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/testing/testing.hpp>

#include <optional>
#include <utility>

namespace carven::runtime {

inline auto run_async_test(execution::AsyncOperation<void>&& operation, SourceSite site) noexcept
    -> void {
    const auto completion = execution::drive_root(std::move(operation));
    if (completion.is_cancelled()) {
        report_test_failure(site, "test", std::nullopt, "cancellation escaped the test", "");
    }
}

} // namespace carven::runtime
