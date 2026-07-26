module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.support.file;

import :support.file;
import std;

namespace {

class TemporaryPath final {
public:
    TemporaryPath() noexcept {
        auto error = std::error_code();
        const auto temporary_directory = std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }

        static auto sequence = std::atomic<std::uint64_t>();
        value = temporary_directory
            / std::format(
                    "carven-support-test-{}-{}",
                    std::chrono::steady_clock::now().time_since_epoch().count(),
                    sequence.fetch_add(1, std::memory_order_relaxed)
            );
    }

    TemporaryPath(const TemporaryPath&) = delete;
    auto operator=(const TemporaryPath&) -> TemporaryPath& = delete;

    ~TemporaryPath() noexcept {
        auto error = std::error_code();
        if (!value.empty()) {
            std::filesystem::remove(value, error);
        }
    }

    auto path() const noexcept -> const std::filesystem::path& { return value; }
    auto ready() const noexcept -> bool { return !value.empty(); }

private:
    std::filesystem::path value;
};

} // namespace

TEST_CASE("Support file: writes and reads exact bytes") {
    const auto temporary = TemporaryPath();
    REQUIRE(temporary.ready());
    const auto expected = std::string("alpha\0beta\n", 11);

    const auto written = write_file(temporary.path(), expected);
    REQUIRE(written.has_value());
    const auto read = read_file(temporary.path(), expected.size());
    REQUIRE(read.has_value());
    CHECK_EQ(*read, expected);
}

TEST_CASE("Support file: missing paths report the failed operation") {
    const auto temporary = TemporaryPath();
    REQUIRE(temporary.ready());

    const auto read = read_file(temporary.path(), 1024);
    REQUIRE(!read.has_value());
    CHECK_EQ(read.error().operation, FileOperation::Inspect);
    CHECK(read.error().code);
}
