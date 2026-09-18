local case_specs = {
    ["commands/structural_display"] = {
        inputs = {"input.cv", "failure.cv"},
        steps = {
            {args = {"interpret", "input.cv"}, stdout = "stdout.txt"},
            {args = {"input.cv"}, stdout = "stdout.txt"},
            {args = {"check", "failure.cv"}, exit_code = 1,
                stderr_contains = {'actual: Money {\n    cents: 12,\n}', 'expected: Money {\n    cents: 15,\n}',
                    'true: <not evaluated>', '1: 1', '2: 2'}},
        },
    },
    ["commands/check"] = {
        inputs = {"input.cv", "invalid_test.cv", "crafts/demo/dependency.cv"},
        fixtures = {
            ["../interpretation/declarations.cv"] = "declarations.cv",
            ["../interpretation/static_failure.cv"] = "static_failure.cv",
            ["../../diagnostics/syntax/input.cv"] = "syntax.cv",
            ["../../diagnostics/warning/input.cv"] = "warning.cv",
        },
        steps = {
            {args = {"check"}, exit_code = 1, stderr_contains = {"requires at least one source file", "carven check --help"}},
            {args = {"check", "input.cv", "--stdout"}, exit_code = 1, stderr_contains = {"unknown check option", "carven check --help"}},
            {args = {"check", "missing.cv"}, exit_code = 1, stderr_contains = {"cannot read source file"}},
            {args = {"check", "syntax.cv"}, exit_code = 1, stderr_contains = {"CV-SYNTAX", "syntax.cv:1:12"}},
            {args = {"check", "warning.cv"}, stderr_ordered = {"CV-LINT-UNUSED-LOCAL", "carven: check passed\n"}},
            {args = {"check", "static_failure.cv"}, exit_code = 1, stderr_contains = {"CV-CONST-TEST"}},
            {args = {"check", "invalid_test.cv"}, exit_code = 1, stderr_contains = {"CV-TYPE-MISMATCH", "invalid_test.cv:2:"}},
            {args = {"check", "declarations.cv"}, stderr = "passed.txt"},
            {
                args = {"check", "input.cv"}, exit_code = 1,
                stderr_contains = {"CV-IMPORT-RESOLUTION", "input.cv"},
            },
            {
                args = {"check", "input.cv", "crafts/demo/dependency.cv"},
                stdout = "stdout.txt", stderr = "stderr.txt",
                absent_files = {"input.cpp", "crafts/demo/dependency.cpp", "warning.cpp", "carven", "program", "program.exe"},
            },
        },
    },
    ["commands/timings"] = {
        inputs = {"input.cv"},
        fixtures = {
            ["../interpretation/declarations.cv"] = "declarations.cv",
            ["../interpretation/escaped_failure.cv"] = "failure.cv",
            ["../../diagnostics/syntax/input.cv"] = "syntax.cv",
            ["../dump/lexical_error.cv"] = "lexical.cv",
            ["../../diagnostics/warning/input.cv"] = "warning.cv",
            ["../../output/state/preexisting.fixture"] = "blocked",
        },
        steps = {
            {args = {"check", "input.cv"}, stdout = "compile.txt", stderr = "../check/passed.txt"},
            {
                args = {"check", "--timings", "input.cv", "--timings", "declarations.cv"}, stdout = "compile.txt",
                stderr_ordered = {"carven: check passed in ", "Source loading", "Lexing", "Parsing", "Semantic analysis"},
                stderr_not_contains = {"C++ generation", "Program execution", "carven: check passed\n"},
            },
            {
                args = {"compile", "input.cv", "--timings", "-o", "emit"}, stdout = "compile.txt",
                stderr_ordered = {"carven: compilation finished in ", "Source loading", "Lexing", "Parsing", "Semantic analysis", "C++ generation", "Artifact writing"},
                output_files = {"emit/input.cpp"},
            },
            {
                args = {"compile", "input.cv", "--stdout", "--timings"},
                stdout_contains = {"==> input.cpp <=="},
                stderr_ordered = {"prepared", "carven: compilation finished in ", "Artifact writing"},
            },
            {
                args = {"interpret", "--timings", "--trace", "input.cv"}, stdout = "run.txt",
                stderr_ordered = {"statement", "carven: interpretation finished in ", "Semantic analysis", "Program execution"},
                stderr_not_contains = {"C++ generation", "Native compilation"},
            },
            {
                args = {"--timings", "input.cv"}, stdout = "run.txt",
                stderr_ordered = {"carven: run exited with code 0 in ", "Source collection", "Source loading", "Lexing", "Parsing", "Semantic analysis", "C++ generation", "Artifact writing", "Native compilation", "Program execution"},
            },
            {args = {"interpret", "input.cv", "--", "--timings"}, stdout = "run.txt"},
            {args = {"input.cv", "--", "--timings"}, stdout = "run.txt"},
            {
                args = {"interpret", "--timings", "declarations.cv"},
                stderr_contains = {"carven: interpretation finished (no entry point) in "},
                stderr_not_contains = {"Program execution"},
            },
            {
                args = {"check", "--timings", "missing.cv"}, exit_code = 1,
                stderr_ordered = {"cannot read source file", "carven: check failed in ", "Source loading"},
                stderr_not_contains = {"Lexing", "Parsing", "Semantic analysis", "passed"},
            },
            {
                args = {"check", "--timings", "lexical.cv"}, exit_code = 1,
                stderr_ordered = {"CV-", "carven: check failed in ", "Lexing"},
                stderr_not_contains = {"Parsing", "Semantic analysis"},
            },
            {
                args = {"check", "--timings", "syntax.cv"}, exit_code = 1,
                stderr_ordered = {"CV-SYNTAX", "carven: check failed in ", "Parsing"},
                stderr_not_contains = {"Semantic analysis"},
            },
            {
                args = {"check", "--timings", "warning.cv"},
                stderr_ordered = {"CV-LINT-UNUSED-LOCAL", "carven: check passed in "},
            },
            {
                args = {"compile", "--timings", "input.cv", "-o", "blocked"}, exit_code = 1,
                stdout = "compile.txt",
                stderr_ordered = {"cannot create directory", "carven: compilation failed in ", "Artifact writing"},
            },
            {
                args = {"interpret", "--timings", "failure.cv"}, exit_code = 1,
                stderr_ordered = {"CV-INTERPRET-EXECUTION", "carven: interpretation failed in ", "Program execution"},
            },
            {
                args = {"--timings", "failure.cv"}, exit_code = 1,
                stderr_ordered = {"carven: run exited with code 1 in ", "Program execution"},
            },
        },
    },
    ["commands/default_initialization"] = {
        inputs = {"input.cv"},
        steps = {
            {args = {"check", "input.cv"}, stdout = "compile.txt", stderr = "../check/passed.txt"},
            {args = {"interpret", "input.cv"}, stdout = "run.txt"},
            {args = {"input.cv"}, stdout = "run.txt"},
        },
    },
    ["commands/constant_blocks"] = {
        inputs = {"input.cv", "helper.cv"},
        steps = {
            {args = {"check", "input.cv", "helper.cv"}, stderr = "../check/passed.txt", stdout = "stdout.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n"}},
            {args = {"compile", "input.cv", "helper.cv", "-o", "emit"}, stdout = "stdout.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n"}},
            {args = {"interpret", "input.cv", "helper.cv"}, stdout = "run.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n", "runtime\nruntime\n"}},
            {args = {"input.cv", "helper.cv"}, stdout = "run.txt", stdout_unordered = true, stdout_ordered = {"helper call\nmodule\n", "runtime\nruntime\n"}},
            {args = {"dump", "ast", "input.cv"}, stdout_contains = {"ConstantBlock", "ConstTestDeclaration"}},
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
        inputs = {"input.cv", "lexical_error.cv", "syntax_error.cv"},
        steps = {
            {args = {"dump"}, exit_code = 1, stderr_contains = {"expected", "carven dump --help"}},
            {args = {"dump", "input.cv"}, stdout = "all.txt"},
            {args = {"dump", "--timings", "input.cv", "--timings"}, stdout = "all.txt",
                stderr_ordered = {"carven: dump finished in ", "Source loading", "Lexing", "Parsing"},
                stderr_not_contains = {"Semantic analysis"}},
            {args = {"dump", "tokens", "input.cv", "--timings"}, stdout_contains = {"Tokens \"input.cv\""},
                stderr_ordered = {"carven: dump finished in ", "Source loading", "Lexing"},
                stderr_not_contains = {"Parsing", "Semantic analysis"}},
            {args = {"dump", "--timings", "ast", "input.cv"}, stdout_contains = {"SourceModule"},
                stderr_ordered = {"carven: dump finished in ", "Source loading", "Lexing", "Parsing"}},
            {args = {"dump", "--timings", "missing.cv"}, exit_code = 1,
                stderr_ordered = {"cannot read source file", "carven: dump failed in ", "Source loading"},
                stderr_not_contains = {"Lexing", "Parsing"}},
            {args = {"dump", "lexical_error.cv", "--timings"}, exit_code = 1, stdout = "lexical.txt",
                stderr_ordered = {"CV-LEXICAL", "carven: dump failed in ", "Lexing"},
                stderr_not_contains = {"Parsing"}},
            {args = {"dump", "syntax_error.cv", "--timings"}, exit_code = 1, stdout = "syntax.txt",
                stderr_ordered = {"CV-SYNTAX", "carven: dump failed in ", "Parsing"}},
            {args = {"dump", "tokens"}, exit_code = 1, stderr_contains = {"expected"}},
            {args = {"dump", "unknown", "input.cv"}, exit_code = 1, stderr_contains = {"unknown dump kind"}},
            {args = {"dump", "--timings", "input.cv", "--unknown"}, exit_code = 1,
                stderr_contains = {"unknown option"}, stderr_not_contains = {"carven: dump failed in "}},
            {args = {"dump", "ast", "input.cv", "input.cv"}, exit_code = 1, stderr_contains = {"expected"}},
            {args = {"dump", "tokens", "input.cv"}, stdout_contains = {"Tokens \"input.cv\""}},
            {args = {"dump", "ast", "input.cv"}, stdout_contains = {"SourceModule"}},
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
            {args = {"--help"}, stdout = "stdout.txt"},
            {args = {"compile", "--help"}, stdout = "compile.txt"},
            {args = {"compile", "-h"}, stdout = "compile.txt"},
            {args = {"check", "--help"}, stdout = "check.txt"},
            {args = {"check", "-h"}, stdout = "check.txt"},
            {args = {"interpret", "--help"}, stdout = "interpret.txt"},
            {args = {"interpret", "-h"}, stdout = "interpret.txt"},
            {args = {"dump", "--help"}, stdout = "dump.txt"},
            {args = {"dump", "-h"}, stdout = "dump.txt"},
            {args = {}, stdout = "stdout.txt"},
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
        args = {"compile", "input.cv"},
        exit_code = 1,
        stderr_contains = {"CV-SYNTAX", "expected parameter name", "input.cv:1:12"},
    },
    ["diagnostics/warning"] = {
        inputs = {"input.cv"},
        args = {"compile", "input.cv"},
        stderr_contains = {"CV-LINT-UNUSED-LOCAL", "input.cv"},
        output_files = {"input.cpp"},
        absent_files = {"carven/generated/input.hpp"},
    },
    ["output/default"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"compile", "bare_structure.cv"},
        output_files = {"bare_structure.cpp", "carven/generated/bare_structure.hpp"},
        absent_files = {".carven", ".carven-artifacts"},
    },
    ["output/destination"] = {
        fixtures = {
            ["../fixtures/bare_structure.cv"] = "bare_structure.cv",
            ["../state/preexisting.fixture"] = "emit/bare_structure.cpp",
            ["../state/stale.fixture"] = "emit/stale.txt",
        },
        args = {"compile", "--output-dir=emit", "bare_structure.cv"},
        output_files = {
            "emit/bare_structure.cpp",
            "emit/carven/generated/bare_structure.hpp",
            "emit/stale.txt",
        },
        absent_files = {".carven"},
        file_not_contains = {
            ["emit/bare_structure.cpp"] = {"preexisting output"},
        },
    },
    ["output/stdout"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"compile", "bare_structure.cv", "--stdout"},
        stdout_ordered = {
            "==> bare_structure.cpp <==",
            "==> carven/generated/bare_structure.hpp <==",
        },
        absent_files = {
            "bare_structure.cpp",
            "carven/generated/bare_structure.hpp",
            ".carven-artifacts",
            ".carven",
        },
    },
    ["output/testing_modes"] = {
        fixtures = {["../fixtures/passing_test.cv"] = "passing_test.cv"},
        steps = {
            {
                args = {"compile", "-o", "none", "passing_test.cv"},
                output_files = {"none/passing_test.cpp"},
                absent_files = {
                    "none/carven/generated/passing_test.hpp",
                    "none/carven/generated/carven-test-runner.hpp",
                    "none/carven/generated/carven-test-main.cpp",
                    "none/carven-test-runner.hpp",
                    "none/carven-test-main.cpp",
                },
                file_not_contains = {
                    ["none/passing_test.cpp"] = {"Testing: passing fixture"},
                },
            },
            {
                args = {"compile", "passing_test.cv", "--tests=default", "--output-dir=default"},
                output_files = {
                    "default/passing_test.cpp",
                    "default/carven/generated/carven-test-runner.hpp",
                    "default/carven/generated/carven-test-main.cpp",
                },
                file_contains = {
                    ["default/passing_test.cpp"] = {"Testing: passing fixture"},
                },
                absent_files = {
                    "default/carven-test-runner.hpp",
                    "default/carven-test-main.cpp",
                },
            },
            {
                args = {"compile", "--tests=external", "--output-dir", "external", "passing_test.cv"},
                output_files = {
                    "external/passing_test.cpp",
                    "external/carven/generated/carven-test-runner.hpp",
                },
                absent_files = {
                    "external/carven/generated/passing_test.hpp",
                    "external/carven/generated/carven-test-main.cpp",
                    "external/carven-test-runner.hpp",
                    "external/carven-test-main.cpp",
                },
                file_contains = {
                    ["external/passing_test.cpp"] = {"Testing: passing fixture"},
                },
            },
        },
    },
    ["output/sink_failure"] = {
        fixtures = {
            ["../fixtures/bare_structure.cv"] = "bare_structure.cv",
            ["../state/preexisting.fixture"] = "blocked",
        },
        args = {"compile", "bare_structure.cv", "--output-dir=blocked"},
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
    ["module_layout/duplicate"] = {
        project = "../project",
        args = {"compile", "main.cv", "./main.cv"},
        exit_code = 1,
        stderr_contains = {"duplicate module path 'main'"},
        absent_files = {"main.cpp", ".carven", ".carven-artifacts"},
    },
    ["module_layout/spec"] = {
        project = "../project",
        args = {
            "compile",
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

case_specs["commands/native_crafts"] = {
    inputs = {"input.cv", "unlisted.cv", "crafts/demo/api.cv", "crafts/demo/native.hpp", "crafts/demo/native.cpp"},
    steps = {
        {args = {"input.cv"}, stdout = "stdout.txt"},
        {
            installed_toolchain = true,
            args = {"input.cv", "./input.cv", "crafts/demo/api.cv"},
            stdout = "stdout.txt",
            absent_files = {"input.cpp", "program", ".carven", ".xmake"},
        },
        {
            args = {"compile", "input.cv", "-o", "emit"},
            exit_code = 1,
            stderr_contains = {"CV-", "module"},
            absent_files = {"emit/input.cpp"},
        },
    },
}
case_specs["commands/native_execution"] = {
    inputs = {"input.cv", "library.cv", "arguments.hpp"},
    steps = {
        {
            args = {"input.cv", "--", "--help", "space argument", "; echo injected", "", 'a"b', "trailing\\", 'slash\\"quote'},
            exit_code = 1,
            stdout = "stdout.txt",
            absent_files = {"input.cpp", "program", "program.exe"},
        },
        {
            args = {"library.cv"},
            exit_code = 1,
            stderr_contains = {"running a program requires an entry point"},
            absent_files = {"library.cpp", "program"},
        },
        {
            args = {"input.cv", "--stdout"},
            exit_code = 1,
            stderr_contains = {"unknown option '--stdout'", "carven --help"},
        },
    },
}

case_specs["commands/interpretation"] = {
    inputs = {
        "input.cv", "unsupported.cv", "failure.cv", "limit.cv", "wrapping.cv",
        "declarations.cv", "static_only.cv", "static_failure.cv", "main.cv", "floating.cv", "typed_failures.cv", "escaped_failure.cv",
    },
    steps = {
        {args = {"interpret"}, exit_code = 1, stderr_contains = {"requires at least one source file", "carven interpret --help"}},
        {
            args = {"interpret", "typed_failures.cv"},
            stdout = "typed_failures.txt",
        },
        {
            args = {"interpret", "escaped_failure.cv"}, exit_code = 1,
            stderr_contains = {"CV-INTERPRET-EXECUTION", "typed failure escaped", "while interpreting this function call"},
        },
        {
            args = {"interpret", "floating.cv"},
            stdout = "floating.txt",
        },
        {
            args = {"interpret", "declarations.cv"},
        },
        {
            args = {"interpret", "--trace", "static_only.cv", "declarations.cv"},
            stdout = "static_only.txt",
            absent_files = {"static_only.cpp", "declarations.cpp", "program"},
        },
        {
            args = {"interpret", "static_failure.cv"}, exit_code = 1,
            stderr_contains = {"CV-CONST-TEST"},
        },
        {
            args = {"interpret", "main.cv", "declarations.cv"},
            stdout = "main.txt",
        },
        {
            args = {"interpret", "main.cv", "input.cv"}, exit_code = 1,
            stderr_contains = {"CV-ENTRY-DUPLICATE"},
        },
        {
            args = {"interpret", "input.cv", "--", "--help"},
            stdout = "stdout.txt",
            absent_files = {"input.cpp", "program", "program.exe"},
        },
        {
            args = {"interpret", "--trace", "input.cv"},
            stdout = "stdout.txt",
            stderr_contains = {"call main", "call fib", "return fib", "statement"},
        },
        {
            args = {"interpret", "unsupported.cv"}, exit_code = 1,
            stderr_contains = {"CV-INTERPRET-ADMISSION"},
        },
        {
            args = {"interpret", "failure.cv"}, exit_code = 1,
            stdout = "failure.txt",
            stderr_contains = {"CV-INTERPRET-EXECUTION", "failure.cv:1:", "while interpreting this function call"},
        },
        {
            args = {"interpret", "--max-steps", "20", "limit.cv"}, exit_code = 1,
            stdout = "limit.txt",
            stderr_contains = {"CV-INTERPRET-LIMIT"},
        },
        {
            args = {"interpret", "--max-steps", "-1", "input.cv"}, exit_code = 1,
            stderr_contains = {"nonnegative integer"},
        },
        {
            args = {"interpret", "wrapping.cv"},
            stdout = "wrapping.txt",
        },
    },
}
table.insert(case_specs["commands/interpretation"].steps, {
    args = {"input.cv"},
    stdout = "stdout.txt",
    absent_files = {"input.cpp", "program", "program.exe"},
})

local xmake_rule_dir = path.join(os.projectdir(), "tests", "cli", "xmake_rule")

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

for _, domain in ipairs({"a", "b"}) do
    target("carven-test-cli-default-domain-" .. domain)
        set_default(false)
        set_kind("object")
        add_rules("@carven/carven")

        set_languages("c++20")
        add_files(path.join(xmake_rule_dir, "domain.cv"))

    target_end()
end

target("carven-test-cli-default-domain-isolation")
    set_default(false)
    set_languages("c++20")
    add_deps(
        "carven-test-cli-default-domain-a",
        "carven-test-cli-default-domain-b"
    )
    add_files(path.join(xmake_rule_dir, "main.cpp"))
    add_tests("default-domain-isolation", {group = "cli", run_timeout = 30000})
