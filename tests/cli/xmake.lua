local case_specs = {
    ["input/mapped"] = {
        inputs = {"second.cv"},
        fixtures = {["../../commands/manifest/input.cv"] = "crafts/toolchain/crafts/input.cv"},
        steps = {
            {args = {"compile", "--explicit-inputs", "--source=logical.input=crafts/toolchain/crafts/input.cv",
                     "--artifact-manifest=manifest.json", "-o", "emit"},
                output_files = {"emit/logical/input.cpp", "manifest.json"},
                file_contains = {["manifest.json"] = {'"module": "logical.input"', 'crafts/toolchain/crafts/input.cv'}}},
            {args = {"check", "--explicit-inputs", "--source=logical.input=crafts/toolchain/crafts/input.cv"},
                stderr_contains = {"check passed"}},
            {args = {"check", "--explicit-inputs", "--source=logical.input=crafts/toolchain/crafts/input.cv",
                     "--source=logical.input=crafts/toolchain/crafts/input.cv"},
                stderr_contains = {"check passed"}},
            {args = {"compile", "--explicit-inputs", "--source=first=crafts/toolchain/crafts/input.cv",
                     "--source=second=crafts/toolchain/crafts/input.cv", "-o", "conflict"},
                exit_code = 1, stderr_contains = {"conflicting module identities"}, absent_files = {"conflict"}},
            {args = {"compile", "--explicit-inputs", "--source=same=crafts/toolchain/crafts/input.cv",
                     "--source=same=second.cv", "-o", "duplicate"},
                exit_code = 1, stderr_contains = {"duplicate module path"}, absent_files = {"duplicate"}},
            {args = {"compile", "--explicit-inputs", "--source=logical.input=crafts/toolchain/crafts/input.cv", "--stdout"},
                stdout_contains = {"==> logical/input.cpp <=="}},
            {args = {"compile", "--explicit-inputs", "--source=bad..module=crafts/toolchain/crafts/input.cv"},
                exit_code = 1, stderr_contains = {"invalid source mapping"}},
            {args = {"compile", "--source=logical.input=crafts/toolchain/crafts/input.cv"},
                exit_code = 1, stderr_contains = {"source mappings require --explicit-inputs"}},
        },
    },
    ["commands/manifest"] = {
        inputs = {"input.cv"},
        fixtures = {["../../diagnostics/syntax/input.cv"] = "invalid.cv"},
        steps = {
            {args = {"compile", "--explicit-inputs", "--tests=main", "--linkage-domain=manifest:app", "--artifact-manifest=metadata/build.json", "-o", "emit", "input.cv"},
                output_files = {"metadata/build.json", "emit/input.cpp", "emit/carven/api/input.hpp"},
                file_contains = {["metadata/build.json"] = {
                    '"path": "input.cpp", "role": "module-implementation", "source_mapping": "source-attributed", "source": "input.cv"',
                    '"path": "carven/generated/carven-test-main.cpp", "role": "test-entry", "source_mapping": "source-attributed", "source": null',
                }},
                file_not_contains = {["metadata/build.json"] = {'"generation":', '"selection":'}}},
            {args = {"compile", "--explicit-inputs", "--artifact-manifest=failed.json", "-o", "failed", "invalid.cv"},
                exit_code = 1, stderr_contains = {"CV-SYNTAX"}, absent_files = {"failed.json", "failed"}},
            {args = {"compile", "--explicit-inputs", "--artifact-manifest=input.cv", "-o", "collision", "input.cv"},
                exit_code = 1, stderr_contains = {"manifest would overwrite a source input"}, absent_files = {"collision"}},
            {args = {"compile", "--explicit-inputs", "--artifact-manifest=collision/input.cpp", "-o", "collision", "input.cv"},
                exit_code = 1, stderr_contains = {"manifest would overwrite a generated artifact"}, absent_files = {"collision"}},
        },
    },
    ["commands/stdout_selection"] = {
        inputs = {"input.cv", "crafts/demo/helper.cv", "crafts/demo/unrelated.cv",
            "crafts/cycle/a.cv", "crafts/cycle/b.cv"},
        steps = {
            {installed_toolchain = true, args = {"compile", "input.cv", "--stdout"},
                stdout_contains = {"==> input.cpp <==", "#include <carven/generated/crafts/demo/helper.hpp>"},
                stdout_not_contains = {"==> crafts/", "==> carven/generated/crafts/", "unrelated"}},
            {installed_toolchain = true, args = {"compile", "input.cv", "crafts/demo/helper.cv", "--stdout"},
                stdout_contains = {"==> input.cpp <==", "==> crafts/demo/helper.cpp <==", "==> carven/generated/crafts/demo/helper.hpp <=="},
                stdout_not_contains = {"unrelated", "==> crafts/carven/"}},
            {installed_toolchain = true, args = {"compile", "crafts/cycle/b.cv", "--stdout"},
                stdout_contains = {"==> crafts/cycle/b.cpp <==", "==> carven/generated/crafts/cycle/a.hpp <==", "ATag", "BTag"},
                stdout_not_contains = {"==> crafts/cycle/a.cpp <==", "==> carven/generated/crafts/cycle/b.hpp <==", "==> input.cpp <=="}},
            {installed_toolchain = true, args = {"compile", "input.cv", "-o", "emit"},
                output_files = {"emit/input.cpp", "emit/crafts/demo/helper.cpp", "emit/crafts/demo/unrelated.cpp"}},
        },
    },
    ["commands/pointer_display/unavailable"] = {
        case_dir = "commands/pointer_display",
        inputs = {"unavailable.cv"},
        args = {"interpret", "--explicit-inputs", "unavailable.cv"}, exit_code = 1,
        stdout_contains = {"element=const@nonnull"},
        stderr_contains = {"CV-INTERPRET-EXECUTION", "pointer address representation is not known during execution"},
    },
    ["commands/pointer_display/addresses"] = {
        case_dir = "commands/pointer_display",
        inputs = {"input.cv"},
        args = {"interpret", "--explicit-inputs", "input.cv"},
        stdout_ordered = {"local=const@nonnull", "cstring=const@nonnull", "123", "null=const@null",
            "local=interp@object#", "cstring=interp@object#", "123", "null=interp@null"},
        stdout_not_contains = {"0x"},
    },
    ["commands/cstring_text"] = {
        inputs = {"input.cv"},
        args = {"interpret", "--explicit-inputs", "input.cv"}, stdout = "stdout.txt", stderr = "stderr.txt",
    },
    ["commands/structural_display"] = {
        inputs = {"input.cv", "failure.cv", "helper.cv"},
        steps = {
            {args = {"interpret", "--explicit-inputs", "input.cv", "helper.cv"}, stdout = "stdout.txt"},
            {args = {"check", "--explicit-inputs", "failure.cv"}, exit_code = 1,
                stderr_contains = {'actual: [\n        12,\n    ]', 'expected: [\n        15,\n    ]',
                    'true: <not evaluated>', 'condition: 1 > 2'},
                stderr_not_contains = {'1: 1', '2: 2', 'false: false'}},
        },
    },
    ["commands/check"] = {
        inputs = {"input.cv", "invalid_test.cv", "crafts/demo/dependency.cv"},
        fixtures = {
            ["../interpretation/static_failure.cv"] = "static_failure.cv",
            ["../../diagnostics/syntax/input.cv"] = "syntax.cv",
            ["../../diagnostics/warning/input.cv"] = "warning.cv",
        },
        steps = {
            {args = {"check"}, exit_code = 1, stderr_contains = {"requires at least one source file", "carven check --help"}},
            {args = {"check", "input.cv", "--stdout"}, exit_code = 1, stderr_contains = {"unknown check option", "carven check --help"}},
            {args = {"check", "--explicit-inputs", "syntax.cv"}, exit_code = 1, stderr_contains = {"CV-SYNTAX", "syntax.cv:1:12"}},
            {args = {"check", "--explicit-inputs", "warning.cv"}, stderr_ordered = {"CV-LINT-UNUSED-LOCAL", "carven: check passed\n"}},
            {args = {"check", "--explicit-inputs", "static_failure.cv"}, exit_code = 1, stderr_contains = {"CV-CONST-TEST"}},
            {args = {"check", "--explicit-inputs", "invalid_test.cv"}, exit_code = 1, stderr_contains = {"CV-TYPE-MISMATCH", "invalid_test.cv:2:"}},
            {
                -- Keep one ordinary check with installed and project Crafts discovery.
                installed_toolchain = true,
                args = {"check", "input.cv", "crafts/demo/dependency.cv"},
                stdout = "stdout.txt", stderr = "stderr.txt",
                absent_files = {"input.cpp", "crafts/demo/dependency.cpp", "warning.cpp", "carven", "program", "program.exe"},
            },
        },
    },
    ["commands/timings"] = {
        inputs = {"input.cv"},
        fixtures = {
            ["../interpretation/escaped_failure.cv"] = "failure.cv",
        },
        steps = {
            {
                args = {"check", "--explicit-inputs", "--timings", "input.cv"}, stdout = "compile.txt",
                stderr_contains = {"carven: check passed in ", "Semantic analysis"},
                stderr_not_contains = {"C++ generation", "Execution"},
            },
            {
                args = {"compile", "--explicit-inputs", "input.cv", "--stdout", "--timings"},
                stdout_contains = {"==> input.cpp <=="},
                stdout_not_contains = {"carven: compilation finished in "},
                stderr_contains = {"carven: compilation finished in ", "C++ generation"},
            },
            {
                args = {"interpret", "--explicit-inputs", "--timings", "--trace", "input.cv"}, stdout = "run.txt",
                stderr_ordered = {"statement", "carven: interpretation finished in ", "Execution"},
                stderr_not_contains = {"C++ generation", "Native compilation"},
            },
            {
                args = {"check", "--explicit-inputs", "--timings", "missing.cv"}, exit_code = 1,
                stderr_ordered = {"cannot read source file", "carven: check failed in "},
                stderr_not_contains = {"Semantic analysis"},
            },
            {
                args = {"interpret", "--explicit-inputs", "--timings", "failure.cv"}, exit_code = 1,
                stderr_ordered = {"CV-INTERPRET-EXECUTION", "carven: interpretation failed in ", "Execution"},
            },
        },
    },
    ["commands/default_initialization"] = {
        inputs = {"input.cv"},
        args = {"interpret", "--explicit-inputs", "input.cv"}, stdout = "run.txt",
    },
    ["commands/const_blocks"] = {
        inputs = {"input.cv", "helper.cv"},
        steps = {
            {args = {"check", "--explicit-inputs", "input.cv", "helper.cv"}, stderr = "../check/passed.txt", stdout = "stdout.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n"}},
            {args = {"interpret", "--explicit-inputs", "input.cv", "helper.cv"}, stdout = "run.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n", "runtime\nruntime\n"}},
        },
    },
    ["commands/static_execution"] = {
        inputs = {"input.cv"},
        steps = {
            {args = {"compile", "--explicit-inputs", "input.cv", "-o", "emit"}, stdout = "stdout.txt", stderr = "stderr.txt"},
            {args = {"compile", "--explicit-inputs", "input.cv", "--stdout"}, stderr = "combined.txt", stdout_contains = {"==> input.cpp <=="}},
        },
    },
    ["commands/dump"] = {
        inputs = {"input.cv", "lexical_error.cv", "syntax_error.cv"},
        steps = {
            {args = {"dump"}, exit_code = 1, stderr_contains = {"expected", "carven dump --help"}},
            {args = {"dump", "input.cv"}, stdout_ordered = {"Tokens \"input.cv\"", "SourceModule"}},
            {args = {"dump", "tokens", "input.cv", "--timings"}, stdout_contains = {"Tokens \"input.cv\""},
                stdout_not_contains = {"SourceModule", "carven: dump finished in "},
                stderr_contains = {"carven: dump finished in ", "Lexing"},
                stderr_not_contains = {"Parsing", "Semantic analysis"}},
            {args = {"dump", "unknown", "input.cv"}, exit_code = 1, stderr_contains = {"unknown dump kind"}},
            {args = {"dump", "ast", "input.cv"}, stdout_contains = {"SourceModule"},
                stdout_not_contains = {"Tokens \""}},
            {
                args = {"dump", "tokens", "lexical_error.cv"},
                exit_code = 1,
                stdout_contains = {"Tokens \"lexical_error.cv\"", "Fn", "Invalid"},
                stderr_contains = {"CV-LEXICAL", "lexical_error.cv"},
            },
            {
                args = {"dump", "ast", "syntax_error.cv"},
                exit_code = 1,
                stderr_contains = {"CV-SYNTAX", "syntax_error.cv"},
            },
        },
    },
    ["invocation/help"] = {
        steps = {
            {args = {"--help"}, stdout_contains = {"Usage:", "carven [options]", "Commands:"}},
            {args = {"compile", "--help"}, stdout_contains = {"carven compile [options]", "--output-dir", "--tests"}},
            {args = {"check", "--help"}, stdout_contains = {"carven check [options]"}},
            {args = {"interpret", "--help"}, stdout_contains = {"carven interpret [options]", "--max-steps"}},
            {args = {"dump", "--help"}, stdout_contains = {"carven dump [kind]", "tokens", "ast"}},
        },
    },
    ["input/invalid_extension"] = {
        args = {"compile", "unknown"},
        exit_code = 1,
        stderr = "stderr.txt",
    },
    ["input/missing"] = {
        args = {"compile", "missing.cv"},
        exit_code = 1,
        stderr_contains = {"cannot read source file", "missing.cv"},
    },
    ["invocation/version"] = {
        args = {"--version"},
        stdout = "stdout.txt",
    },
    ["diagnostics/syntax"] = {
        inputs = {"input.cv"},
        args = {"compile", "--explicit-inputs", "input.cv"},
        exit_code = 1,
        stderr_contains = {"CV-SYNTAX", "expected parameter name", "input.cv:1:12"},
    },
    ["diagnostics/warning"] = {
        inputs = {"input.cv"},
        args = {"compile", "--explicit-inputs", "input.cv"},
        stderr_contains = {"CV-LINT-UNUSED-LOCAL", "input.cv"},
        output_files = {"input.cpp"},
        absent_files = {"carven/generated/input.hpp"},
    },
    ["output/default"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"compile", "--explicit-inputs", "bare_structure.cv"},
        output_files = {"bare_structure.cpp", "carven/generated/bare_structure.hpp"},
    },
    ["output/destination"] = {
        fixtures = {
            ["../fixtures/bare_structure.cv"] = "bare_structure.cv",
            ["../state/preexisting.fixture"] = "emit/bare_structure.cpp",
            ["../state/stale.fixture"] = "emit/stale.txt",
        },
        args = {"compile", "--explicit-inputs", "--output-dir=emit", "bare_structure.cv"},
        output_files = {
            "emit/bare_structure.cpp",
            "emit/carven/generated/bare_structure.hpp",
            "emit/stale.txt",
        },
        file_not_contains = {
            ["emit/bare_structure.cpp"] = {"preexisting output"},
        },
    },
    ["output/stdout"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"compile", "--explicit-inputs", "bare_structure.cv", "--stdout"},
        stdout_ordered = {
            "==> bare_structure.cpp <==",
            "==> carven/generated/bare_structure.hpp <==",
        },
        absent_files = {
            "bare_structure.cpp",
            "carven/generated/bare_structure.hpp",
        },
    },
    ["output/testing_modes"] = {
        fixtures = {["../fixtures/passing_test.cv"] = "passing_test.cv"},
        steps = {
            {
                args = {"compile", "--explicit-inputs", "-o", "none", "passing_test.cv"},
                output_files = {"none/passing_test.cpp"},
                absent_files = {
                    "none/carven/generated/passing_test.hpp",
                    "none/carven/generated/carven-test-runner.hpp",
                    "none/carven/generated/carven-test-main.cpp",
                },
            },
            {
                args = {"compile", "--explicit-inputs", "passing_test.cv", "--tests=main", "--output-dir=default"},
                output_files = {
                    "default/passing_test.cpp",
                    "default/carven/generated/carven-test-runner.hpp",
                    "default/carven/generated/carven-test-main.cpp",
                },
            },
            {
                args = {"compile", "--explicit-inputs", "--tests=runner", "--output-dir", "external", "passing_test.cv"},
                output_files = {
                    "external/passing_test.cpp",
                    "external/carven/generated/carven-test-runner.hpp",
                },
                absent_files = {
                    "external/carven/generated/passing_test.hpp",
                    "external/carven/generated/carven-test-main.cpp",
                },
            },
        },
    },
    ["output/sink_failure"] = {
        fixtures = {
            ["../fixtures/bare_structure.cv"] = "bare_structure.cv",
            ["../state/preexisting.fixture"] = "blocked",
        },
        args = {"compile", "--explicit-inputs", "bare_structure.cv", "--output-dir=blocked"},
        exit_code = 1,
        stderr_contains = {"carven: error: cannot create directory 'blocked':"},
        output_files = {"blocked"},
        absent_files = {
            "bare_structure.cpp",
            "carven/generated/bare_structure.hpp",
        },
        file_contains = {
            ["blocked"] = {"preexisting output"},
        },
    },
    ["invocation/invalid_option"] = {
        args = {"compile", "--unknown", "input.cv"},
        exit_code = 1,
        stderr_contains = {"unknown option '--unknown'", "carven compile --help"},
    },
    ["module_layout/deduplication"] = {
        project = "../project",
        args = {"compile", "--explicit-inputs", "nested/local.cv", "./nested/local.cv", "-o", "emit"},
        output_files = {"emit/nested/local.cpp"},
        absent_files = {"emit/main.cpp"},
    },
    ["module_layout/spec"] = {
        project = "../project",
        args = {
            "compile",
            "--explicit-inputs",
            "--output-dir=emit",
            "main.cv",
            "sibling.cv",
            "nested/worker.cv",
            "nested/local.cv",
            "__internal.cv",
            "import/export.cv",
        },
        output_files = {
            "emit/main.cpp",
            "emit/sibling.cpp",
            "emit/nested/worker.cpp",
            "emit/nested/local.cpp",
            "emit/__internal.cpp",
            "emit/import/export.cpp",
            "emit/carven/generated/import/export.hpp",
            "emit/carven/generated/main.hpp",
            "emit/carven/generated/sibling.hpp",
            "emit/carven/generated/nested/worker.hpp",
            "emit/carven/generated/nested/local.hpp",
            "emit/carven/generated/__internal.hpp",
        },
    },
}

