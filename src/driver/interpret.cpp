module carven:driver.interpret.impl;

import :diagnostics.builder;
import :diagnostics.report;
import :driver.analysis;
import :driver.interpret;
import :driver.sources;
import :driver.timings;
import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.semir.decl;
import :source.manager;
import :source.provenance;
import :source.text;
import :support.invariant;
import :support.timing;
import std;

namespace {

auto fail(std::string_view message) noexcept -> int {
    std::println(std::cerr, "carven: error: {}", message);
    std::println(std::cerr, "Run 'carven interpret --help' for usage.");
    return 1;
}

struct RestoredSources final {
    SourceManager sources;
    std::vector<SourceID> ids;
};

auto restore_sources(const SemIRProgram& program) noexcept -> RestoredSources {
    auto restored = RestoredSources {.sources = {}, .ids = {}};
    for (const auto& source : program.provenance().source_snapshots()) {
        const auto id = restored.sources.append_virtual(
            std::string(source.display_origin()),
            std::string(source.text())
        );
        if (!id) {
            invariant_violation("published source snapshot could not be restored for diagnostics");
        }
        restored.ids.push_back(*id);
    }
    return restored;
}

auto report_execution_error(
    const SemIRProgram& program,
    const ExecutionDiagnostic& error,
    const RestoredSources& restored
) noexcept -> void {
    const auto provenance = program.provenance();
    const auto span = [&](ProgramOriginID origin) noexcept {
        const auto source = provenance.source_origin(origin);
        return locate(restored.ids[source.source_id.index()], source.span);
    };
    auto diagnostic = DiagnosticBuilder(error.code, error.message);
    diagnostic.primary(span(error.origin));
    for (const auto origin : error.calls | std::views::reverse | std::views::take(8)) {
        if (origin != error.origin) {
            diagnostic.related(span(origin), "while interpreting this function call");
        }
    }
    std::print(std::cerr, "{}", render_diagnostic(diagnostic.build(), restored.sources));
}

} // namespace

auto run_interpret_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int {
    if (args.size() == 1
        && (std::string_view(args[0]) == "--help" || std::string_view(args[0]) == "-h")) {
        std::print(
            "Run the supported Carven language subset with the interpreter.\n"
            "\n"
            "Usage:\n"
            "  carven interpret [options] <source-file>... [-- <arguments>...]\n"
            "\n"
            "Options:\n"
            "      --tests          Run runtime tests instead of the program entry\n"
            "      --timings        Show total and stage timings on stderr\n"
            "      --trace          Show executed statements, calls, and returns on stderr\n"
            "      --max-steps <n>  Set the execution step budget (default: 100000)\n"
            "  -h, --help           Show this help\n"
            "\n"
            "Sources include the fixed toolchain and working-directory Crafts roots.\n"
            "Program execution requires top-level statements or main.\n"
            "The entry must take no parameters; arguments after '--' are ignored.\n"
            "Native C++ integration requires compiled execution.\n"
        );
        return 0;
    }
    auto paths = std::vector<std::string_view>();
    auto trace = false;
    auto tests = false;
    auto show_timings = false;
    auto limits = constant_execution_limits();
    auto seen_steps = false;
    for (auto index = 0uz; index < args.size(); ++index) {
        const auto arg = std::string_view(args[index]);
        if (arg == "--") {
            // A no-argument main ignores process arguments, as in native execution.
            break;
        }
        if (arg == "--tests") {
            if (tests) {
                return fail("--tests may be specified only once");
            }
            tests = true;
        } else if (arg == "--timings") {
            show_timings = true;
        } else if (arg == "--trace") {
            if (trace) {
                return fail("--trace may be specified only once");
            }
            trace = true;
        } else if (arg == "--max-steps") {
            if (seen_steps || ++index == args.size()) {
                return fail(
                    "--max-steps requires one nonnegative integer and may appear only once"
                );
            }
            seen_steps = true;
            const auto text = std::string_view(args[index]);
            const auto parsed =
                std::from_chars(text.data(), text.data() + text.size(), limits.steps);
            if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) {
                return fail("--max-steps requires a nonnegative integer");
            }
        } else if (arg.starts_with('-')) {
            return fail(std::format("unknown interpret option '{}'", arg));
        } else {
            paths.push_back(arg);
        }
    }
    if (paths.empty()) {
        return fail("interpret requires at least one source file");
    }
    auto timings = CommandTimings(show_timings, "interpretation");
    const auto output =
        ExecutionOutput([](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
            std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
        });
    const auto sources = collect_command_sources(executable, paths, timings.recorder());
    if (!sources) {
        return fail(sources.error());
    }
    const auto program = load_and_analyze_sources(sources->carven, output, timings.recorder());
    if (!program) {
        return 1;
    }
    auto diagnostic_sources = std::optional<RestoredSources>();
    const auto report_error = [&](const ExecutionDiagnostic& error) noexcept {
        if (!diagnostic_sources) {
            diagnostic_sources.emplace(restore_sources(*program));
        }
        report_execution_error(*program, error, *diagnostic_sources);
    };
    auto entry = std::optional<FunctionID>();
    for (const auto function : program->declarations().functions()) {
        if (function.value.entry_point) {
            entry = function.id;
        }
    }
    if (!tests && !entry) {
        return fail("running a program requires an entry point");
    }
    if (!sources->native.empty()) {
        return fail("native C++ source files are not supported by the interpreter");
    }
    auto options = InterpreterOptions {.limits = limits, .trace = {}};
    if (trace) {
        options.trace = [&](const ExecutionTraceEvent& event) noexcept {
            const auto provenance = program->provenance();
            const auto origin = provenance.source_origin(event.origin);
            const auto& source = provenance.source_snapshot(origin.source_id);
            const auto kind = event.kind == ExecutionTraceKind::Call ? "call"
                : event.kind == ExecutionTraceKind::Return           ? "return"
                                                                     : "statement";
            const auto name = event.function
                ? provenance.spelling(program->declarations().function(*event.function).name)
                : std::string_view();
            std::println(
                std::cerr,
                "{}{}:{}: {}{}{}",
                std::string(event.depth * 2, ' '),
                source.display_origin(),
                source.location(origin.span),
                kind,
                name.empty() ? "" : " ",
                name
            );
        };
    }
    auto execution = TimingScope(timings.recorder(), TimingStage::Execution);
    if (tests) {
        const auto results = interpret_tests(*program, output, options);
        execution.stop();
        if (!results) {
            report_error(results.error());
            return 1;
        }
        if (results->empty()) {
            return fail("running tests requires at least one runtime test");
        }
        auto failed = 0uz;
        for (const auto& result : *results) {
            if (!result.diagnostics.empty()) {
                ++failed;
                std::println(
                    std::cerr,
                    "carven: test '{}' failed",
                    program->provenance().spelling(program->tests().test(result.test).name)
                );
                for (const auto& diagnostic : result.diagnostics) {
                    report_error(diagnostic);
                }
            }
        }
        std::println(
            std::cerr,
            "carven: {} tests passed; {} failed",
            results->size() - failed,
            failed
        );
        timings.set_outcome(failed == 0 ? "finished" : "failed");
        return failed == 0 ? 0 : 1;
    }
    const auto result = interpret(*program, *entry, output, options);
    execution.stop();
    if (!result) {
        report_error(result.error());
        return 1;
    }
    timings.set_outcome("finished");
    return 0;
}
