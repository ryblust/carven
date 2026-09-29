module carven:test.harness.directory;

import std;

namespace carven::testing {

class TempDirectory final {
public:
    explicit TempDirectory(std::string_view label = "carven-test") noexcept;
    ~TempDirectory() noexcept;
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory(TempDirectory&&) = delete;
    auto operator=(const TempDirectory&) -> TempDirectory& = delete;
    auto operator=(TempDirectory&&) -> TempDirectory& = delete;

    auto path(std::string_view name = {}) const noexcept -> std::filesystem::path;

private:
    std::filesystem::path directory;
};

} // namespace carven::testing
