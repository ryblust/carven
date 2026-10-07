module;
#include <cstdio>

module carven:workspace.benchmark.navigation;

import :diagnostics.report;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {
using Clock = std::chrono::steady_clock;

enum class Graph { Independent, Shared, Dense };

struct Project final {
    std::string_view name;
    std::size_t functions;
    std::vector<WorkspaceProjectModule> modules;
    std::vector<std::string> texts;
    std::size_t dependency;
    std::size_t root;
    std::size_t closure;
};

struct Settings final {
    std::size_t samples;
    std::size_t warmups;
    std::optional<std::size_t> size;
};

auto require(bool value, std::string_view message) noexcept -> void {
    if (!value) {
        std::println(stderr, "workspace navigation benchmark: {}", message);
        std::exit(1);
    }
}

auto add(Project& project, std::string_view path, std::string text) noexcept -> void {
    auto canonical = CanonicalModulePath::from_value(path);
    require(canonical.has_value(), "invalid generated module path");
    project.modules.push_back(
        {.document = std::format("untitled:{}", path), .module_path = std::move(*canonical)}
    );
    project.texts.push_back(std::move(text));
}

auto make_project(Graph graph, std::size_t size) noexcept -> Project {
    auto project = Project {
        .name = graph == Graph::Independent ? "independent"
            : graph == Graph::Shared        ? "shared"
                                            : "dense",
        .functions = size * 20uz + (graph == Graph::Dense ? 1uz : 0uz),
        .modules = {},
        .texts = {},
        .dependency = 0uz,
        .root = 0uz,
        .closure = graph == Graph::Independent ? 1uz
            : graph == Graph::Shared           ? 2uz
                                               : size + 1uz
    };
    if (graph == Graph::Shared) {
        add(project, "library", "export const seed: i32 = 1;\n");
        project.root = 1uz;
    }
    for (auto index = 0uz; index < size; ++index) {
        auto text = graph == Graph::Shared ? std::string("import library using seed;\n")
                                           : std::format("export const seed_{}: i32 = 1;\n", index);
        const auto seed =
            graph == Graph::Shared ? std::string("seed") : std::format("seed_{}", index);
        for (auto function = 0uz; function < 20uz; ++function) {
            text += std::format(
                "fn value_{}() {{ let result = {}; return result; }}\n",
                function,
                seed
            );
        }
        add(project, std::format("unit_{}", index), std::move(text));
    }
    if (graph == Graph::Dense) {
        auto text = std::string();
        for (auto index = 0uz; index < size; ++index) {
            text += std::format("import unit_{} using seed_{};\n", index, index);
        }
        text += "fn probe() { let result = seed_0; return result; }\n";
        project.root = project.modules.size();
        add(project, "root", std::move(text));
    }
    return project;
}

auto update(
    WorkspaceAnalysisHost& host,
    const WorkspaceProjectModule& module,
    std::int64_t version,
    std::string text
) noexcept -> void {
    require(
        host.update(module.document, version, std::move(text)).has_value(),
        "document update failed"
    );
}

auto micros(Clock::duration elapsed) noexcept -> double {
    return std::chrono::duration<double, std::micro>(elapsed).count();
}

auto run_round(
    const Project& project,
    std::size_t size,
    std::size_t sample,
    bool focused,
    bool report
) noexcept -> void {
    auto host = WorkspaceAnalysisHost();
    for (auto index = 0uz; index < project.modules.size(); ++index) {
        update(host, project.modules[index], 1, project.texts[index]);
    }
    const auto& root = project.modules[project.root];
    const auto anchor = static_cast<std::uint32_t>(project.texts[project.root].find("result"));
    auto expected = BuiltinType::I32;
    auto update_elapsed = Clock::duration::zero();
    const auto record = [&](std::string_view operation, std::size_t computations) noexcept {
        const auto snapshot = host.snapshot();
        const auto counts = snapshot.counts();
        const auto start = Clock::now();
        const auto query = [&]() noexcept -> WorkspaceHoverQuery {
            if (focused) {
                return snapshot.hover(project.modules, root.document, anchor);
            }
            auto analysis = snapshot.semantic(project.modules);
            const auto information = analysis.result->hover(root.document, anchor);
            return {.analysis = std::move(analysis), .result = information};
        }();
        const auto elapsed = Clock::now() - start;
        const auto delta = snapshot.counts().semantic - counts.semantic;
        require(delta == computations, "unexpected semantic cache computation count");
        if (query.analysis.result->program() == nullptr) {
            std::print(
                stderr,
                "{}",
                render_diagnostics(
                    query.analysis.result->diagnostics(),
                    query.analysis.result->sources()
                )
            );
            require(false, "generated navigation did not publish");
        }
        require(query.result.has_value(), "generated hover is missing");
        const auto* type = std::get_if<TypeID>(&query.result->type);
        require(
            type != nullptr
                && *type == query.analysis.result->program()->types().builtin_type(expected),
            "hover type is stale"
        );
        require(query.result->location.range.start() == anchor, "hover range is stale");
        require(
            query.analysis.documents.size() == (focused ? project.closure : project.modules.size()),
            "unexpected analysis scope"
        );
        if (report) {
            std::println(
                "{},{},{},{},{},{},{:.3f},{:.3f},{},{},{}",
                project.name,
                size,
                project.functions,
                sample,
                focused ? "focused" : "full",
                operation,
                micros(elapsed),
                micros(update_elapsed),
                delta,
                snapshot.counts().syntax - counts.syntax,
                query.analysis.documents.size()
            );
        }
        update_elapsed = Clock::duration::zero();
    };
    const auto change = [&](std::size_t index, std::int64_t version, std::string text) noexcept {
        const auto start = Clock::now();
        update(host, project.modules[index], version, std::move(text));
        update_elapsed = Clock::now() - start;
    };
    record("cold", 1uz);
    record("warm", 0uz);
    const auto unrelated = project.name == "shared" ? 2uz : 1uz;
    change(unrelated, 2, "// unrelated edit\n" + project.texts[unrelated]);
    record("unrelated_edit", focused && project.name != "dense" ? 0uz : 1uz);
    auto dependency = project.texts[project.dependency];
    const auto annotation = dependency.find(": i32");
    require(annotation != std::string::npos, "dependency edit is missing");
    dependency.replace(annotation, 5uz, ": i64");
    expected = BuiltinType::I64;
    change(project.dependency, 3, std::move(dependency));
    record("dependency_edit", 1uz);
    // A complete check must still visit every project module after navigation.
    const auto snapshot = host.snapshot();
    const auto before = snapshot.counts().semantic;
    const auto start = Clock::now();
    const auto complete = snapshot.semantic(project.modules);
    const auto elapsed = Clock::now() - start;
    require(
        complete.result->program() != nullptr
            && complete.documents.size() == project.modules.size(),
        "complete check lost project modules"
    );
    const auto delta = snapshot.counts().semantic - before;
    require(
        delta == (focused && project.closure != project.modules.size() ? 1uz : 0uz),
        "complete check cache was lost"
    );
    if (report) {
        std::println(
            "{},{},{},{},{},check_after_navigation,{:.3f},0.000,{},0,{}",
            project.name,
            size,
            project.functions,
            sample,
            focused ? "focused" : "full",
            micros(elapsed),
            delta,
            complete.documents.size()
        );
    }
}

auto settings(int argc, char** argv) noexcept -> Settings {
    auto result = Settings {.samples = 7uz, .warmups = 1uz, .size = {}};
    for (auto index = 1; index < argc; ++index) {
        const auto option = std::string_view(argv[index]);
        require(index + 1 < argc, "expected --samples N, --warmups N, or --size N");
        const auto value = std::string_view(argv[++index]);
        auto number = 0uz;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        require(
            parsed.ec == std::errc() && parsed.ptr == value.data() + value.size(),
            "invalid option value"
        );
        if (option == "--samples") {
            require(number > 0uz && number <= 100uz, "samples must be in 1..100");
            result.samples = number;
        } else if (option == "--warmups") {
            require(number <= 100uz, "warmups must be in 0..100");
            result.warmups = number;
        } else {
            require(
                option == "--size" && number >= 2uz && number <= 1000uz,
                "size must be in 2..1000"
            );
            result.size = number;
        }
    }
    return result;
}
} // namespace

extern "C++" auto main(int argc, char** argv) noexcept -> int {
    const auto options = settings(argc, argv);
    std::println(
        "workload,size,functions,sample,mode,operation,wall_us,update_us,semantic_delta,syntax_delta,analyzed_modules"
    );
    for (const auto size :
         options.size ? std::vector {*options.size} : std::vector {10uz, 100uz, 500uz}) {
        for (const auto graph : std::array {Graph::Independent, Graph::Shared, Graph::Dense}) {
            const auto project = make_project(graph, size);
            for (auto round = 0uz; round < options.samples + options.warmups; ++round) {
                const auto report = round >= options.warmups;
                const auto sample = report ? round - options.warmups + 1uz : 0uz;
                // Alternate order within each pair to reduce order bias.
                run_round(project, size, sample, round % 2uz == 0uz, report);
                run_round(project, size, sample, round % 2uz != 0uz, report);
            }
        }
    }
    return 0;
}