case_specs["commands/native_execution"] = {
    inputs = {"input.cv", "library.cv", "unlisted.cv",
        "crafts/demo/api.cv", "crafts/demo/native.hpp", "crafts/demo/native.cpp"},
    fixtures = {
        ["../../../../crafts/carven/runtime"] = "installation/crafts/toolchain with spaces/crafts/carven/runtime",
    },
    steps = {
        {
            installed_toolchain = true,
            args = {"--timings", "input.cv", "./input.cv", "crafts/demo/api.cv", "--", "--timings", "--help", "space argument", "; echo injected", "", 'a"b', "trailing\\", 'slash\\"quote'},
            run_timeout = 120000,
            exit_code = 1,
            stdout = "stdout.txt",
            stderr_ordered = {"failure 'input.Failure' escaped the program entry",
                "carven: run exited with code 1 in ", "Native compilation", "Execution"},
            absent_files = {"input.cpp", "program", "program.exe"},
        },
        {
            installed_toolchain = true,
            args = {"library.cv"},
            exit_code = 1,
            stderr_contains = {"running a program requires a runtime entry point"},
            absent_files = {"library.cpp", "program"},
        },
        {
            args = {"input.cv", "--stdout"},
            exit_code = 1,
            stderr_contains = {"unknown option '--stdout'", "carven --help"},
        },
    },
}

