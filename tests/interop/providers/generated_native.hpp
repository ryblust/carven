#pragma once

#include <carven/providers/tests/interop/providers/generated.hpp>

namespace generated_types =
    carven::providers::tests::interop::providers::generated::cv_escaped_functions;

auto generated_record(
    std::int32_t mode,
    generated_types::generated_record::Parameter1 local,
    generated_types::generated_record::Parameter2 io
) -> generated_types::generated_record::Completion;

auto generated_void() -> void;
auto generated_other_translation_unit() -> bool;
auto generated_converted() -> double;
auto generated_write(generated_types::generated_write::Parameter0& value) -> void;
auto generated_slice(generated_types::generated_slice::Parameter0 values) -> std::int32_t;
auto generated_links(generated_types::generated_links::Parameter0 value) -> std::int32_t;
