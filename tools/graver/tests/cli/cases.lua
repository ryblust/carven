function main()
    local input = "fn f(){let x=call(1,2);}// 尾注释\r\n"
    local expected = "fn f() {\n    let x = call(1, 2);\n} // 尾注释\n"
    -- Argument and stream contracts. Each case prepares only the files it uses.
    return {
        {name = "implicit_stdin", args = {}, stdin = input, code = 0, stdout = expected},
        {name = "explicit_stdin", args = {"-"}, stdin = input, code = 0, stdout = expected},
        {name = "check_stdin_changed", args = {"check", "-"}, stdin = input, code = 1, stdout = "stdin\n"},
        {name = "check_stdin_clean", args = {"check", "-"}, stdin = expected, code = 0},
        {name = "missing_file", args = {"missing.cv"}, code = 2, diagnostic = "cannot read source file"},
        {name = "check_missing_input", args = {"check"}, stdin = input, code = 2, diagnostic = "requires an input"},
        {name = "write_missing_input", args = {"write"}, stdin = input, code = 2, diagnostic = "requires an input"},
        {name = "unknown_help", args = {"help", "unknown"}, code = 2, diagnostic = "unknown help topic"},
        {name = "extra_help_topic", args = {"help", "check", "write"}, code = 2, diagnostic = "at most one"},
        {name = "multiple_stdout_inputs", files = {["input.cv"] = input, ["second.cv"] = input},
            args = {"input.cv", "second.cv"}, code = 2, diagnostic = "exactly one"},
        {name = "write_stdin", args = {"write", "-"}, stdin = input, code = 2, diagnostic = "stdin cannot"},
        {name = "mixed_write_failure", files = {["input.cv"] = input, ["z-error.cv"] = "fn f("},
            args = {"write", "input.cv", "z-error.cv"}, code = 2, diagnostic = "CV-SYNTAX"},
        {name = "batch_workflow", files = {["input.cv"] = input, ["second.cv"] = input, ["formatted.cv"] = expected}, steps = {
            {args = {"check", "second.cv", "input.cv", "./second.cv", "formatted.cv"}, code = 1, stdout = "input.cv\nsecond.cv\n"},
            {args = {"write", "input.cv", "formatted.cv", "second.cv"}, code = 0, changed = {["input.cv"] = expected, ["second.cv"] = expected}},
            {args = {"check", "input.cv", "formatted.cv", "second.cv"}, code = 0},
        }},
        {name = "directory_workflow", files = {
            ["project/nested/你好 file.cv"] = input, ["project/build/ignored.cv"] = "invalid",
            ["project/.cache/ignored.cv"] = "invalid", ["project/ignored.txt"] = "invalid",
        }, steps = {
            {args = {"check", "project"}, code = 1, stdout = "project/nested/你好 file.cv\n"},
            {args = {"write", "project"}, code = 0, changed = {["project/nested/你好 file.cv"] = expected}},
        }},
        {name = "literal_command_path", files = {["help"] = input}, steps = {
            {args = {"./help"}, code = 0, stdout = expected},
            {args = {"check", "help"}, code = 1, stdout = "help\n"},
            {args = {"write", "help"}, code = 0, changed = {["help"] = expected}},
        }},
        {name = "literal_dash_path", files = {["--help"] = input},
            args = {"--help"}, code = 0, stdout = expected},
        {name = "help", args = {"help"}, code = 0, contains = "Usage:"},
        {name = "help_topic", args = {"help", "check"}, code = 0, contains = "Usage: graver check"},
    }
end
