local case_specs = {
    ["commands/collections"] = {
        case_dir = "../crafts/carven/std/collections",
        inputs = {"containers.cv"}, args = {"interpret", "--tests", "containers.cv"},
        stderr_contains = {"tests: 1 passed; 0 failed"},
    },
    ["commands/fs"] = {
        case_dir = "../crafts/fs",
        inputs = {"bridge.cv"}, args = {"interpret", "--tests", "bridge.cv"},
        stdout_contains = {"fs: bytes, text, path kinds, errors\n"},
        stderr_contains = {"tests: 1 passed; 0 failed"},
    },
    ["commands/toml"] = {
        case_dir = "../crafts/carven/std/toml",
        inputs = {"smoke.cv"}, args = {"interpret", "--tests", "smoke.cv"},
        stderr_contains = {"tests:", "0 failed"},
    },
    ["commands/host"] = {
        inputs = {"stages.cv", "bytes.cv", "recovery.cv", "limit.cv", "module_witness.cv", "cold.cv", "timers.cv", "pieces.cv", "budget.cv", "witness.cv"},
        cases = {
            budget = {args = {"check", "budget.cv"}, stdin_bytes = string.rep("x", 1024 * 1024 + 1), exit_code = 1,
                stderr_contains = {"CV-CONST-LIMIT", "execution value size limit"}},
            witness = {args = {"check", "witness.cv"}, stdin = "witness.txt", exit_code = 1,
                stderr_contains = {"type dependency query"}},
            stages = {args = {"interpret", "stages.cv"}, stdin_bytes = "first\r\nsecond\nthird\n", stdout_contains = {"static first second\nruntime third\n"}},
            bytes = {args = {"check", "bytes.cv"}, stdin = "bytes.txt", stdout_contains = {"bytes\n"}, stderr_contains = {"check passed"}},
            recovery = {args = {"check", "recovery.cv"}, stdin = "recovery.txt", stdout_contains = {"recovered\n"}, stderr_contains = {"check passed"}},
            limit = {args = {"check", "limit.cv"}, stdin_bytes = "abcd\r\n", stdout_contains = {"limit\n"}, stderr_contains = {"check passed"}},
            ["module-witness"] = {args = {"interpret", "module_witness.cv"}, stdin = "witness.txt", stdout_contains = {"1 1\n"}},
            cold = {args = {"check", "cold.cv"}, stdin = "witness.txt", stdout_contains = {"cold\n"}, stderr_contains = {"check passed"}},
            timers = {args = {"interpret", "timers.cv"}, stdout_contains = {"runtime timers\n"}},
            pieces = {args = {"interpret", "pieces.cv"}, stdin = "pieces.txt", stdout_contains = {"static pieces\nruntime pieces\n"}},
        },
    },
    ["commands/standard_capabilities"] = {
        case_dir = "commands/host",
        inputs = {"standard.cv", "native_constant.cv", "filesystem_witness.cv"},
        steps = {
            {args = {"check", "native_constant.cv"}, exit_code = 1,
                stderr_contains = {"CV-CONST-INITIALIZER"}},
            {args = {"check", "filesystem_witness.cv"}, exit_code = 1,
                stderr_contains = {"CV-CONST-ADMISSION", "type dependency query"}},
            {args = {"interpret", "standard.cv"},
                stdout_contains = {"static standard capabilities\ninterpreted standard capabilities\n"},
                output_files = {"observed.txt", "generated.cv", "emitted/generated.cpp"}},
        },
    },
    ["commands/provider"] = {
        inputs = {"input.cv", "input/provider/Result.cv", "private_only.cv"},
        args = {"compile", "-o", "emit", "input.cv", "input/provider/Result.cv", "private_only.cv"},
        output_files = {"emit/carven/providers/input.hpp", "emit/carven/providers/input/provider/Result.hpp", "emit/carven/providers/private_only.hpp", "emit/carven/manifest.json"},
    },
    ["commands/manifest"] = {
        fixtures = {["invalid.cv.fixture"] = "invalid.cv"},
        args = {"compile", "-o", "failed", "invalid.cv"}, exit_code = 1,
        stderr_contains = {"CV-SYNTAX", "expected parameter name", "invalid.cv:1:12"},
        absent_files = {"failed"},
    },
    ["commands/artifact_reuse"] = {
        case_dir = "commands/manifest",
        inputs = {"input.cv"},
        steps = {
            {args = {"check", "input.cv", "-o", "program"}, stderr = "../check/passed.txt",
                output_files = {"program/carven/manifest.json", "program/carven/semantic"},
                absent_files = {"program/**.cpp", "program/**.hpp"}},
            {args = {"compile", "program", "-o", "ordinary"},
                output_files = {"ordinary/input.cpp", "ordinary/carven/api/input.hpp", "ordinary/carven/manifest.json"},
                absent_files = {"ordinary/carven/generated/carven-test-main.cpp", "ordinary/carven/semantic"}},
            {args = {"compile", "program", "--tests", "-o", "tests"},
                output_files = {"tests/input.cpp", "tests/carven/api/input.hpp",
                    "tests/carven/generated/carven-test-main.cpp", "tests/carven/manifest.json"},
                absent_files = {"tests/carven/semantic"}},
            {args = {"compile", "program", "--stdout"},
                stdout_contains = {"==> input.cpp <==", "==> carven/api/input.hpp <=="},
                stdout_not_contains = {"==> crafts/carven/", "==> carven/semantic <=="}},
        },
    },
    ["commands/stdout_selection"] = {
        inputs = {"input.cv", "crafts/demo/helper.cv", "crafts/demo/unrelated.cv",
            "crafts/cycle/a.cv", "crafts/cycle/b.cv"},
        steps = {
            {args = {"compile", "input.cv", "--stdout"}, exit_code = 1,
                stderr_contains = {"demo"}},
            {args = {"compile", "input.cv", "crafts/demo/helper.cv", "--stdout"},
                stdout_contains = {"==> input.cpp <==", "==> crafts/demo/helper.cpp <==", "/crafts/demo/helper.hpp <=="},
                stdout_not_contains = {"unrelated", "==> crafts/carven/"}},
            {args = {"compile", "crafts/cycle/b.cv", "crafts/cycle/a.cv", "--stdout"},
                stdout_contains = {"==> crafts/cycle/b.cpp <==", "==> crafts/cycle/a.cpp <=="},
                stdout_not_contains = {"==> input.cpp <=="}},
            {args = {"compile", "input.cv", "crafts/demo/helper.cv", "crafts/demo/unrelated.cv", "-o", "emit"},
                output_files = {"emit/input.cpp", "emit/crafts/demo/helper.cpp", "emit/crafts/demo/unrelated.cpp"}},
        },
    },
    ["commands/pointer_display/addresses"] = {
        case_dir = "commands/pointer_display",
        inputs = {"input.cv"},
        args = {"interpret", "input.cv"},
        stdout_ordered = {"local=const@nonnull", "cstring=const@nonnull", "123", "null=const@null",
            "local=interp@object#", "cstring=interp@object#", "123", "null=interp@null"},
        stdout_not_contains = {"0x"},
    },
    ["commands/cstring_text"] = {
        inputs = {"input.cv"},
        args = {"interpret", "input.cv"}, stdout = "stdout.txt", stderr = "stderr.txt",
    },
    ["commands/check"] = {
        inputs = {"input.cv", "crafts/demo/dependency.cv"},
        fixtures = {
            ["../../diagnostics/warning/input.cv"] = "warning.cv",
        },
        steps = {
            {args = {"check", "warning.cv"}, stderr_ordered = {"CV-LINT-UNUSED-LOCAL", "carven: check passed\n"}},
            {
                -- Check the compiler's Crafts together with project Crafts.
                args = {"check", "input.cv", "crafts/demo/dependency.cv"},
                stdout = "stdout.txt", stderr = "stderr.txt",
                absent_files = {"input.cpp", "crafts/demo/dependency.cpp", "warning.cpp", "carven", "program", "program.exe"},
            },
        },
    },
    ["commands/timings"] = {
        inputs = {"input.cv"},
        steps = {
            {
                args = {"check", "--timings", "input.cv", "-o", "checked"}, stdout = "compile.txt",
                stderr_contains = {"carven: check passed in ", "Semantic analysis"},
                stderr_not_contains = {"C++ generation", "Execution"},
            },
            {
                args = {"compile", "--timings", "checked", "-o", "generated"},
                stderr_contains = {"carven: compilation finished in ", "C++ generation"},
                stderr_not_contains = {"Semantic analysis"},
                stdout_not_contains = {"prepared", "runtime"},
            },
            {
                args = {"compile", "input.cv", "--stdout", "--timings"},
                stdout_contains = {"==> input.cpp <=="},
                stdout_not_contains = {"carven: compilation finished in "},
                stderr_contains = {"carven: compilation finished in ", "C++ generation"},
            },
            {
                args = {"interpret", "--timings", "--trace", "input.cv"}, stdout = "run.txt",
                stderr_ordered = {"statement", "carven: interpretation finished in ", "Execution"},
                stderr_not_contains = {"C++ generation", "Native compilation"},
            },
        },
    },
    ["commands/const_blocks"] = {
        inputs = {"input.cv", "helper.cv"},
        steps = {
            {args = {"check", "input.cv", "helper.cv"}, stderr = "../check/passed.txt", stdout = "stdout.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n"}},
            {args = {"interpret", "input.cv", "helper.cv"}, stdout = "run.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n", "runtime\nruntime\n"}},
        },
    },
    ["commands/static_execution"] = {
        inputs = {"input.cv"},
        steps = {
            {args = {"compile", "input.cv", "-o", "emit"}, stdout = "stdout.txt", stderr = "stderr.txt"},
            {args = {"compile", "input.cv", "--stdout"}, stderr = "combined.txt", stdout_contains = {"==> input.cpp <=="}},
        },
    },
    ["commands/dump"] = {
        inputs = {"input.cv"},
        steps = {
            {args = {"dump", "input.cv"}, stdout_ordered = {"Tokens \"input.cv\"", "SourceModule"}},
            {args = {"dump", "tokens", "input.cv", "--timings"}, stdout_contains = {"Tokens \"input.cv\""},
                stdout_not_contains = {"SourceModule", "carven: dump finished in "},
                stderr_contains = {"carven: dump finished in ", "Lexing"},
                stderr_not_contains = {"Parsing", "Semantic analysis"}},
            {args = {"dump", "ast", "input.cv"}, stdout_contains = {"SourceModule"},
                stdout_not_contains = {"Tokens \""}},
        },
    },
    ["diagnostics/warning"] = {
        inputs = {"input.cv"},
        args = {"compile", "input.cv"},
        stderr_contains = {"CV-LINT-UNUSED-LOCAL", "input.cv"},
        output_files = {"input.cpp"},
    },
    ["output/default"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"compile", "bare_structure.cv"},
        output_files = {"bare_structure.cpp", "carven/generated/*/bare_structure.hpp"},
        file_not_contains = {["carven/manifest.json"] = {"crafts.carven.std"}},
    },
    ["output/destination"] = {
        fixtures = {
            ["../fixtures/bare_structure.cv"] = "bare_structure.cv",
            ["../state/preexisting.fixture"] = "emit/bare_structure.cpp",
            ["../state/stale.fixture"] = "emit/stale.txt",
        },
        args = {"compile", "-o", "emit", "bare_structure.cv"},
        output_files = {
            "emit/bare_structure.cpp",
            "emit/carven/generated/*/bare_structure.hpp",
            "emit/stale.txt",
        },
        file_not_contains = {
            ["emit/bare_structure.cpp"] = {"preexisting output"},
        },
    },
    ["output/stdout"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"compile", "bare_structure.cv", "--stdout"},
        stdout_ordered = {
            "==> bare_structure.cpp <==",
            "/bare_structure.hpp <==",
        },
        absent_files = {
            "**.cpp",
            "**.hpp",
        },
    },
    ["module_layout/spec"] = {
        project = "../project",
        args = {
            "compile",
            "-o", "emit",
            "main.cv",
            "sibling.cv",
            "nested/worker.cv",
            "nested/local.cv",
            "./nested/local.cv",
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
            "emit/carven/generated/*/import/export.hpp",
            "emit/carven/generated/*/main.hpp",
            "emit/carven/generated/*/sibling.hpp",
            "emit/carven/generated/*/nested/worker.hpp",
            "emit/carven/generated/*/nested/local.hpp",
            "emit/carven/generated/*/__internal.hpp",
        },
    },
}

