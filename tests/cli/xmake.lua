local case_specs = {
    ["commands/dump"] = {
        inputs = {"input.cv", "lexical_error.cv", "syntax_error.cv"},
        steps = {
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
            {args = {"missing.cv", "--help"}, stdout = "stdout.txt"},
            {args = {}, stdout = "stdout.txt"},
        },
    },
    ["input/invalid_extension"] = {
        args = {"unknown"},
        exit_code = 1,
        stderr = "stderr.txt",
    },
    ["input/missing"] = {
        args = {"missing.cv"},
        exit_code = 1,
        stderr_contains = {"cannot read source file", "missing.cv"},
    },
    ["invocation/version"] = {
        args = {"--version"},
        stdout = "stdout.txt",
    },
    ["diagnostics/syntax"] = {
        inputs = {"input.cv"},
        args = {"input.cv"},
        exit_code = 1,
        stderr_contains = {"CV-SYNTAX", "expected parameter name", "input.cv:1:12"},
    },
    ["diagnostics/warning"] = {
        inputs = {"input.cv"},
        args = {"input.cv"},
        stderr_contains = {"CV-LINT-UNUSED-LOCAL", "input.cv"},
        output_files = {"input.cpp"},
        absent_files = {"carven/generated/input.hpp"},
    },
    ["output/default"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"bare_structure.cv"},
        output_files = {"bare_structure.cpp", "carven/generated/bare_structure.hpp"},
        absent_files = {".carven", ".carven-artifacts"},
    },
    ["output/destination"] = {
        fixtures = {
            ["../fixtures/bare_structure.cv"] = "bare_structure.cv",
            ["../state/preexisting.fixture"] = "emit/bare_structure.cpp",
            ["../state/stale.fixture"] = "emit/stale.txt",
        },
        steps = {
            {
                args = {"--output-dir=emit", "bare_structure.cv"},
                output_files = {
                    "emit/bare_structure.cpp",
                    "emit/carven/generated/bare_structure.hpp",
                    "emit/stale.txt",
                },
                absent_files = {".carven"},
                file_excludes = {
                    ["emit/bare_structure.cpp"] = {"preexisting output"},
                },
            },
            {
                args = {"bare_structure.cv", "-o", "emit"},
                output_files = {
                    "emit/bare_structure.cpp",
                    "emit/carven/generated/bare_structure.hpp",
                    "emit/stale.txt",
                },
            },
        },
    },
    ["output/stdout"] = {
        fixtures = {["../fixtures/bare_structure.cv"] = "bare_structure.cv"},
        args = {"bare_structure.cv", "--stdout"},
        stdout_contains = {
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
    ["output/stale_preservation"] = {
        fixtures = {
            ["../state/a.cv"] = "a.cv",
            ["../state/b.cv"] = "b.cv",
            ["../state/stale.fixture"] = "emit/stale.txt",
        },
        steps = {
            {
                args = {"--output-dir", "emit", "a.cv", "b.cv"},
                output_files = {
                    "emit/a.cpp",
                    "emit/b.cpp",
                    "emit/carven/generated/a.hpp",
                    "emit/carven/generated/b.hpp",
                    "emit/stale.txt",
                },
            },
            {
                args = {"--output-dir", "emit", "a.cv"},
                output_files = {
                    "emit/a.cpp",
                    "emit/carven/generated/a.hpp",
                    "emit/b.cpp",
                    "emit/carven/generated/b.hpp",
                    "emit/stale.txt",
                },
            },
        },
    },
    ["output/testing_modes"] = {
        fixtures = {["../fixtures/passing_test.cv"] = "passing_test.cv"},
        steps = {
            {
                args = {"-o", "none", "passing_test.cv"},
                output_files = {"none/passing_test.cpp"},
                absent_files = {
                    "none/carven/generated/passing_test.hpp",
                    "none/carven-test-main.cpp",
                },
                file_excludes = {
                    ["none/passing_test.cpp"] = {"Testing: passing fixture"},
                },
            },
            {
                args = {"passing_test.cv", "--tests=default", "--output-dir=default"},
                output_files = {
                    "default/passing_test.cpp",
                    "default/carven-test-main.cpp",
                },
                file_contains = {
                    ["default/passing_test.cpp"] = {"Testing: passing fixture"},
                },
            },
            {
                args = {"--tests=external", "--output-dir", "external", "passing_test.cv"},
                output_files = {"external/passing_test.cpp"},
                absent_files = {
                    "external/carven/generated/passing_test.hpp",
                    "external/carven-test-main.cpp",
                },
                file_contains = {
                    ["external/passing_test.cpp"] = {"Testing: passing fixture"},
                },
            },
        },
    },
    ["output/options"] = {
        steps = {
            {
                args = {"--unknown", "input.cv"},
                exit_code = 1,
                stderr_contains = {"unknown option '--unknown'"},
            },
            {
                args = {"-o", "--stdout", "input.cv"},
                exit_code = 1,
                stderr_contains = {"missing output path after '-o'"},
            },
            {
                args = {"--stdout", "-o", "emit", "input.cv"},
                exit_code = 1,
                stderr_contains = {"artifact destination was specified more than once"},
            },
        },
    },
    ["module_layout/duplicate"] = {
        project = "../project",
        args = {"main.cv", "./main.cv"},
        exit_code = 1,
        stderr_contains = {"duplicate module path 'main'"},
        absent_files = {"main.cpp", ".carven", ".carven-artifacts"},
    },
    ["module_layout/spec"] = {
        project = "../project",
        args = {
            "--output-dir=emit",
            "main.cv",
            "sibling.cv",
            "nested/worker.cv",
            "nested/local.cv",
            "__internal.cv",
        },
        output_files = {
            "emit/main.cpp",
            "emit/sibling.cpp",
            "emit/nested/worker.cpp",
            "emit/nested/local.cpp",
            "emit/__internal.cpp",
            "emit/carven/generated/main.hpp",
            "emit/carven/generated/sibling.hpp",
            "emit/carven/generated/nested/worker.hpp",
            "emit/carven/generated/nested/local.hpp",
            "emit/carven/generated/__internal.hpp",
        },
    },
}

local crafts_dir = path.join(os.projectdir(), "crafts")
local xmake_rule_dir = path.join(os.projectdir(), "tests", "cli", "xmake_rule")

local function use_local_carven(target)
    import("core.project.project")
    target:values_set("carven.program", project.target("carven"):targetfile())
end

local case_names = {}
for case_name in pairs(case_specs) do
    table.insert(case_names, case_name)
end
table.sort(case_names)

target("carven-test-cli")
    set_default(false)
    set_kind("phony")
    add_deps("carven", {inherit = false})

    for _, case_name in ipairs(case_names) do
        add_tests(case_name, {group = "cli"})
    end

    on_test(function (target, opt)
        local harness = import("harness", {
            rootdir = path.join(os.projectdir(), "tests", "cli"),
            anonymous = true,
        })
        local case_name = opt.name:match("^[^/]+/(.+)$") or opt.name
        return harness(target, opt, case_specs[case_name])
    end)

target("carven-test-xmake-default-domain-a")
    set_default(false)
    set_kind("object")
    add_rules("@carven/carven")
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_files(path.join(xmake_rule_dir, "domain.cv"))
    after_load(use_local_carven)

target("carven-test-xmake-default-domain-b")
    set_default(false)
    set_kind("object")
    add_rules("@carven/carven")
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_files(path.join(xmake_rule_dir, "domain.cv"))
    after_load(use_local_carven)

target("carven-test-xmake-default-domain-isolation")
    set_default(false)
    set_languages("c++20")
    add_deps(
        "carven-test-xmake-default-domain-a",
        "carven-test-xmake-default-domain-b"
    )
    add_files(path.join(xmake_rule_dir, "main.cpp"))
    add_tests("default-domain-isolation", {group = "cli"})
