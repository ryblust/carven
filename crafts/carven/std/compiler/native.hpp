#pragma once

#include <carven/runtime/collection/slice.hpp>
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/text/string.hpp>

#include <string_view>

namespace carven::standard::compiler {

struct Implementation final {
    runtime::String file;
    runtime::String source;
    runtime::Vector<runtime::String> dependencies;
};

struct CompilationResult final {
    bool success;
    bool cacheable;
    runtime::String error;
    runtime::Vector<Implementation> implementations;
    runtime::Vector<runtime::String> include_dirs;
    runtime::Vector<runtime::String> native_providers;

    auto take_error() noexcept -> runtime::String;
    auto take_implementations() noexcept -> runtime::Vector<Implementation>;
    auto take_include_dirs() noexcept -> runtime::Vector<runtime::String>;
    auto take_native_providers() noexcept -> runtime::Vector<runtime::String>;
};

// Compile sources and write C++ artifacts using the compiler host's pipeline.
// Diagnostics and static output use the host's standard streams. Implementation
// paths are relative to the current directory when destination is relative;
// source identities are canonical filesystem paths.
auto compile(runtime::Slice<runtime::String> sources, std::string_view destination) noexcept
    -> CompilationResult;

} // namespace carven::standard::compiler