if not is_plat("windows", "mingw") then
    case_specs["commands/native_async"] = {
        case_dir = "../crafts/async", inputs = {"operations.cv"},
        args = {"interpret", "operations.cv"},
        stdout_contains = {"native async: stdout, channel, cancellation\n"},
    }
end

local native_execution = {
    native_operation = {
        args = {"interpret", "native.cv", "crafts/demo/api.cv"}, exit_code = 1, stdout = "source_stdout.txt",
        stderr_contains = {"CV-INTERPRET-ADMISSION", "native C++ function requires compiled execution",
            "crafts/demo/api.cv:", "native.cv:"},
    },
    interpret = {
        args = {"interpret", "source.cv", "crafts/demo/api.cv"}, stdout = "source_stdout.txt",
    },
    tests = {
        args = {"interpret", "--tests", "source.cv", "crafts/demo/api.cv"},
        stderr_contains = {"tests: 1 passed; 0 failed"},
    },
    compile = {
        args = {"compile", "source.cv", "crafts/demo/api.cv", "-o", "emit"},
        output_files = {"emit/source.cpp", "emit/crafts/demo/api.cpp"},
        absent_files = {"emit/crafts/carven/std/io.cpp"},
    },
}

-- Use the real bundled Standard and Runtime Crafts.
for name, scenario in pairs(native_execution) do
    scenario.case_dir = "commands/native_execution"
    scenario.inputs = {"source.cv", "native.cv", "crafts/demo/api.cv", "crafts/demo/native.hpp"}
    case_specs["commands/native_execution/" .. name] = scenario
