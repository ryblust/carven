#pragma once

#include <carven/runtime/collection/slice.hpp>
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/text/string.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace carven::standard::process {

class State;

struct Handle final {
    std::shared_ptr<State> state;
};

struct SpawnResult final {
    Handle process;
    std::int32_t code;
    auto take_process() noexcept -> Handle;
};

struct Result final {
    runtime::Vector<std::uint8_t> bytes;
    std::int32_t code;
    std::int32_t status;
    bool end;
    auto take_bytes() noexcept -> runtime::Vector<std::uint8_t>;
};

struct TextResult final {
    std::int32_t code;
    runtime::String text;
    auto take_text() noexcept -> runtime::String;
};

struct ArgumentsResult final {
    std::int32_t code;
    runtime::Vector<runtime::String> arguments;
    auto take_arguments() noexcept -> runtime::Vector<runtime::String>;
};

// Queries the current host process. Arguments exclude the executable name.
auto arguments() noexcept -> ArgumentsResult;

auto executable() noexcept -> TextResult;

// Resolves PATH without canonicalizing the selected executable's name.
auto find_executable(std::string_view name) noexcept -> TextResult;

// 0: Windows, 1: macOS, 2: Linux, 3: another operating system.
auto operating_system() noexcept -> std::int32_t;

auto environment(std::string_view name) noexcept -> TextResult;

auto spawn(
    std::string_view program,
    runtime::Slice<runtime::String> arguments,
    std::string_view directory
) noexcept -> SpawnResult;

struct Exit final {
    std::int32_t index;
    std::int32_t status;
    std::int32_t code;
};

struct RunResult final {
    std::int32_t status;
    std::int32_t code;
};

// An empty output path inherits stdout.
auto spawn_inherited(
    std::string_view program,
    runtime::Slice<runtime::String> arguments,
    std::string_view output = {}
) noexcept -> SpawnResult;

auto wait_any(std::span<const Handle> processes) noexcept -> Exit;

auto run(
    std::string_view program,
    runtime::Slice<runtime::String> arguments,
    std::string_view output
) noexcept -> RunResult;

auto parallelism() noexcept -> std::int32_t;

struct Group final {
    std::shared_ptr<std::vector<Handle>> children;
};

auto group_new() noexcept -> Group;

auto group_spawn(
    const Group& group,
    std::string_view program,
    runtime::Slice<runtime::String> arguments
) noexcept -> std::int32_t;

auto group_wait(const Group& group) noexcept -> Exit;

auto read(Handle process, std::size_t limit) noexcept -> runtime::execution::AsyncOperation<Result>;

auto write(Handle process, runtime::Vector<std::uint8_t> bytes) noexcept
    -> runtime::execution::AsyncOperation<Result>;

auto wait(Handle process) noexcept -> runtime::execution::AsyncOperation<Result>;
auto close_input(const Handle& process) noexcept -> std::int32_t;
auto terminate(const Handle& process) noexcept -> std::int32_t;

} // namespace carven::standard::process
