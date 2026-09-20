module carven:driver.diagnostic;

import :diagnostics.diagnostic;
import :source.manager;
import std;

auto render_driver_error(
    std::string_view message,
    bool use_color,
    std::string_view usage_command = {},
    std::string_view help = {}
) noexcept -> std::string;

auto emit_driver_error(
    std::string_view message,
    std::string_view usage_command = {},
    std::string_view help = {}
) noexcept -> int;

auto emit_missing_entry_error() noexcept -> int;

// Applies the CLI color policy and enables terminal processing when needed.
auto initialize_diagnostic_color() noexcept -> bool;

auto emit_source_diagnostics(
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources
) noexcept -> void;
