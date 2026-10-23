module carven:driver.cli.impl;

import :driver.cli;
import :driver.compile;
import :driver.diagnostic;
import :driver.dump;
import :driver.interpret;
import :driver.options;
import :support.path;
import :support.process;
import std;

namespace {

auto run_formatter(std::span<const std::string> args) noexcept -> int {
    const auto program = process::executable();
    if (!program) {
        return emit_driver_error("cannot locate the running compiler for formatting");
    }
#if defined(_WIN32)
    const auto formatter = program->parent_path() / "carven-format.exe";
#else
    const auto formatter = program->parent_path() / "carven-format";
#endif
    const auto result = process::run(formatter, args, {}, process::Stdio::Inherit);
    if (!result) {
        return emit_driver_error(
            std::format(
                "cannot run formatter '{}': {}; install or build carven-format beside carven",
                path_to_generic_utf8(formatter),
                result.error().message()
            )
        );
    }
    return result->status;
}

auto run_project_command(
    std::string_view command,
    std::span<const std::string> args,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> int {
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        if (command == "build") {
            std::println(
                "Usage: carven build\n\n"
                "Interpret the current directory's build.cv as an ordinary Carven program.\n"
                "The script defines the build through std::build and other library calls."
            );
        } else if (command == "test") {
            std::println(
                "Usage: carven test [<target>]\n\n"
                "Execute the current directory's build.cv and run its native test programs.\n"
                "Without a target, test the project's declared build targets.\n"
                "Each test program uses its target's source dependencies and native settings."
            );
        } else {
            std::println(
                "Usage:\n"
                "  carven run <source.cv>... [-- <arguments>...]\n"
                "  carven run [<target>] [-- <arguments>...]\n\n"
                "With sources, build and run one native executable named after the first file.\n"
                "Otherwise, execute the current directory's build.cv to build and run a project target.\n"
                "The standard builder writes outputs under build/ by default."
            );
        }
        return 0;
    }
    if (command == "build" && !args.empty()) {
        return emit_driver_error(
            "build takes no arguments; use 'carven run <source.cv>...' to run source files",
            "carven"
        );
    }
    if (command == "run" && !args.empty() && std::string_view(args[0]).ends_with(".cv")) {
        return run_interpret_function(
            "crafts.carven.std.build.cli",
            "run_sources",
            native_bindings
        );
    }
    return run_interpret_command(std::array<std::string, 1> {"build.cv"}, native_bindings);
}

auto print_help() noexcept -> int {
    std::print(
        "Carven - a programming language that compiles to C++.\n"
        "Build native programs, check and interpret sources, and inspect project structure.\n"
        "\n"
        "Usage:\n"
        "  carven build\n"
        "  carven run <source.cv>... [-- <arguments>...]\n"
        "  carven run [<target>] [-- <arguments>...]\n"
        "  carven test [<target>]\n"
        "  carven format [arguments...]\n"
        "  carven <command> [options] <input>...\n"
        "\n"
        "Commands:\n"
        "  build                           Interpret the current directory's build.cv\n"
        "  run                             Build and run an executable\n"
        "  test                            Build and run project test blocks\n"
        "  format                          Format sources or check their formatting\n"
        "  query                           Print the build.cv project model as JSON\n"
        "  compile                         Generate C++ artifacts\n"
        "  check                           Check sources and run const tests\n"
        "  interpret                       Interpret supported programs and tests\n"
        "  dump                            Print tokens or the syntax tree\n"
        "\n"
        "Options:\n"
        "  -h, --help                      Show this help\n"
        "  -V, --version                   Show the version\n"
        "\n"
        "Pass .cv sources to run, or use the current directory's build.cv for project commands.\n"
        "Use 'carven <command> --help' for command options.\n"
    );
    return 0;
}

auto print_version() noexcept -> int {
    std::println("carven v0.1.0");
    return 0;
}

} // namespace

auto carven_main(
    int argc,
    const char* const* native_argv,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> int {
    const auto arguments = process::arguments(argc, native_argv);
    if (!arguments) {
        return emit_driver_error(arguments.error());
    }

    const auto argv = std::span(*arguments);
    const auto args = argv.subspan(1);
    if (args.empty()) {
        return print_help();
    }

    const auto first_arg = std::string_view(args[0]);
    if (first_arg == "--help" || first_arg == "-h") {
        return print_help();
    } else if (first_arg == "--version" || first_arg == "-V") {
        return print_version();
    } else if (first_arg == "dump") {
        return run_dump_command(args.subspan(1));
    } else if (first_arg == "format") {
        return run_formatter(args.subspan(1));
    }

    if (first_arg == "query") {
        if (args.size() == 2 && (args[1] == "--help" || args[1] == "-h")) {
            std::println(
                "Usage: carven query [source-or-artifacts] [--timings]\n\n"
                "Execute build.cv or a supplied program and print its project model and source inputs.\n"
                "A retained semantic artifact skips analysis; its freshness belongs to the caller."
            );
            return 0;
        }
        return run_query_command(args.subspan(1), native_bindings);
    }

    if (first_arg == "build" || first_arg == "run" || first_arg == "test") {
        return run_project_command(first_arg, args.subspan(1), native_bindings);
    }

    if (first_arg == "compile") {
        return run_compilation_command(
            args.subspan(1),
            CompilationCommand::Compile,
            native_bindings
        );
    }

    if (first_arg == "check") {
        return run_compilation_command(args.subspan(1), CompilationCommand::Check, native_bindings);
    }

    if (first_arg == "interpret") {
        return run_interpret_command(args.subspan(1), native_bindings);
    }

    return emit_driver_error("expected a command; use 'carven --help' for commands", "carven");
}
