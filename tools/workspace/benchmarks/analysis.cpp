module;
#include <cstdio>

module carven:workspace.benchmark.analysis;

import :source.module_path;
import :workspace.analysis;
import :workspace.document;
import :workspace.semantic;
import std;

namespace {

using Clock = std::chrono::steady_clock;

struct Project final {
    std::vector<WorkspaceProjectModule> modules;
    std::vector<std::string> texts;
};

struct Sample final {
    double microseconds;
    WorkspaceQueryCounts counts;
};

using Timings = std::map<std::string, std::vector<Sample>>;

auto require(bool condition, std::string_view message) noexcept -> void {
    if (!condition) {
        std::println(stderr, "workspace benchmark: {}", message);
        std::exit(1);
    }
}

auto make_project(std::size_t size) noexcept -> Project {
    auto project = Project();
    project.modules.reserve(size);
    project.texts.reserve(size);
    for (auto index = 0uz; index < size; ++index) {
        auto path = CanonicalModulePath::from_value(std::format("module_{}", index));
        require(path.has_value(), "generated module path is invalid");
        project.modules.push_back(
            {.document = std::format("untitled:module_{}", index), .module_path = std::move(*path)}
        );
        project.texts.push_back(std::format("fn value_{}() -> i32 {{ return 1; }}", index));
    }
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
        "document update failed"
    );
}

auto populate(WorkspaceAnalysisHost& host, const Project& project) noexcept -> void {
    for (auto index = 0uz; index < project.modules.size(); ++index) {
        update(host, project.modules[index].document, 1, project.texts[index]);
    }
    update(host, "untitled:unselected", 1, "fn outside() {}");
}

auto difference(WorkspaceQueryCounts after, WorkspaceQueryCounts before) noexcept
    -> WorkspaceQueryCounts {
    return {
        .syntax = after.syntax - before.syntax,
        .document_symbols = after.document_symbols - before.document_symbols,
        .workspace_symbols = after.workspace_symbols - before.workspace_symbols,
        .semantic = after.semantic - before.semantic,
    };
}