local interpretation = {
    fixtures = {
        ["../../../language/functions/interpreted_runtime.cv"] = "shared.cv",
        ["../../../language/types/pointer_graph.cv"] = "local_graph.cv",
        ["../../../language/types/pointer_delivery.cv"] = "pointer_delivery.cv",
        ["../../../language/text/backing_identity.cv"] = "text_backing.cv",
        ["../../../language/async/scheduling.cv"] = "async_scheduling.cv",
        ["../../../language/async/waiting.cv"] = "async_waiting.cv",
        ["../../../language/async/composition.cv"] = "async_composition.cv",
        ["../../../language/async/failure.cv"] = "async_failure.cv",
        ["../../../language/async/cancelled.cv"] = "async_cancelled.cv",
    },
    inputs = {
        "input.cv", "unsupported.cv", "inactive_native.cv", "failure.cv", "implicit_failure.cv", "implicit_recovery.cv", "limit.cv", "wrapping.cv",
        "declarations.cv", "static_only.cv", "static_failure.cv", "main.cv", "floating.cv", "typed_failures.cv", "escaped_failure.cv", "composition_budget.cv",
        "async_waiting_entry.cv", "async_composition_entry.cv", "async_scheduling_entry.cv",
    },
    cases = {
        ["async-waiting"] = {args = {"interpret", "async_waiting_entry.cv", "async_waiting.cv"}, stdout_contains = {"true\n"}},
        ["async-composition"] = {args = {"interpret", "async_composition_entry.cv", "async_composition.cv"}, stdout_contains = {"true\n"}},
        ["composition-budget"] = {args = {"interpret", "--max-steps", "128", "composition_budget.cv"}, exit_code = 1,
            stdout_not_contains = {"unreachable"}, stderr_contains = {"CV-INTERPRET-LIMIT"}},
        ["missing-input"] = {args = {"interpret"}, exit_code = 1, stderr_contains = {"requires at least one source file", "carven interpret --help"}},
        ["shared-runtime"] = {args = {"interpret", "--tests", "shared.cv"}, stdout_contains = {"shared runtime test\n"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["pointer-graph"] = {args = {"interpret", "--tests", "local_graph.cv"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["pointer-delivery"] = {args = {"interpret", "--tests", "pointer_delivery.cv"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["text-backing"] = {args = {"interpret", "--tests", "text_backing.cv"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["async-scheduling"] = {args = {"interpret", "async_scheduling_entry.cv", "async_scheduling.cv"}, stdout = "async_scheduling.txt",
            stderr_contains = {"CV-LINT-UNUSED-LOCAL", "unused local binding"}},
        ["async-failure"] = {args = {"interpret", "async_failure.cv"}, exit_code = 1,
            stderr_ordered = {"root failure selected", "child cancellation requested: true", "child closed",
                "CV-INTERPRET-EXECUTION", "typed failure escaped execution without recovery"}},
        ["async-cancellation"] = {args = {"interpret", "async_cancelled.cv"}, exit_code = 1,
            stderr_ordered = {"root requested child cancellation", "child accepts cancellation: true",
                "CV-INTERPRET-EXECUTION", "cancellation escaped execution without a consumer"},
            stderr_not_contains = {"unreachable child", "unreachable root"}},
        ["typed-failures"] = {
            args = {"interpret", "typed_failures.cv"},
            stdout = "typed_failures.txt",
        },
        ["escaped-failure"] = {
            args = {"interpret", "escaped_failure.cv"}, exit_code = 1,
            stderr_contains = {"CV-INTERPRET-EXECUTION", "typed failure escaped", "while interpreting this function call"},
        },
        ["implicit-failure"] = {
            args = {"interpret", "implicit_failure.cv"}, exit_code = 1,
            stderr_contains = {"CV-INTERPRET-EXECUTION", "typed failure escaped"},
        },
        ["implicit-recovery"] = {args = {"interpret", "implicit_recovery.cv"}, exit_code = 0},
        ["floating"] = {
            args = {"interpret", "floating.cv"},
            stdout = "floating.txt",
        },
        ["missing-entry"] = {
            args = {"interpret", "declarations.cv"}, exit_code = 1,
            stderr_contains = {"requires a runtime entry point"},
        },
        ["static-without-entry"] = {
            args = {"interpret", "--trace", "static_only.cv", "declarations.cv"}, exit_code = 1,
            stderr_contains = {"requires a runtime entry point"},
            stdout = "static_only.txt",
            absent_files = {"static_only.cpp", "declarations.cpp", "program"},
        },
        ["static-failure"] = {
            args = {"interpret", "static_failure.cv"}, exit_code = 1,
            stderr_contains = {"CV-CONST-TEST"},
        },
        ["explicit-entry"] = {
            args = {"interpret", "main.cv", "declarations.cv"},
            stdout = "main.txt",
        },
        ["duplicate-entry"] = {
            args = {"interpret", "main.cv", "input.cv"}, exit_code = 1,
            stderr_contains = {"CV-ENTRY-DUPLICATE"},
        },
        ["arguments"] = {
            args = {"interpret", "input.cv", "--", "--help"},
            stdout = "stdout.txt",
            absent_files = {"input.cpp", "program", "program.exe"},
        },
        ["trace"] = {
            args = {"interpret", "--trace", "input.cv"},
            stdout = "stdout.txt",
            stderr_contains = {"call main", "call fib", "return fib", "statement"},
        },
        ["unsupported"] = {
            args = {"interpret", "unsupported.cv"}, exit_code = 1,
            stderr_contains = {"CV-INTERPRET-ADMISSION"},
        },
        ["inactive-native"] = {args = {"interpret", "inactive_native.cv"}, exit_code = 0},
        ["division-failure"] = {
            args = {"interpret", "failure.cv"}, exit_code = 1,
            stdout = "failure.txt",
            stderr_ordered = {
                "failure.cv:1:28: error: division by zero in expression",
                "  called from: failure.cv:4:9",
                "  note: execution aborted",
            },
        },
        ["step-budget"] = {
            args = {"interpret", "--max-steps", "20", "limit.cv"}, exit_code = 1,
            stdout = "limit.txt",
            stderr_contains = {"CV-INTERPRET-LIMIT"},
        },
        ["invalid-step-budget"] = {
            args = {"interpret", "--max-steps", "-1", "input.cv"}, exit_code = 1,
            stderr_contains = {"nonnegative integer"},
        },
        ["wrapping"] = {
            args = {"interpret", "wrapping.cv"},
            stdout = "wrapping.txt",
        },
    },
}

-- These invocations share fixture bytes, not state or generated artifacts.
-- Register each independently so Xmake can schedule and report its actual cost.
for name, scenario in pairs(interpretation.cases) do
    local spec = table.clone(scenario)
    spec.case_dir = "commands/interpretation"
    spec.inputs = interpretation.inputs
    spec.fixtures = interpretation.fixtures
    if name ~= "missing-input" then
        table.insert(spec.args, 2, "--explicit-inputs")
    end
    case_specs["commands/interpretation/" .. name] = spec
end

case_specs["commands/test_options"] = {
    fixtures = {["../interpretation/declarations.cv"] = "declarations.cv"},
    steps = {
        {args = {"compile", "--explicit-inputs", "declarations.cv", "-o", "no-entry"},
            output_files = {"no-entry/declarations.cpp"}},
        {args = {"compile", "--explicit-inputs", "--tests=main", "declarations.cv", "-o", "empty-tests"},
            output_files = {"empty-tests/carven/generated/carven-test-main.cpp"}},
        {installed_toolchain = true, args = {"--tests", "declarations.cv"}, exit_code = 1,
            stderr_contains = {"requires at least one runtime test"}},
        {args = {"interpret", "--explicit-inputs", "--tests", "declarations.cv"}, exit_code = 1,
            stderr_contains = {"requires at least one runtime test"}},
        {args = {"interpret", "--tests", "--tests", "declarations.cv"}, exit_code = 1,
            stderr_contains = {"--tests may be specified only once"}},
        {args = {"--tests"}, exit_code = 1, stderr_contains = {"requires at least one source file"}},
        {args = {"compile", "--tests=runner", "--tests=main", "declarations.cv"}, exit_code = 1,
            stderr_contains = {"test emission mode was specified more than once"}},
    },
}

case_specs["commands/test_report"] = {
    fixtures = {["input.cv.fixture"] = "input.cv"},
    steps = {
        {args = {"interpret", "--explicit-inputs", "--tests", "input.cv"}, exit_code = 1,
            stdout = "stdout.txt", stderr = "stderr.txt"},
    },
}

case_specs["commands/anonymous_tests"] = {
    inputs = {"duplicate_label.cv", "reserved_label.cv"},
    steps = {
        {args = {"check", "--explicit-inputs", "duplicate_label.cv"}, exit_code = 1,
            stderr_contains = {"CV-TEST-DUPLICATE-NAME", "duplicate_label.cv:4:6"}},
        {args = {"check", "--explicit-inputs", "reserved_label.cv"}, exit_code = 1,
            stderr_contains = {"CV-TEST-MAIN-NAME", "reserved_label.cv:2:6"}},
    },
}

case_specs["commands/assertions"] = {
    inputs = {"failure.cv", "failure_entry.cv", "static_failure.cv", "fatal_test.cv", "loop_step_stop.cv"},
    fixtures = {["../../../language/testing/assertions.cv"] = "input.cv"},
    steps = {
        {args = {"interpret", "--explicit-inputs", "--tests", "input.cv"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        {args = {"interpret", "--explicit-inputs", "failure_entry.cv", "failure.cv"}, exit_code = 1,
            stderr_contains = {"message evaluated", "assertion failed", "actual == expected",
                "actual: [\n        1,\n        11,\n    ]", "expected: [\n        2,\n        22,\n    ]",
                "array mismatch", "failure.cv:",
                "  called from: failure.cv:", "  note: execution aborted"},
            stderr_not_contains = {"\n        99,", "unreachable"}},
        {args = {"check", "--explicit-inputs", "static_failure.cv"}, exit_code = 1,
            stderr_contains = {"CV-ASSERT", "1 == 2", "static mismatch"}},
        {args = {"interpret", "--explicit-inputs", "--tests", "loop_step_stop.cv"}, exit_code = 1,
            stdout_contains = {"body\nlater test\n"},
            stdout_not_contains = {"unreachable"},
            stderr_contains = {"step stopped", "tests: 1 passed; 1 failed"}},
        {args = {"interpret", "--explicit-inputs", "--tests", "fatal_test.cv"}, exit_code = 1,
            stderr_contains = {"module: fatal_test\n    name: fatal assertion", "assertion failed", "stop the run",
                "earlier failure retained", "earlier check retained", "note: execution aborted"},
            stderr_not_contains = {"unreachable", "carven: tests:"}},
    },
}

case_specs["commands/entry_failure"] = {
    fixtures = {
        ["explicit.cv.fixture"] = "explicit.cv",
        ["implicit.cv.fixture"] = "implicit.cv",
    },
    steps = {
        {args = {"interpret", "--explicit-inputs", "explicit.cv"}, exit_code = 1,
            stdout_contains = {"before"}, stderr = "explicit.interpret.txt"},
        {args = {"interpret", "--explicit-inputs", "implicit.cv"}, exit_code = 1,
            stderr = "implicit.interpret.txt"},
    },
}

case_specs["commands/runtime_traps"] = {
    inputs = {"divide_entry.cv", "index_entry.cv", "helper_entry.cv", "range_entry.cv", "lane_entry.cv"},
    fixtures = {
        ["divide.cv.fixture"] = "divide.cv",
        ["index.cv.fixture"] = "index.cv",
        ["slice.cv.fixture"] = "slice.cv",
        ["helper.cv.fixture"] = "helper.cv",
        ["range.cv.fixture"] = "range.cv",
        ["lane.cv.fixture"] = "lane.cv",
    },
    steps = {
        {args = {"interpret", "--explicit-inputs", "divide_entry.cv", "divide.cv"}, exit_code = 1,
            stderr_contains = {"division by zero", "divide.cv:2:17"}},
        {args = {"interpret", "--explicit-inputs", "index_entry.cv", "index.cv"}, exit_code = 1,
            stderr_contains = {"sequence index is out of bounds", "index.cv:2:16"}},
        {args = {"interpret", "--explicit-inputs", "--tests", "slice.cv"}, exit_code = 1,
            stderr_contains = {"sequence index is out of bounds", "slice.cv:2:12",
                "module: slice\n    name: slice bounds", "note: execution aborted"},
            stdout_not_contains = {"unreachable"}, stderr_not_contains = {"carven: tests:"}},
        {args = {"interpret", "--explicit-inputs", "helper_entry.cv", "helper.cv"}, exit_code = 1,
            stderr_contains = {"test operation requires an active test", "helper.cv:2:5"}},
        {args = {"interpret", "--explicit-inputs", "range_entry.cv", "range.cv"}, exit_code = 1,
            stderr_contains = {"slice range is out of bounds", "range.cv:2:12"}},
        {args = {"interpret", "--explicit-inputs", "lane_entry.cv", "lane.cv"}, exit_code = 1,
            stderr_contains = {"SIMD index or memory range is out of bounds", "lane.cv:4:12"}},
    },
}

case_specs["commands/test_order"] = {
    inputs = {"a.cv", "z.cv"},
    args = {"interpret", "--explicit-inputs", "--tests", "z.cv", "a.cv"},
    stdout_contains = {"a1\na2\nz\n"}, stderr_contains = {"tests: 3 passed; 0 failed"},
}

case_specs["commands/execution_modes"] = {
    inputs = {"input.cv"},
    steps = {},
}
for _, selection in ipairs({
    {mode = "interpret", tests = false},
    {mode = "interpret", tests = true},
    {mode = "compile", tests = false},
    {mode = "compile", tests = true},
}) do
    local mode, tests = selection.mode, selection.tests
    local args = {mode, "--explicit-inputs"}
    if tests then table.insert(args, mode == "compile" and "--tests=main" or "--tests") end
    if mode == "compile" then
        table.insert(args, "-o")
        table.insert(args, tests and "test-output" or "program-output")
    end
    table.insert(args, "input.cv")
    local output = {"compile-time test\n", "compile-time output: 3\n"}
    if mode ~= "compile" then
        table.insert(output, tests and "runtime test\n" or "runtime output: 2\n")
    end
    table.insert(case_specs["commands/execution_modes"].steps, {
        args = args,
        stdout_ordered = output,
        stdout_not_contains = mode == "compile" and {"runtime test", "runtime output"}
            or {tests and "runtime output" or "runtime test"},
        stderr_contains = mode ~= "compile" and tests and {"tests: 1 passed; 0 failed"} or nil,
    })
end

case_specs["commands/source_collection"] = {
    inputs = {"input.cv", "isolated.cv", "crafts/demo/value.cv", "explicit.cv", "external/crafts/json/parser.cv"},
    steps = {
        {args = {"interpret", "--explicit-inputs", "isolated.cv", "crafts/demo/value.cv"},
            stdout_contains = {"7\n"}},
        {args = {"check", "--explicit-inputs", "isolated.cv"}, exit_code = 1,
            stderr_contains = {"CV-IMPORT-RESOLUTION", "isolated.cv:1:"}},
        {args = {"interpret", "--explicit-inputs", "isolated.cv"}, exit_code = 1,
            stderr_contains = {"CV-IMPORT-RESOLUTION", "isolated.cv:1:"}},
        {installed_toolchain = true, args = {"interpret", "explicit.cv"}, absolute_inputs = {"external/crafts/json/parser.cv"},
            stdout_contains = {"42\n"}},
        {installed_toolchain = true, args = {"compile", "input.cv", "crafts/demo/value.cv", "-o", "emit"},
            output_files = {"emit/input.cpp", "emit/crafts/demo/value.cpp", "emit/crafts/carven/std/utf/scalar.cpp"}},
        {args = {"compile", "input.cv", "-o", "installed"}, installed_toolchain = true,
            installed_inputs = {"carven/std/utf/scalar.cv"},
            output_files = {"installed/input.cpp", "installed/crafts/carven/std/utf/scalar.cpp"}},
        {installed_toolchain = true, args = {"interpret", "input.cv"}, stdout_contains = {"65 7\n"}},
    },
}

local cli_dir = path.join(os.projectdir(), "tests", "cli")

local native_specs = {
    {
        name = "runtime",
        case_dir = "commands/runtime_traps",
        sources = {
            ["commands/runtime_traps/main.cv"] = "main.cv",
            ["commands/runtime_traps/divide.cv.fixture"] = "divide.cv",
            ["commands/runtime_traps/index.cv.fixture"] = "index.cv",
            ["commands/runtime_traps/helper.cv.fixture"] = "helper.cv",
            ["commands/runtime_traps/range.cv.fixture"] = "range.cv",
            ["commands/runtime_traps/lane.cv.fixture"] = "lane.cv",
            ["commands/assertions/failure.cv"] = "failure.cv",
        },
        scenarios = {
            divide = {args = {"divide"}, exit_code = 86, stderr = "divide.txt"},
            index = {args = {"index"}, exit_code = 86, stderr = "index.txt"},
            helper = {args = {"helper"}, exit_code = 86, stderr = "helper.txt"},
            range = {args = {"range"}, exit_code = 86, stderr = "range.txt"},
            lane = {args = {"lane"}, exit_code = 86, stderr = "lane.txt"},
            failure = {args = {"failure"}, exit_code = 86,
                stderr_contains = {"message evaluated", "assertion failed", "actual == expected",
                    "actual: [\n        1,\n        11,\n    ]", "expected: [\n        2,\n        22,\n    ]",
                    "array mismatch", "failure.cv:", "note: execution aborted"},
                stderr_not_contains = {"\n        99,", "unreachable"}},
        },
    },
    {
        name = "stops",
        tests = "main",
        case_dir = "commands/assertions",
        sources = {
            ["commands/assertions/loop_step_stop.cv"] = "loop_step_stop.cv",
            ["commands/assertions/evaluation_stop.cv"] = "evaluation_stop.cv",
        },
        scenarios = {
            stops = {args = {}, exit_code = 1,
                stdout_contains = {"later test\nbody\nlater test\n"},
                stdout_not_contains = {"unreachable"},
                stderr_contains = {"step stopped", "evaluation stopped", "tests: 2 passed; 4 failed",
                    "module: loop_step_stop", "module: evaluation_stop"}},
        },
    },
    {
        name = "slice",
        tests = "main",
        case_dir = "commands/runtime_traps",
        sources = {
            ["commands/runtime_traps/slice.cv.fixture"] = "slice.cv",
            ["commands/assertions/abort.cv"] = "abort.cv",
        },
        scenarios = {
            slice = {args = {}, exit_code = 86, stderr = "slice.txt"},
        },
    },
    {
        name = "fatal",
        tests = "main",
        case_dir = "commands/assertions",
        sources = {
            ["commands/assertions/fatal_test.cv"] = "fatal_test.cv",
            ["commands/assertions/abort.cv"] = "abort.cv",
        },
        scenarios = {
            fatal = {args = {}, exit_code = 86,
                stderr_contains = {"module: fatal_test\n    name: fatal assertion", "assertion failed", "stop the run",
                    "earlier failure retained", "earlier check retained", "note: execution aborted"},
                stderr_not_contains = {"unreachable", "carven: tests:"}},
        },
    },
}

for _, spec in ipairs(native_specs) do
    -- This fixed source root gives fixtures valid, stable Carven module identities.
    -- The package rule still places generated C++ and objects in the configured builddir.
    local source_prefix = "build/cli_fixtures/" .. spec.name .. "/"
    local source_root = path.join(os.projectdir(), source_prefix)
    target("carven-test-cli-" .. spec.name)
        set_default(false)
        set_kind("binary")
        add_rules("@carven/carven", {tests = spec.tests})
        set_languages("c++20")
        if spec.name == "runtime" then
            add_defines("NDEBUG")
            add_includedirs(path.join(cli_dir, "commands", "runtime_traps"))
        end
        on_load(function (target)
            for source, destination in table.orderpairs(spec.sources) do
                local prepared_source = path.join(source_root, destination)
                os.mkdir(path.directory(prepared_source))
                os.cp(path.join(cli_dir, source), prepared_source, {copy_if_different = true})
                target:add("files", prepared_source)
            end
        end)
        for _, name in ipairs(table.orderkeys(spec.scenarios)) do
            add_tests(name, {group = "cli", run_timeout = 30000})
        end
        on_test(function (target, opt)
            local harness = import("harness", {rootdir = cli_dir, anonymous = true})
            return harness(target, opt, spec.scenarios, {
                case_dir = path.join(cli_dir, spec.case_dir),
                source_prefix = source_prefix,
                module_prefix = source_prefix:gsub("/", "."),
            })
        end)
    target_end()
end

target("carven-test-cli")
    set_default(false)
    set_kind("phony")
    add_deps("carven", {inherit = false})

    for _, case_name in ipairs(table.orderkeys(case_specs)) do
        add_tests(case_name, {group = "cli"})
    end

    on_test(function (target, opt)
        local harness = import("harness", {
            rootdir = path.join(os.projectdir(), "tests", "cli"),
            anonymous = true,
        })
        return harness(target, opt, case_specs)
    end)
target_end()
