#pragma once

#include <carven/std/channel/native.hpp>
#include <carven/std/collections/native.hpp>
#include <carven/std/fs/native.hpp>
#include <carven/std/io/native.hpp>
#include <carven/std/process/native.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace carven::standard {

// Selects Standard Craft capabilities; each consumer derives its own typed bindings.
// Conversion arguments are ordered From, To. Empty names denote source-value conversions.
template<typename Operation, typename Conversion>
auto visit_native_bindings(Operation operation, Conversion conversion) noexcept -> void {
#define CARVEN_STANDARD_OPERATION(symbol, effects)                                                 \
    operation.template operator()<&carven::standard::symbol>("carven::standard::" #symbol, effects)
    CARVEN_STANDARD_OPERATION(collections::hash_bytes, false);
    CARVEN_STANDARD_OPERATION(collections::digest, false);
    CARVEN_STANDARD_OPERATION(collections::update_digest, false);
    CARVEN_STANDARD_OPERATION(collections::DigestState::first, false);
    CARVEN_STANDARD_OPERATION(collections::DigestState::second, false);

    CARVEN_STANDARD_OPERATION(channel::create, false);
    CARVEN_STANDARD_OPERATION(channel::close, false);
    CARVEN_STANDARD_OPERATION(channel::try_send, false);
    CARVEN_STANDARD_OPERATION(channel::try_receive, false);
    CARVEN_STANDARD_OPERATION(channel::send, false);
    CARVEN_STANDARD_OPERATION(channel::receive, false);
    CARVEN_STANDARD_OPERATION(channel::Result::available, false);
    CARVEN_STANDARD_OPERATION(channel::Result::end, false);
    CARVEN_STANDARD_OPERATION(channel::Result::take_bytes, false);

    CARVEN_STANDARD_OPERATION(fs::open, true);
    CARVEN_STANDARD_OPERATION(fs::read, true);
    CARVEN_STANDARD_OPERATION(fs::close, true);
    CARVEN_STANDARD_OPERATION(fs::OpenResult::code, false);
    CARVEN_STANDARD_OPERATION(fs::OpenResult::take_reader, false);
    CARVEN_STANDARD_OPERATION(fs::write_bytes, true);
    CARVEN_STANDARD_OPERATION(fs::status, true);
    CARVEN_STANDARD_OPERATION(fs::create_directories, true);
    CARVEN_STANDARD_OPERATION(fs::canonical, true);
    CARVEN_STANDARD_OPERATION(fs::resolve, true);
    CARVEN_STANDARD_OPERATION(fs::parent, false);
    CARVEN_STANDARD_OPERATION(fs::join, false);
    CARVEN_STANDARD_OPERATION(fs::remove, true);
    CARVEN_STANDARD_OPERATION(fs::files, true);
    CARVEN_STANDARD_OPERATION(fs::ReadResult::code, false);
    CARVEN_STANDARD_OPERATION(fs::StatusResult::code, false);
    CARVEN_STANDARD_OPERATION(fs::StatusResult::kind, false);
    CARVEN_STANDARD_OPERATION(fs::RemovalResult::code, false);
    CARVEN_STANDARD_OPERATION(fs::RemovalResult::removed, false);
    CARVEN_STANDARD_OPERATION(fs::PathResult::code, false);
    CARVEN_STANDARD_OPERATION(fs::PathsResult::code, false);
    CARVEN_STANDARD_OPERATION(fs::ReadResult::take_bytes, false);
    CARVEN_STANDARD_OPERATION(fs::PathResult::take_path, false);
    CARVEN_STANDARD_OPERATION(fs::PathsResult::take_paths, false);

    CARVEN_STANDARD_OPERATION(io::write_stdout, true);
    CARVEN_STANDARD_OPERATION(io::write_output, true);

    CARVEN_STANDARD_OPERATION(process::spawn, true);
    CARVEN_STANDARD_OPERATION(process::spawn_inherited, true);
    CARVEN_STANDARD_OPERATION(process::parallelism, true);
    CARVEN_STANDARD_OPERATION(process::environment, true);
    CARVEN_STANDARD_OPERATION(process::arguments, true);
    CARVEN_STANDARD_OPERATION(process::executable, true);
    CARVEN_STANDARD_OPERATION(process::find_executable, true);
    CARVEN_STANDARD_OPERATION(process::operating_system, false);
    CARVEN_STANDARD_OPERATION(process::run, true);
    CARVEN_STANDARD_OPERATION(process::group_new, false);
    CARVEN_STANDARD_OPERATION(process::group_spawn, true);
    CARVEN_STANDARD_OPERATION(process::group_wait, true);
    CARVEN_STANDARD_OPERATION(process::read, true);
    CARVEN_STANDARD_OPERATION(process::write, true);
    CARVEN_STANDARD_OPERATION(process::wait, true);
    CARVEN_STANDARD_OPERATION(process::close_input, true);
    CARVEN_STANDARD_OPERATION(process::terminate, true);
    CARVEN_STANDARD_OPERATION(process::SpawnResult::code, false);
    CARVEN_STANDARD_OPERATION(process::Result::code, false);
    CARVEN_STANDARD_OPERATION(process::Result::status, false);
    CARVEN_STANDARD_OPERATION(process::Result::end, false);
    CARVEN_STANDARD_OPERATION(process::RunResult::status, false);
    CARVEN_STANDARD_OPERATION(process::RunResult::code, false);
    CARVEN_STANDARD_OPERATION(process::TextResult::code, false);
    CARVEN_STANDARD_OPERATION(process::ArgumentsResult::code, false);
    CARVEN_STANDARD_OPERATION(process::Exit::index, false);
    CARVEN_STANDARD_OPERATION(process::Exit::status, false);
    CARVEN_STANDARD_OPERATION(process::Exit::code, false);
    CARVEN_STANDARD_OPERATION(process::SpawnResult::take_process, false);
    CARVEN_STANDARD_OPERATION(process::Result::take_bytes, false);
    CARVEN_STANDARD_OPERATION(process::TextResult::take_text, false);
    CARVEN_STANDARD_OPERATION(process::ArgumentsResult::take_arguments, false);
#undef CARVEN_STANDARD_OPERATION

#define CARVEN_STANDARD_CONVERSION(type)                                                           \
    conversion.template operator()<carven::standard::type, carven::standard::type>(                \
        "carven::standard::" #type                                                                 \
    )
    CARVEN_STANDARD_CONVERSION(channel::Handle);
    CARVEN_STANDARD_CONVERSION(fs::Reader);
    CARVEN_STANDARD_CONVERSION(process::Handle);
    CARVEN_STANDARD_CONVERSION(process::Group);
#undef CARVEN_STANDARD_CONVERSION

    conversion.template operator()<bool, bool>(std::string_view());
    conversion.template operator()<std::uint8_t, std::uint8_t>(std::string_view());
    conversion.template operator()<std::int32_t, std::int32_t>(std::string_view());
    conversion.template operator()<std::uint64_t, std::uint64_t>(std::string_view());
    conversion.template operator()<runtime::String, runtime::String>(std::string_view());
    conversion.template operator()<runtime::Vector<std::uint8_t>, runtime::Vector<std::uint8_t>>(
        std::string_view()
    );
    conversion
        .template operator()<runtime::Vector<runtime::String>, runtime::Vector<runtime::String>>(
            std::string_view()
        );
    conversion.template operator()<std::int32_t, std::size_t>(std::string_view());
    if constexpr (!std::same_as<std::uint64_t, std::size_t>) {
        conversion.template operator()<std::size_t, std::size_t>(std::string_view());
    }
}

} // namespace carven::standard
