module carven:support.input;

import std;

enum class HostInputMode { Chunk, Line, All };

struct HostInputRequest final {
    HostInputMode mode;
    std::size_t limit;
    std::size_t scanned;
};

// Valid until the next fill or consume on the cursor.
struct HostInputPacket final {
    std::span<const std::uint8_t> bytes;
    bool end;
};

struct HostInputSystemError final {
    std::int32_t code;
};

struct HostInputLimitError final {};

using HostInputResult = std::variant<HostInputPacket, HostInputSystemError, HostInputLimitError>;

class HostInputCursor final {
public:
    static auto process() noexcept -> HostInputCursor&;
    HostInputCursor(const HostInputCursor&) = delete;
    auto operator=(const HostInputCursor&) -> HostInputCursor& = delete;
    auto ready(HostInputRequest& request) noexcept -> std::optional<HostInputResult>;
    auto consume(std::size_t count) noexcept -> void;
    auto fill() noexcept -> void;
    auto wait(std::optional<std::int64_t> deadline) noexcept -> void;

private:
    HostInputCursor() = default;
    auto view(std::size_t count, bool end = false) const noexcept -> HostInputResult;

    std::vector<std::uint8_t> pending;
    bool ended = false;
    std::int32_t error = 0;
#if defined(_WIN32)
    wchar_t high_surrogate = 0;
    bool console_lines = false;
    std::size_t console_committed = 0;
#endif
};
