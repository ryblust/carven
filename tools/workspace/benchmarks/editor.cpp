module;
#include <cstdio>

module carven:workspace.benchmark.editor;

import :diagnostics.report;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :support.timing;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto stage_count = static_cast<std::size_t>(TimingStage::Count);
using Durations = std::array<Clock::duration, stage_count>;

enum class Workload { Functions, Imports, Constants, StaticCalls };

struct Project final {
    std::string_view name;
    std::size_t functions;
    std::vector<WorkspaceProjectModule> modules;
    std::vector<std::string> texts;
    std::size_t edited;
    std::size_t queried;
};

struct Settings final {
    std::size_t samples;
    std::size_t warmups;
    std::optional<std::size_t> size;
    bool timings;
};

auto require(bool condition, std::string_view message) noexcept -> void {
    if (!condition) {
        std::println(stderr, "workspace editor benchmark: {}", message);
        std::exit(1);
    }
}

auto add_module(Project& project, std::string_view path, std::string text) noexcept -> void {
    auto canonical = CanonicalModulePath::from_value(path);
    require(canonical.has_value(), "generated module path is invalid");
    project.modules.push_back(
        {.document = std::format("untitled:{}", path), .module_path = std::move(*canonical)}
    );
    project.texts.push_back(std::move(text));
}

auto replace_first(std::string text, std::string_view before, std::string_view after) noexcept
    -> std::string {
    const auto offset = text.find(before);
    require(offset != std::string::npos, "edit target is missing");
    text.replace(offset, before.size(), after);
    return text;
}

auto make_project(Workload workload, std::size_t size) noexcept -> Project {
    auto project = Project {
        .name = "",
        .functions = size,
        .modules = {},
        .texts = {},
        .edited = 0uz,
        .queried = 0uz
    };
    auto text = std::string();
    if (workload == Workload::Functions) {
        project.name = "functions";
        text = "struct Pair { left: i32, right: i32 }\n";
        for (auto index = 0uz; index < size; ++index) {
            text += std::format(
                "fn value_{}(value: i32) -> i32 {{\n"
                "    let pair = Pair {{ left: value, right: 1 }};\n"
                "    let result = pair.left + pair.right;\n"
                "    return result;\n"
                "}}\n",
                index
            );
        }
        add_module(project, "main", std::move(text));
        return project;
    }
    if (workload == Workload::Imports) {
        project.name = "imports";
        project.functions += 3uz;
        add_module(project, "library", "export fn leaf() => 1i32;\n");
        add_module(
            project,
            "middle",
            "import library using leaf;\nexport fn middle() => leaf();\n"
        );
        add_module(
            project,
            "facade",
            "import middle using middle;\nexport fn facade() => middle();\n"
        );
        project.queried = 3uz;
        text = "import facade using facade;\n";
        for (auto index = 0uz; index < size; ++index) {
            text +=
                std::format("fn value_{}() {{ let result = facade(); return result; }}\n", index);
        }
    } else if (workload == Workload::Constants) {
        project.name = "constants";
        add_module(project, "library", "export const seed: i32 = 1;\n");
        add_module(
            project,
            "middle",
            "import library using seed;\nexport const increment: i32 = seed + 1;\n"
        );
        add_module(
            project,
            "facade",
            "import middle using increment;\nexport const answer: i32 = increment + 1;\n"
        );
        project.queried = 3uz;
        text = "import facade using answer;\nconst { println(answer); }\n";
        for (auto index = 0uz; index < size; ++index) {
            text += std::format(
                "fn value_{}() -> i32 {{ let result = answer; return result; }}\n",
                index
            );
        }
    } else {
        project.name = "static_calls";
        ++project.functions;
        add_module(project, "library", "export const fn evaluate() -> i32 => 1;\n");
        project.queried = 1uz;
        text = "import library using evaluate;\nconst { println(evaluate()); }\n";
        for (auto index = 0uz; index < size; ++index) {
            text += std::format(
                "fn value_{}() -> i32 {{ const result = evaluate(); return result; }}\n",
                index
            );
        }
    }
    add_module(project, "main", std::move(text));
    return project;
}

