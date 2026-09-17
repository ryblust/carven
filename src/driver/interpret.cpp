module carven:driver.interpret.impl;

import :diagnostics.builder;
import :diagnostics.report;
import :driver.analysis;
import :driver.interpret;
import :interpreter.execute;
import :semantic.evaluation.execution;
import :semantic.semir.decl;
import :source.manager;
import :source.provenance;
import :source.text;
import :support.invariant;
import std;

namespace {

auto fail(std::string_view message) noexcept -> int {
    std::println(std::cerr, "carven: error: {}", message);
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
        std::println(
            "Usage: carven interpret [--trace] [--max-steps N] <source-file>... [-- <arguments>...]\n"
            "Interpret the supported Carven subset after semantic analysis.\n"
            "--trace         Show executed statement locations and function calls/returns.\n"
            "--max-steps N   Set the execution step budget (default: 100000)."
        );
        return 0;
    }
    auto paths = std::vector<std::string_view>();
    auto trace = false;
    auto limits = ExecutionLimits {};
    auto seen_steps = false;
    for (auto index = 0uz; index < args.size(); ++index) {
        const auto arg = std::string_view(args[index]);
        if (arg == "--") {
            // A no-argument main ignores process arguments, as in native execution.
            break;
        }
        if (arg == "--trace") {
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
    const auto output =
        ExecutionOutput([](ExecutionOutputStream stream, std::string_view bytes) static noexcept {
            std::print(stream == ExecutionOutputStream::Error ? std::cerr : std::cout, "{}", bytes);
        });
    const auto program = load_and_analyze_sources(paths, output);
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
    const auto result = interpret(*program, *entry, output, options);
    if (!result) {
        report_execution_error(*program, result.error());
        return 1;
    }
    return 0;
}
