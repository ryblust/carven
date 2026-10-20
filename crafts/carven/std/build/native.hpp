#pragma once

#include <carven/runtime/collection/slice.hpp>
#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/text/string.hpp>

namespace carven::standard::build {

// An unreadable file leaves the result empty.
auto fingerprint(
    runtime::Slice<runtime::String> paths,
    runtime::Slice<runtime::String> values
) noexcept -> runtime::String;

// Results follow the input groups. An unreadable file leaves its group's result
// empty; each distinct path is read at most once during the call.
auto fingerprints(runtime::Slice<runtime::Vector<runtime::String>> paths) noexcept
    -> runtime::Vector<runtime::String>;

} // namespace carven::standard::build