auto update(
    WorkspaceAnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    require(
        host.update(std::string(document), version, std::string(text)).has_value(),
        "update failed"
    );
}

auto microseconds(Clock::duration duration) noexcept -> double {
    return std::chrono::duration<double, std::micro>(duration).count();
}

auto query(
    const WorkspaceAnalysisHost& host,
    const Project& project,
    std::size_t size,
    std::size_t sample,
    std::string_view operation,
    Clock::duration update_elapsed,
    bool published,
    BuiltinType expected_type,
    std::optional<std::string_view> expected_output,
    std::size_t computations,
    bool timings,
    bool report
) noexcept -> void {
    auto durations = Durations {};
    const auto capture = [&](TimingStage stage, Clock::duration elapsed) noexcept {
        durations[static_cast<std::size_t>(stage)] += elapsed;
    };
    const auto snapshot = host.snapshot();
    const auto before = snapshot.counts();
    const auto start = Clock::now();
    const auto result =
        snapshot.semantic(project.modules, timings ? TimingOutput(capture) : TimingOutput());
    const auto elapsed = Clock::now() - start;
    const auto delta = snapshot.counts().semantic - before.semantic;
    require(delta == computations, "unexpected semantic cache computation count");
    if ((result.result->program() != nullptr) != published) {
        std::print(
            stderr,
            "{}",
            render_diagnostics(result.result->diagnostics(), result.result->sources())
        );
        require(false, "unexpected publication result");
    }
    const auto& document = project.modules[project.queried].document;
    const auto source = result.result->source(document);
    require(source.has_value(), "query source is missing");
    const auto anchor = source->text.find("result");
    require(anchor != std::string_view::npos, "hover anchor is missing");
    const auto hover =
        snapshot.hover(project.modules, document, static_cast<std::uint32_t>(anchor));
    require(
        snapshot.counts().semantic == before.semantic + delta,
        "hover repeated analysis of the same selected content"
    );
    require(hover.result.has_value() == published, "unexpected hover availability");
    if (hover.result) {
        const auto* type = std::get_if<TypeID>(&hover.result->type);
        require(type != nullptr, "published hover has no program type identity");
        const auto* builtin = std::get_if<BuiltinTypeValue>(
            &hover.analysis.result->program()->types().type(*type).value
        );
        require(
            builtin != nullptr && builtin->kind == expected_type,
            "dependent hover type is stale"
        );
        require(hover.result->location.range.start() == anchor, "hover source range is stale");
    }
    if (expected_output) {
        auto output = std::string();
        for (const auto& chunk : result.result->output()) {
            output += chunk.bytes;
        }
        require(output == *expected_output, "static execution output is stale or repeated");
    }
    if (!report) {
        return;
    }
    std::print(
        "{},{},{},{},{},{:.3f},{:.3f},{}",
        project.name,
        size,
        project.functions,
        sample,
        operation,
        microseconds(elapsed),
        microseconds(update_elapsed),
        delta
    );
    for (const auto stage : std::array {
             TimingStage::SourceLoading,
             TimingStage::Lexing,
             TimingStage::Parsing,
             TimingStage::SemanticAnalysis,
             TimingStage::SemanticCatalog,
             TimingStage::SemanticDeclarations,
             TimingStage::SemanticBodies,
             TimingStage::SemanticSolving,
             TimingStage::SemanticValidation,
             TimingStage::SourceObservations,
             TimingStage::SourceIndex
         }) {
        std::print(",{:.3f}", microseconds(durations[static_cast<std::size_t>(stage)]));
    }
    std::println();
}

