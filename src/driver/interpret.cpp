module carven:driver.interpret.impl;

import :diagnostics.builder;
import :diagnostics.report;
import :driver.analysis;
import :driver.interpret;
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

auto report_execution_error(const SemIRProgram& program, const ExecutionDiagnostic& error) noexcept
    -> void {
    const auto provenance = program.provenance();
    auto sources = SourceManager();
    auto source_ids = std::vector<SourceID>();
    for (const auto& source : provenance.source_snapshots()) {
        const auto id = sources.append_virtual(
            std::string(source.display_origin()),
            std::string(source.text())
        );
        if (!id) {
            invariant_violation("published source snapshot could not be restored for diagnostics");
        }
        source_ids.push_back(*id);
    }
    const auto span = [&](ProgramOriginID origin) noexcept {
        const auto source = provenance.source_origin(origin);
        return locate(source_ids[source.source_id.index()], source.span);
    };
    auto diagnostic = DiagnosticBuilder(error.code, error.message);
    diagnostic.primary(span(error.origin));
    for (const auto origin : error.calls | std::views::reverse | std::views::take(8)) {
        if (origin != error.origin) {
            diagnostic.related(span(origin), "while interpreting this function call");
        }
    }
    std::print(std::cerr, "{}", render_diagnostic(diagnostic.build(), sources));
}

} // namespace

auto run_interpret_command(std::span<const char* const> args) noexcept -> int {
    if (args.size() == 1
        && (std::string_view(args[0]) == "--help" || std::string_view(args[0]) == "-h")) {
        std::print(
            "Run the supported Carven language subset with the interpreter.\n"
            "\n"
            "Usage:\n"
            "  carven interpret [options] <source-file>... [-- <arguments>...]\n"
            "\n"
            "Options:\n"
            "      --timings        Show total and stage timings on stderr\n"
            "      --trace          Show executed statements, calls, and returns on stderr\n"
            "      --max-steps <n>  Set the execution step budget (default: 100000)\n"
            "  -h, --help           Show this help\n"
            "\n"
            "Imports resolve among the supplied sources. An entry point is optional.\n"
            "The entry must take no parameters; arguments after '--' are ignored.\n"
            "Native C++ integration requires compiled execution.\n"
        );
        return 0;
    }
    auto paths = std::vector<std::string_view>();
    auto trace = false;
    auto show_timings = false;
    auto limits = constant_execution_limits();
    auto seen_steps = false;
    for (auto index = 0uz; index < args.size(); ++index) {
        const auto arg = std::string_view(args[index]);
        if (arg == "--") {
            // A no-argument main ignores process arguments, as in native execution.
            break;
        }
        if (arg == "--timings") {
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
    const auto program = load_and_analyze_sources(paths, output, timings.recorder());
    if (!program) {
        return 1;
    }
    auto entry = std::optional<FunctionID>();
    for (const auto function : program->declarations().functions()) {
        if (function.value.entry_point) {
            entry = function.id;
        }
    }
    if (!entry) {
        timings.set_outcome("finished (no entry point)");
        return 0;
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
    auto execution = TimingScope(timings.recorder(), TimingStage::ProgramExecution);
    const auto result = interpret(*program, *entry, output, options);
    execution.stop();
    if (!result) {
        report_execution_error(*program, result.error());
        return 1;
    }
    timings.set_outcome("finished");
    return 0;
}