template<typename Action>
auto measure(WorkspaceAnalysisHost& host, Action action, std::size_t repetitions = 1) noexcept
    -> Sample {
    const auto before = host.snapshot().counts();
    const auto start = Clock::now();
    for (auto iteration = 0uz; iteration < repetitions; ++iteration) {
        action();
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    return {
        .microseconds = elapsed / static_cast<double>(repetitions),
        .counts = difference(host.snapshot().counts(), before),
    };
}

auto query(WorkspaceAnalysisHost& host, const Project& project) noexcept -> void {
    const auto snapshot = host.snapshot();
    const auto result = snapshot.semantic(project.modules);
    require(result.result->program() != nullptr, "generated project failed semantic analysis");
    const auto hover = snapshot.hover(
        project.modules,
        project.modules.front().document,
        static_cast<std::uint32_t>(project.texts.front().find("return") + 7)
    );
    require(hover.result.has_value(), "generated literal has no hover result");
    require(hover.analysis.result == result.result, "hover did not share the semantic query");
}

auto run_round(const Project& project, Timings& timings) noexcept -> void {
    auto host = WorkspaceAnalysisHost();
    const auto record = [&](std::string name, auto action, std::size_t repetitions = 1) noexcept {
        timings[std::move(name)].push_back(measure(host, action, repetitions));
    };
    record("initial_updates", [&] noexcept { populate(host, project); });
    record("cold_semantic", [&] noexcept {
        const auto result = host.snapshot().semantic(project.modules);
        require(result.result->program() != nullptr, "cold semantic analysis failed");
    });
    record("cold_hover", [&] noexcept { query(host, project); });
    record("warm_semantic_hover", [&] noexcept { query(host, project); }, 20);
    record("version_only_update", [&] noexcept {
        update(host, project.modules.front().document, 2, project.texts.front());
    });
    record("version_only_query", [&] noexcept { query(host, project); });
    record("unselected_update", [&] noexcept {
        update(host, "untitled:unselected", 2, "fn changed_outside() {}");
    });
    record("unselected_query", [&] noexcept { query(host, project); });
    record("selected_body_update", [&] noexcept {
        update(host, project.modules.front().document, 3, "fn value_0() -> i32 { return 2; }");
    });
    record("selected_body_query", [&] noexcept { query(host, project); });
    record("selected_signature_update", [&] noexcept {
        update(host, project.modules.front().document, 4, "fn value_0() -> i64 { return 2; }");
    });
    record("selected_signature_query", [&] noexcept { query(host, project); });
}

template<typename Value>
auto median(std::vector<Value> values) noexcept -> double {
    std::ranges::sort(values);
    const auto middle = values.size() / 2;
    if (values.size() % 2 != 0) {
        return static_cast<double>(values[middle]);
    }
    return (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2;
}

template<typename Projection>
auto median_field(std::span<const Sample> samples, Projection projection) noexcept -> double {
    auto values = std::vector<double>();
    for (const auto& sample : samples) {
        values.push_back(static_cast<double>(projection(sample)));
    }
    return median(std::move(values));
}

auto report_timings(std::size_t size, const Timings& timings) noexcept -> void {
    for (const auto& [name, samples] : timings) {
        std::println(
            "timing,{},{},{},{},{},{:.3f},{:.0f},{:.0f},{:.0f},{:.0f},,,,,,",
            size,
            size,
            samples.size(),
            name,
            name == "warm_semantic_hover" ? 20 : 1,
            median_field(
                samples,
                [](const Sample& sample) static noexcept { return sample.microseconds; }
            ),
            median_field(
                samples,
                [](const Sample& sample) static noexcept { return sample.counts.syntax; }
            ),
            median_field(
                samples,
                [](const Sample& sample) static noexcept { return sample.counts.document_symbols; }
            ),
            median_field(
                samples,
                [](const Sample& sample) static noexcept { return sample.counts.workspace_symbols; }
            ),
            median_field(samples, [](const Sample& sample) static noexcept {
                return sample.counts.semantic;
            })
        );
    }
}

struct Retention final {
    bool semantic_after_update;
    bool semantic_after_query;
    bool semantic_after_release;
    bool syntax_after_update;
    bool syntax_after_query;
    bool syntax_after_release;
};

auto retention(const Project& project, bool retain_snapshot) noexcept -> Retention {
    auto host = WorkspaceAnalysisHost();
    populate(host, project);
    auto snapshot = std::optional(host.snapshot());
    const auto old_semantic = std::weak_ptr(snapshot->semantic(project.modules).result);
    const auto old_syntax =
        std::weak_ptr(snapshot->syntax(project.modules.front().document)->result);
    if (!retain_snapshot) {
        snapshot.reset();
    }
    update(host, project.modules.front().document, 2, "fn value_0() -> i32 { return 2; }");
    const auto semantic_after_update = !old_semantic.expired();
    const auto syntax_after_update = !old_syntax.expired();
    query(host, project);
    const auto semantic_after_query = !old_semantic.expired();
    const auto syntax_after_query = !old_syntax.expired();
    snapshot.reset();
    return {
        .semantic_after_update = semantic_after_update,
        .semantic_after_query = semantic_after_query,
        .semantic_after_release = !old_semantic.expired(),
        .syntax_after_update = syntax_after_update,
        .syntax_after_query = syntax_after_query,
        .syntax_after_release = !old_syntax.expired(),
    };
}

auto report_retention(const Project& project, std::size_t samples, bool retain_snapshot) noexcept
    -> void {
    auto observations = std::array<std::vector<int>, 6>();
    for (auto round = 0uz; round < samples; ++round) {
        const auto result = retention(project, retain_snapshot);
        observations[0].push_back(result.semantic_after_update);
        observations[1].push_back(result.semantic_after_query);
        observations[2].push_back(result.semantic_after_release);
        observations[3].push_back(result.syntax_after_update);
        observations[4].push_back(result.syntax_after_query);
        observations[5].push_back(result.syntax_after_release);
    }
    std::println(
        "retention,{},{},{},{},1,,,,,,{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f}",
        project.modules.size(),
        project.modules.size(),
        samples,
        retain_snapshot ? "retained_snapshot" : "released_snapshot",
        median(observations[0]),
        median(observations[1]),
        median(observations[2]),
        median(observations[3]),
        median(observations[4]),
        median(observations[5])
    );
}

} // namespace

extern "C++" auto main(int argc, char* argv[]) noexcept -> int {
    auto samples = 5uz;
    if (argc != 1) {
        require(
            argc == 3 && std::string_view(argv[1]) == "--samples",
            "usage: workspace-benchmark-analysis [--samples 1..100]"
        );
        const auto text = std::string_view(argv[2]);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), samples);
        require(
            parsed.ec == std::errc()
                && parsed.ptr == text.data() + text.size()
                && samples > 0
                && samples <= 100,
            "samples must be in 1..100"
        );
    }
    std::println(
        "kind,modules,functions,samples,operation,repetitions,median_us,workspace_syntax_delta,document_symbols_delta,workspace_symbols_delta,semantic_delta,old_semantic_after_update,old_semantic_after_query,old_semantic_after_snapshot_release,old_syntax_after_update,old_syntax_after_query,old_syntax_after_snapshot_release"
    );
    for (const auto size : std::array {10uz, 100uz, 500uz}) {
        const auto project = make_project(size);
        auto timings = Timings();
        for (auto round = 0uz; round < samples; ++round) {
            run_round(project, timings);
        }
        report_timings(size, timings);
        report_retention(project, samples, false);
        report_retention(project, samples, true);
    }
    return 0;
}