auto run_round(
    const Project& project,
    Workload workload,
    std::size_t size,
    std::size_t sample,
    const Settings& settings,
    bool report
) noexcept -> void {
    auto host = WorkspaceAnalysisHost();
    for (auto index = 0uz; index < project.modules.size(); ++index) {
        update(host, project.modules[index].document, 1, project.texts[index]);
    }
    update(host, "untitled:unselected", 1, "fn outside() {}\n");
    auto type = BuiltinType::I32;
    auto output = workload == Workload::StaticCalls ? std::optional<std::string_view>("1\n")
        : workload == Workload::Constants           ? std::optional<std::string_view>("3\n")
                                                    : std::nullopt;
    auto update_elapsed = Clock::duration::zero();
    const auto change =
        [&](std::string_view document, std::int64_t version, std::string_view text) noexcept {
            const auto start = Clock::now();
            update(host, document, version, text);
            update_elapsed = Clock::now() - start;
        };
    const auto record =
        [&](std::string_view operation, std::size_t computations, bool published = true) noexcept {
            query(
                host,
                project,
                size,
                sample,
                operation,
                update_elapsed,
                published,
                type,
                output,
                computations,
                settings.timings,
                report
            );
            update_elapsed = Clock::duration::zero();
        };
    record("cold", 1uz);
    record("warm", 0uz);
    const auto& module = project.modules[project.edited];
    const auto& original = project.texts[project.edited];
    change(module.document, 2, original);
    record("version_only", 0uz);
    change("untitled:unselected", 2, "fn unrelated_edit() {}\n");
    record("unselected", 0uz);
    const auto edited = workload == Workload::Functions
        ? replace_first(original, "right: 1", "right: 2")
        : workload == Workload::Imports   ? replace_first(original, "1i32", "2i32")
        : workload == Workload::Constants ? replace_first(original, "= 1", "= 2")
                                          : replace_first(original, "=> 1", "=> 2");
    change(module.document, 3, edited);
    if (workload == Workload::StaticCalls) {
        output = "2\n";
    }
    if (workload == Workload::Constants) {
        output = "4\n";
    }
    record(workload == Workload::Constants ? "constant_edit" : "body_edit", 1uz);
    change(module.document, 4, "// editor comment\n" + edited);
    record("comment_edit", 1uz);
    if (workload == Workload::Imports) {
        change(module.document, 5, replace_first(original, "1i32", "1i64"));
        type = BuiltinType::I64;
        record("inferred_type_edit", 1uz);
    } else if (workload == Workload::Functions) {
        change(module.document, 5, replace_first(edited, "pair.left + pair.right", ";"));
        record("broken_syntax", 1uz, false);
        change(module.document, 6, edited);
        record("repair", 1uz);
    }
}

auto parse_settings(int argc, char** argv) noexcept -> Settings {
    auto result = Settings {.samples = 7uz, .warmups = 1uz, .size = {}, .timings = true};
    for (auto index = 1; index < argc; ++index) {
        const auto option = std::string_view(argv[index]);
        if (option == "--no-timings") {
            result.timings = false;
            continue;
        }
        require(index + 1 < argc, "expected --samples N, --warmups N, --size N, or --no-timings");
        const auto text = std::string_view(argv[++index]);
        auto value = 0uz;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        require(
            parsed.ec == std::errc() && parsed.ptr == text.data() + text.size(),
            "invalid integer option"
        );
        if (option == "--samples") {
            require(value > 0 && value <= 100, "samples must be in 1..100");
            result.samples = value;
        } else if (option == "--warmups") {
            require(value <= 100, "warmups must be in 0..100");
            result.warmups = value;
        } else {
            require(option == "--size" && value > 0 && value <= 10000, "size must be in 1..10000");
            result.size = value;
        }
    }
    return result;
}

} // namespace

extern "C++" auto main(int argc, char** argv) noexcept -> int {
    const auto settings = parse_settings(argc, argv);
    std::println(
        "workload,size,functions,sample,operation,wall_us,update_us,semantic_delta,source_preparation_us,lexing_us,parsing_us,semantic_us,catalog_us,declarations_us,body_batch_us,solving_us,validation_us,observations_us,index_us"
    );
    for (const auto size :
         settings.size ? std::vector {*settings.size} : std::vector {100uz, 500uz, 2000uz}) {
        for (const auto workload : std::array {
                 Workload::Functions,
                 Workload::Imports,
                 Workload::Constants,
                 Workload::StaticCalls
             }) {
            const auto project = make_project(workload, size);
            for (auto round = 0uz; round < settings.warmups + settings.samples; ++round) {
                run_round(
                    project,
                    workload,
                    size,
                    round >= settings.warmups ? round - settings.warmups + 1 : 0uz,
                    settings,
                    round >= settings.warmups
                );
            }
        }
    }
    return 0;
}