end

local interpretation = {
    fixtures = {
        ["../../../language/functions/interpreted_runtime.cv"] = "shared.cv",
        ["../../../language/types/pointer_graph.cv"] = "local_graph.cv",
        ["../../../language/types/pointer_delivery.cv"] = "pointer_delivery.cv",
        ["../../../language/text/backing_identity.cv"] = "text_backing.cv",
        ["../../../language/async/scheduling.cv"] = "async_scheduling.cv",
        ["../../../language/async/waiting.cv"] = "async_waiting.cv",
        ["../../../language/async/composition.cv"] = "async_composition.cv",
        ["../../../language/entry/async_cancelled.cv"] = "async_cancelled.cv",
        ["../../../language/entry/async_failure.cv"] = "async_failure.cv",
    },
    inputs = {
        "input.cv", "implicit_recovery.cv", "wrapping.cv",
        "declarations.cv", "main.cv", "floating.cv", "typed_failures.cv",
        "async_waiting_entry.cv", "async_composition_entry.cv", "async_scheduling_entry.cv",
        "failure.cv", "limit.cv", "static_only.cv", "static_failure.cv",
    },
    cases = {
        ["missing-input"] = {args = {"interpret"}, exit_code = 1, stderr_contains = {"requires at least one source file", "carven interpret --help"}},
        ["async-failure"] = {args = {"interpret", "async_failure.cv"}, exit_code = 1,
            stderr_ordered = {"root failure selected", "child cancellation requested: true", "child closed",
                "CV-INTERPRET-EXECUTION", "typed failure escaped execution without recovery"}},
        ["async-cancellation"] = {args = {"interpret", "async_cancelled.cv"}, exit_code = 1,
            stderr_ordered = {"root requested child cancellation", "child accepts cancellation: true",
                "CV-INTERPRET-EXECUTION", "cancellation escaped execution without a consumer"},
            stderr_not_contains = {"unreachable child", "unreachable root"}},
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
        ["division-failure"] = {
            args = {"interpret", "failure.cv"}, exit_code = 1,
            stdout = "failure.txt",
            stderr_ordered = {
                "failure.cv:1:28: error: division by zero in expression",
                "  called from:", "failure.cv:4:9",
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
        ["async-waiting"] = {args = {"interpret", "async_waiting_entry.cv", "async_waiting.cv"}},
        ["async-composition"] = {args = {"interpret", "async_composition_entry.cv", "async_composition.cv"}},
        ["shared-runtime"] = {args = {"interpret", "--tests", "shared.cv"}, stdout_contains = {"shared runtime test\n"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["pointer-graph"] = {args = {"interpret", "--tests", "local_graph.cv"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["pointer-delivery"] = {args = {"interpret", "--tests", "pointer_delivery.cv"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["text-backing"] = {args = {"interpret", "--tests", "text_backing.cv"},
            stderr_contains = {"tests: 1 passed; 0 failed"}},
        ["async-scheduling"] = {args = {"interpret", "async_scheduling_entry.cv", "async_scheduling.cv"},
            stderr_contains = {"CV-LINT-UNUSED-LOCAL", "unused local binding"}},
        ["typed-failures"] = {
            args = {"interpret", "typed_failures.cv"},
            stdout = "typed_failures.txt",
        },
        ["implicit-recovery"] = {args = {"interpret", "implicit_recovery.cv"}},
        ["floating"] = {
            args = {"interpret", "floating.cv"},
            stdout = "floating.txt",
        },
        ["explicit-entry"] = {
            args = {"interpret", "main.cv", "declarations.cv"},
            stdout = "main.txt",
        },
        ["multiple-entries"] = {
            args = {"interpret", "main.cv", "input.cv"}, exit_code = 1,
            stdout_contains = {"compile 21"},
            stderr_contains = {"CV-ENTRY-DUPLICATE", "main.cv", "input.cv"},
            stdout_not_contains = {"explicit main", "result 42"},
        },
        ["arguments"] = {
            args = {"interpret", "--trace", "input.cv", "--", "--help"},
            stdout = "stdout.txt",
            stderr_contains = {"call main", "call fib", "return fib", "statement"},
            absent_files = {"input.cpp", "program", "program.exe"},
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
    case_specs["commands/interpretation/" .. name] = spec
end

case_specs["commands/test_options"] = {
    fixtures = {["../interpretation/declarations.cv"] = "declarations.cv"},
    args = {"compile", "--tests", "declarations.cv", "-o", "empty-tests"},
    output_files = {"empty-tests/carven/generated/carven-test-main.cpp", "empty-tests/carven/manifest.json"},
}

case_specs["commands/assertions/passing"] = {
    case_dir = "commands/assertions",
    fixtures = {
        ["../../../language/testing/assertions.cv"] = "input.cv",
    },
    args = {"interpret", "--tests", "input.cv"},
    stderr_contains = {"tests: 1 passed; 0 failed"},
}

case_specs["commands/test_order"] = {
    inputs = {"a.cv", "z.cv"},
    args = {"interpret", "--tests", "z.cv", "a.cv"},
    stdout_contains = {"a1\na2\nz\n"}, stderr_contains = {"tests: 3 passed; 0 failed"},
}

case_specs["commands/execution_modes"] = {
    inputs = {"input.cv"},
    steps = {
        {
            args = {"interpret", "input.cv"},
            stdout_ordered = {"compile-time test\n", "compile-time output: 3\n", "runtime output: 2\n"},
            stdout_not_contains = {"runtime test"},
        },
        {
            args = {"interpret", "--tests", "input.cv"},
            stdout_ordered = {"compile-time test\n", "compile-time output: 3\n", "runtime test\n"},
            stdout_not_contains = {"runtime output"},
            stderr_contains = {"tests: 1 passed; 0 failed"},
        },
        {
            args = {"compile", "input.cv", "-o", "program-output"},
            stdout_ordered = {"compile-time test\n", "compile-time output: 3\n"},
            stdout_not_contains = {"runtime test", "runtime output"},
            output_files = {"program-output/input.cpp", "program-output/carven/manifest.json"},
            absent_files = {"program-output/carven/generated/carven-test-runner.hpp",
                "program-output/carven/generated/carven-test-main.cpp"},
        },
        {
            args = {"compile", "--tests", "input.cv", "-o", "test-output"},
            stdout_ordered = {"compile-time test\n", "compile-time output: 3\n"},
            stdout_not_contains = {"runtime test", "runtime output"},
            output_files = {"test-output/input.cpp", "test-output/carven/generated/carven-test-runner.hpp",
                "test-output/carven/generated/carven-test-main.cpp"},
        },
        {
            args = {"compile", "--tests=runner", "input.cv", "-o", "runner-output"},
            stdout_ordered = {"compile-time test\n", "compile-time output: 3\n"},
            stdout_not_contains = {"runtime test", "runtime output"},
            output_files = {"runner-output/input.cpp", "runner-output/carven/generated/carven-test-runner.hpp"},
            absent_files = {"runner-output/carven/generated/carven-test-main.cpp"},
        },
    },
}

case_specs["commands/source_collection"] = {
    inputs = {"input.cv", "isolated.cv", "crafts/demo/value.cv", "explicit.cv"},
    fixtures = {["external/crafts/json/parser.cv"] = "crafts/json/parser.cv"},
    steps = {
        {args = {"interpret", "isolated.cv"}, exit_code = 1,
            stderr_contains = {"demo"}},
        {args = {"interpret", "isolated.cv", "crafts/demo/value.cv"},
            stdout_contains = {"7\n"}},
        {args = {"interpret", "explicit.cv"}, absolute_inputs = {"crafts/json/parser.cv"},
            stdout_contains = {"42\n"}},
        {args = {"compile", "input.cv", "crafts/demo/value.cv", "-o", "emit"},
            output_files = {"emit/input.cpp", "emit/crafts/demo/value.cpp", "emit/crafts/carven/std/time.cpp"},
            file_not_contains = {["emit/carven/manifest.json"] = {"crafts.carven.std.json", "crafts.carven.std.net"}}},
        {args = {"check", "input.cv", "crafts/demo/value.cv", "-o", "checked"},
            stderr_contains = {"carven: check passed"},
            output_files = {"checked/carven/semantic"}},
        {args = {"compile", "checked", "--tests", "-o", "restored"},
            output_files = {"restored/input.cpp", "restored/crafts/demo/value.cpp",
                "restored/crafts/carven/std/time.cpp"},
            file_not_contains = {["restored/carven/manifest.json"] = {'"external": true'}}},
        {args = {"interpret", "input.cv", "crafts/demo/value.cv"}, stdout_contains = {"65 7\n"}},
    },
}

case_specs["commands/pointer_display/unavailable"] = {
    case_dir = "commands/pointer_display",
    inputs = {"unavailable.cv"},
    args = {"interpret", "unavailable.cv"}, exit_code = 1,
    stdout_contains = {"element=const@nonnull"},
    stderr_contains = {"CV-INTERPRET-EXECUTION", "pointer address representation is not known during execution"},
}

case_specs["input/invalid_extension"] = {
    args = {"compile", "unknown"},
    exit_code = 1,
    stderr = "stderr.txt",
}

case_specs["input/missing"] = {
    args = {"compile", "missing.cv"},
    exit_code = 1,
    stderr_contains = {"cannot read source file", "missing.cv"},
}

case_specs["output/sink_failure"] = {
    fixtures = {
        ["../fixtures/bare_structure.cv"] = "bare_structure.cv",
        ["../state/preexisting.fixture"] = "blocked",
    },
    args = {"compile", "bare_structure.cv", "-o", "blocked"},
    exit_code = 1,
    stderr_contains = {"carven: error: cannot create directory 'blocked':"},
    output_files = {"blocked"},
    absent_files = {
        "bare_structure.cpp",
        "carven/generated/*/bare_structure.hpp",
    },
    file_contents = {
        ["blocked"] = "preexisting output\n",
    },
}

case_specs["invocation/invalid_option"] = {
    args = {"compile", "--unknown=世界 🌍", "input.cv"},
    exit_code = 1,
    stderr_contains = {"unknown option '--unknown=世界 🌍'", "carven compile --help"},
}

if not is_plat("windows") then
    case_specs["invocation/invalid_utf8"] = {
        args = {string.char(255)}, exit_code = 1,
        stderr_contains = {"process argument is not valid UTF-8"},
    }
end

case_specs["commands/test_report"] = {
    fixtures = {["input.cv.fixture"] = "input.cv"},
    args = {"interpret", "--tests", "input.cv"}, exit_code = 1,
    stdout = "stdout.txt", stderr_ordered = {
        "before failures", "input.cv:11:5: error: check failed", "name: failed",
        "condition: actual == expected", "actual: [\n        1,\n        2,", "expected: [\n        1,\n        3,",
        "first line\n    second line", "between failures", "input.cv:14:5: error: check failed",
        "CLI failure", "input.cv:24:5: error: check failed", ": <not evaluated>", "second failure",
        "after failures", "input.cv:39:5: error: requirement failed", "required condition", "test stopped",
        "input.cv:43:5: error: explicit failure", "explicit\n      failure", "message: \"\"",
        "input.cv:53:5: error: check failed", "input.cv:52:1", "first anonymous failure",
        "input.cv:57:5: error: check failed", "input.cv:56:1", "second anonymous failure",
        "input.cv:61:5: error: check failed", "name: \"\"", "explicit empty label", "tests: 1 passed; 6 failed",
    },
}

case_specs["commands/anonymous_tests"] = {
    inputs = {"duplicate_label.cv", "reserved_label.cv"},
    steps = {
        {args = {"check", "duplicate_label.cv"}, exit_code = 1,
            stderr_contains = {"CV-TEST-DUPLICATE-NAME", "duplicate_label.cv:4:6"}},
        {args = {"check", "reserved_label.cv"}, exit_code = 1,
            stderr_contains = {"CV-TEST-MAIN-NAME", "reserved_label.cv:2:6"}},
    },
}

case_specs["commands/entry_failure"] = {
    fixtures = {
        ["explicit.cv.fixture"] = "explicit.cv",
        ["implicit.cv.fixture"] = "implicit.cv",
    },
    steps = {
        {args = {"interpret", "explicit.cv"}, exit_code = 1,
            stdout_contains = {"before"}, stderr_ordered = {
                "CV-INTERPRET-EXECUTION", "typed failure escaped execution without recovery",
                "Denied {", "code: 7", "explicit.cv:5:19", "throw Denied", "load(7)?", "while interpreting this function call"}},
        {args = {"interpret", "implicit.cv"}, exit_code = 1,
            stderr_ordered = {"CV-INTERPRET-EXECUTION", "failure: Missing {}", "implicit.cv:3:27",
                "throw Missing", "load()?", "while interpreting this function call"}},
    },
}

local assertions = {
    inputs = {"failure.cv", "failure_entry.cv", "static_failure.cv", "fatal_test.cv", "loop_step_stop.cv"},
    cases = {
        ["failure"] = {args = {"interpret", "failure_entry.cv", "failure.cv"}, exit_code = 1,
            stderr_contains = {"message evaluated", "assertion failed", "actual == expected",
                "actual: [\n        1,\n        11,\n    ]", "expected: [\n        2,\n        22,\n    ]",
                "array mismatch", "failure.cv:",
                "  called from:", "failure.cv:18:5", "failure_entry.cv:3:1", "  note: execution aborted"},
            stderr_not_contains = {"\n        99,", "unreachable"}},
        ["static-failure"] = {args = {"check", "static_failure.cv"}, exit_code = 1,
            stderr_contains = {"CV-ASSERT", "1 == 2", "static mismatch"}},
        ["loop-step-stop"] = {args = {"interpret", "--tests", "loop_step_stop.cv"}, exit_code = 1,
            stdout_contains = {"body\nlater test\n"},
            stdout_not_contains = {"unreachable"},
            stderr_contains = {"step stopped", "tests: 1 passed; 1 failed"}},
        ["fatal"] = {args = {"interpret", "--tests", "fatal_test.cv"}, exit_code = 1,
            stderr_contains = {"module: fatal_test\n    name: fatal assertion", "assertion failed", "stop the run",
                "earlier failure retained", "earlier check retained", "note: execution aborted"},
            stderr_not_contains = {"unreachable", "carven: tests:"}},
    },
}
for name, spec in pairs(assertions.cases) do
    spec.case_dir = "commands/assertions"
    spec.inputs = assertions.inputs
    case_specs["commands/assertions/" .. name] = spec
end

local runtime_traps = {
    inputs = {"index_entry.cv", "helper_entry.cv", "range_entry.cv", "lane_entry.cv"},
    fixtures = {
        ["index.cv.fixture"] = "index.cv",
        ["slice.cv.fixture"] = "slice.cv",
        ["helper.cv.fixture"] = "helper.cv",
        ["range.cv.fixture"] = "range.cv",
        ["lane.cv.fixture"] = "lane.cv",
    },
    cases = {
        ["index"] = {args = {"interpret", "index_entry.cv", "index.cv"}, exit_code = 1,
            stderr_contains = {"sequence index is out of bounds", "index.cv:2:16"}},
        ["slice"] = {args = {"interpret", "--tests", "slice.cv"}, exit_code = 1,
            stderr_contains = {"sequence index is out of bounds", "slice.cv:2:12",
                "module: slice\n    name: slice bounds", "note: execution aborted"},
            stdout_not_contains = {"unreachable"}, stderr_not_contains = {"carven: tests:"}},
        ["helper"] = {args = {"interpret", "helper_entry.cv", "helper.cv"}, exit_code = 1,
            stderr_contains = {"test operation requires an active test", "helper.cv:2:5"}},
        ["range"] = {args = {"interpret", "range_entry.cv", "range.cv"}, exit_code = 1,
            stderr_contains = {"slice range is out of bounds", "range.cv:2:12"}},
        ["lane"] = {args = {"interpret", "lane_entry.cv", "lane.cv"}, exit_code = 1,
            stderr_contains = {"SIMD index or memory range is out of bounds", "lane.cv:2:12"}},
    },
}
for name, spec in pairs(runtime_traps.cases) do
    spec.case_dir = "commands/runtime_traps"
    spec.inputs = runtime_traps.inputs
    spec.fixtures = runtime_traps.fixtures
    case_specs["commands/runtime_traps/" .. name] = spec
end

local host = case_specs["commands/host"]
case_specs["commands/host"] = nil
for name, scenario in pairs(host.cases) do
    scenario.case_dir = "commands/host"
    scenario.inputs = host.inputs
    case_specs["commands/host/" .. name] = scenario
end

local help = {
    main = {"Usage:", "carven build", "carven run", "Commands:"},
    build = {"carven build", "outputs under build/"},
    run = {"carven run [<source.cv>|<target>", "-- <arguments>"},
    compile = {"carven compile [options]", "-o <dir>", "--tests"},
    check = {"carven check [options]", "-o <dir>"},
    interpret = {"carven interpret [options]", "--max-steps"},
    dump = {"carven dump [kind]", "tokens", "ast"},
}

target("carven")
    for command in table.orderpairs(help) do
        add_tests("invocation/help/" .. command, {
            group = "cli", run_timeout = 30000,
            runargs = command == "main" and {"--help"} or {command, "--help"},
        })
    end
    add_tests("invocation/version", {
        group = "cli", run_timeout = 30000, runargs = {"--version"}, plain = true,
        pass_output_files = {path.join(os.scriptdir(), "invocation", "version", "stdout.txt")},
    })
    for name, sources in table.orderpairs({
        math = {"input.cv"}, number_parse = {"input.cv"}, structural_display = {"input.cv", "helper.cv"},
        default_initialization = {"input.cv"},
    }) do
        local directory = path.join(os.scriptdir(), "commands", name)
        add_tests("commands/" .. name, {
            group = "cli", run_timeout = 30000, rundir = directory,
            runargs = table.join({"interpret"}, sources), plain = true,
            pass_output_files = {path.join(directory, "stdout.txt")},
        })
    end
    after_test(function (target, opt)
        assert(not opt.stderr or opt.stderr == "", opt.stderr)
        local command = opt.name:match("/help/(.+)$")
        local stdout = opt.stdout or ""
        for _, fragment in ipairs(help[command] or {}) do
            assert(stdout:find(fragment, 1, true), "missing help text: " .. fragment)
        end
    end)
target_end()

target("carven-test-cli")
    set_default(false)
    set_group("cli")
    set_kind("phony")
    add_deps("carven", {inherit = false})

    for _, case_name in ipairs(table.orderkeys(case_specs)) do
        add_tests(case_name)
    end

    on_test(function (target, opt)
        local name = opt.name:match("^[^/]+/(.+)$")
        local spec = case_specs[name]
        if name == "commands/standard_capabilities" then
            spec.steps[3].envs = {
                CARVEN_TEST_COMPILER = path.absolute(target:dep("carven"):targetfile(), os.projectdir()),
            }
        end
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target:dep("carven"), opt, spec,
                path.join(os.projectdir(), "tests", "cli", spec.case_dir or name))
    end)
target_end()

includes("build")
