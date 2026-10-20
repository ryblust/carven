module;

#include <carven/hosting/api.hpp>
#include <carven/hosting/value.hpp>

module carven:hosting.program.impl;

import :compiler.analysis;
import :diagnostics.report;
import :frontend.document;
import :frontend.program.dependencies;
import :frontend.program.parse;
import :hosting.program;
import :hosting.session;
import :hosting.value;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.document;
import :source.location;
import :source.manager;
import :source.module_path;
import :support.invariant;
import std;

extern "C++" {

namespace {

auto host_error(HostErrorKind kind, std::string message) noexcept -> std::unexpected<HostError> {
    return std::unexpected(hosting_error(kind, std::move(message)));
}

auto host_location(const SemIRProgram& program, ProgramOriginID origin) noexcept -> HostLocation {
    const auto provenance = program.provenance();
    const auto source = provenance.source_origin(origin);
    const auto position = provenance.location(origin);
    return {
        .file = std::string(provenance.source_snapshot(source.source_id).display_origin()),
        .line = position.line,
        .column = position.column
    };
}

} // namespace

HostProgram::HostProgram(std::shared_ptr<const HostProgramState> state) noexcept
    : state(std::move(state)) {}

HostFunction::HostFunction(std::shared_ptr<const HostFunctionState> state) noexcept
    : state(std::move(state)) {}

HostFailure::HostFailure(HostRecord payload) noexcept
    : value(std::move(payload)) {}

HostFailure::HostFailure(HostEnum payload) noexcept
    : value(std::move(payload)) {}

auto host_call_options() noexcept -> HostCallOptions {
    const auto limits = static_execution_limits();
    return {
        .output = {.context = nullptr, .write = nullptr},
        .allow_host_effects = false,
        .limits =
            {.steps = limits.steps,
             .byte_work = limits.byte_work,
             .aggregate_work = limits.aggregate_work},
        .native_types = {}
    };
}

auto HostProgram::load(
    std::string_view module_path,
    std::string_view text,
    HostOutput output
) noexcept -> std::expected<HostProgram, HostError> {
    const auto source = HostSource {
        .module_path = std::string(module_path),
        .origin = std::string(module_path) + ".cv",
        .text = std::string(text)
    };
    return load(module_path, std::span(&source, 1uz), output);
}

auto HostProgram::load(
    std::string_view module_path,
    std::span<const HostSource> batch,
    HostOutput output,
    HostSourceResolver dependencies
) noexcept -> std::expected<HostProgram, HostError> {
    const auto entry_path = CanonicalModulePath::from_value(module_path);
    if (!entry_path) {
        return host_error(HostErrorKind::Input, "invalid entry module path");
    }
    auto sources = SourceManager();
    auto inputs = std::vector<SourceModuleInput>();
    auto roots = std::vector<CanonicalModulePath>();
    auto loaded = std::map<CanonicalModulePath, SourceID>();
    for (const auto& input : batch) {
        auto path = CanonicalModulePath::from_value(input.module_path);
        if (!path || loaded.contains(*path)) {
            return host_error(
                HostErrorKind::Input,
                "invalid or duplicate source module: " + input.module_path
            );
        }
        const auto source = sources.append_virtual(input.origin, input.text);
        if (!source) {
            return host_error(
                HostErrorKind::Input,
                "source text could not be loaded: " + input.origin
            );
        }
        loaded.emplace(*path, *source);
        roots.push_back(*path);
        inputs.push_back({.source_id = *source, .module_path = std::move(*path)});
    }
    if (!loaded.contains(*entry_path)) {
        return host_error(HostErrorKind::Input, "entry module is not in the source batch");
    }
    auto parsed = std::vector<ParsedSourceInput>();
    auto load_error = std::optional<HostError>();
    const auto load =
        [&](const CanonicalModulePath& path) noexcept -> std::shared_ptr<const ParsedDocument> {
        if (load_error) {
            return {};
        }
        auto found = loaded.find(path);
        if (found == loaded.end()) {
            if (dependencies.resolve == nullptr) {
                return {};
            }
            auto resolved = dependencies.resolve(dependencies.context, path.value());
            if (!resolved) {
                load_error = std::move(resolved.error());
                return {};
            }
            if (!*resolved) {
                return {};
            }
            auto& input = **resolved;
            if (input.module_path != path.value()) {
                load_error = hosting_error(
                    HostErrorKind::Input,
                    std::format(
                        "source resolver returned module '{}' for '{}'",
                        input.module_path,
                        path.value()
                    )
                );
                return {};
            }
            const auto source =
                sources.append_virtual(std::move(input.origin), std::move(input.text));
            if (!source) {
                load_error = hosting_error(
                    HostErrorKind::Input,
                    "source text could not be loaded: " + source.error().origin
                );
                return {};
            }
            found = loaded.emplace(path, *source).first;
            inputs.push_back({.source_id = *source, .module_path = path});
        }
        auto document = parse_document(SourceDocument::retain(sources, found->second));
        parsed.push_back({.source_id = found->second, .document = document});
        return document;
    };
    static_cast<void>(source_dependency_closure(roots, load));
    if (load_error) {
        return std::unexpected(std::move(*load_error));
    }
    const auto write = [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
        if (output.write != nullptr) {
            output.write(output.context, stream == ExecutionOutputStream::Error, bytes);
        }
    };
    auto analysis = analyze_compilation(
        std::move(sources),
        SourceBatch {.modules = inputs},
        CompilationAnalysisOptions {
            .output = write,
            .timings = {},
            .collect_source_observations = false,
            .capture_output = false,
            .host_effects = HostEffects::Deny,
        },
        parsed
    );
    auto diagnostics = render_diagnostics(analysis.diagnostics(), analysis.sources(), false);
    auto program = std::move(analysis).take_program();
    if (!program) {
        return host_error(HostErrorKind::Analysis, std::move(diagnostics));
    }
    return HostProgram(
        std::make_shared<HostProgramState>(HostProgramState {
            .program = std::move(*program),
            .diagnostics = std::move(diagnostics),
            .module_path = std::string(module_path),
        })
    );
}

auto HostProgram::diagnostics() const noexcept -> std::string_view {
    return state ? std::string_view(state->diagnostics) : std::string_view();
}

auto HostProgram::function(std::string_view name) const noexcept
    -> std::expected<HostFunction, HostError> {
    if (!state) {
        return host_error(HostErrorKind::Lookup, "program handle was moved from");
    }
    auto found = state->function(name);
    if (!found) {
        return std::unexpected(std::move(found.error()));
    }
    return HostFunction(
        std::make_shared<HostFunctionState>(HostFunctionState {.owner = state, .function = *found})
    );
}

auto HostProgramState::function(std::string_view name) const noexcept
    -> std::expected<FunctionID, HostError> {
    for (const auto entry : program.declarations().functions()) {
        const auto& declaration = entry.value;
        const auto provenance_module =
            program.declarations().module_decl(declaration.module_id).provenance_module;
        if (program.provenance().module_record(provenance_module).path.value() != module_path
            || program.provenance().spelling(declaration.name) != name
            || declaration.visibility == DeclarationVisibility::Module) {
            continue;
        }
        const auto& callable = program.declarations().callable(declaration.callable);
        const auto& signature = program.callable_signatures().signature(callable.signature);
        if (!std::holds_alternative<FunctionBodyImplementation>(callable.implementation)
            || !callable_body_id(callable)
            || !hosting_signature_supported(program, signature, true, true)) {
            return host_error(
                HostErrorKind::Signature,
                "function is outside the owning hosted call contract"
            );
        }
        return entry.id;
    }
    return host_error(HostErrorKind::Lookup, "visible source function was not found");
}

auto HostFunction::call(
    std::span<const HostValue> arguments,
    HostCallOptions options
) const noexcept -> std::expected<HostOutcome, HostError> {
    if (!state) {
        return host_error(HostErrorKind::Lookup, "function handle was moved from");
    }
    const auto& program = state->owner->program;
    const auto& declaration = program.declarations().function(state->function);
    const auto& signature = program.callable_signatures().signature(
        program.declarations().callable(declaration.callable).signature
    );
    if (signature.execution != CallableExecutionKind::Synchronous) {
        return host_error(HostErrorKind::Signature, "async functions require a hosting session");
    }
    auto session_options = host_session_options();
    session_options.calls = options;
    auto session = HostSession::create(HostProgram(state->owner), session_options);
    if (!session) {
        return std::unexpected(std::move(session.error()));
    }
    auto task = session->submit(*this, arguments);
    if (!task) {
        return std::unexpected(std::move(task.error()));
    }
    while (task->status() != HostTaskStatus::Finished) {
        session->poll(256uz);
    }
    return std::move(*task->state->result);
}

auto HostProgramState::complete(
    FunctionID function,
    ExecutionResult<ExecutionValue> result,
    HostLimits limits,
    std::span<const HostNativeType> native_types
) const noexcept -> std::expected<HostOutcome, HostError> {
    const auto& declaration = program.declarations().function(function);
    const auto& signature = program.callable_signatures().signature(
        program.declarations().callable(declaration.callable).signature
    );
    if (result) {
        auto exported = hosting_export(program, *result, signature.result, limits, native_types);
        if (!exported) {
            return std::unexpected(std::move(exported.error()));
        }
        return HostOutcome(std::move(*exported));
    }
    if (const auto* failure = std::get_if<ExecutionSourceFailure>(&result.error())) {
        if (std::ranges::find(
                program.failure_sets().failure_set(signature.failures).members,
                failure->type
            )
            == program.failure_sets().failure_set(signature.failures).members.end()) {
            invariant_violation("hosted call returned an undeclared failure");
        }
        auto payload =
            hosting_export(program, *failure->payload, failure->type, limits, native_types);
        if (!payload) {
            return std::unexpected(std::move(payload.error()));
        }
        if (auto* record = std::get_if<HostRecord>(&*payload)) {
            return HostOutcome(HostFailure(std::move(*record)));
        }
        if (auto* enumeration = std::get_if<HostEnum>(&*payload)) {
            return HostOutcome(HostFailure(std::move(*enumeration)));
        }
        invariant_violation("hosted failure lost its nominal payload");
    }
    if (std::holds_alternative<ExecutionCancelled>(result.error())) {
        return HostOutcome(HostCancelled {});
    }
    if (const auto* halt = std::get_if<ExecutionHalt>(&result.error())) {
        auto error = hosting_error(HostErrorKind::Execution, std::string(halt->event.message()));
        error.location = host_location(program, halt->event.origin);
        for (const auto origin : halt->event.calls) {
            error.calls.push_back(host_location(program, origin));
        }
        return std::unexpected(std::move(error));
    }
    return host_error(HostErrorKind::Execution, "hosted execution did not complete");
}

auto HostFailure::module_path() const noexcept -> std::string_view {
    return value.visit([](const auto& payload) static noexcept -> std::string_view {
        using Payload = std::remove_cvref_t<decltype(payload)>;
        if constexpr (std::same_as<Payload, HostRecord> || std::same_as<Payload, HostEnum>) {
            return payload.module_path;
        } else {
            std::unreachable();
        }
    });
}

auto HostFailure::name() const noexcept -> std::string_view {
    return value.visit([](const auto& payload) static noexcept -> std::string_view {
        using Payload = std::remove_cvref_t<decltype(payload)>;
        if constexpr (std::same_as<Payload, HostRecord> || std::same_as<Payload, HostEnum>) {
            return payload.name;
        } else {
            std::unreachable();
        }
    });
}

auto HostFailure::payload() const& noexcept -> const HostValue& {
    return value;
}

auto HostFailure::payload() && noexcept -> HostValue {
    return std::move(value);
}

auto HostFailure::same_type(const HostFailure& other) const noexcept -> bool {
    return !name().empty() && module_path() == other.module_path() && name() == other.name();
}

} // extern "C++"
