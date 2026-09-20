module carven:support.terminal;

import std;

// Enables ANSI processing when standard error is a supported terminal.
auto enable_stderr_ansi() noexcept -> bool;

class TerminalStyler final {
public:
    explicit TerminalStyler(bool use_color) noexcept;

    auto bold(std::string_view text) const noexcept -> std::string;
    auto bold_red(std::string_view text) const noexcept -> std::string;
    auto bold_green(std::string_view text) const noexcept -> std::string;
    auto bold_yellow(std::string_view text) const noexcept -> std::string;
    auto bold_cyan(std::string_view text) const noexcept -> std::string;

private:
    bool enabled;
};
