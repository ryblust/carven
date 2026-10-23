function main()
    local input = "fn f(){let x=call(1,2);}// 尾注释\r\n"
    local expected = "fn f() {\n    let x = call(1, 2);\n} // 尾注释\n"
    -- Argument and stream contracts. Each case prepares only the files it uses.
    local cases = {
        {name = "implicit_stdin", args = {}, stdin = input, stdout = expected},
        {name = "explicit_stdin", args = {"-"}, stdin = input, stdout = expected},
        {name = "check_stdin_changed", args = {"check", "-"}, stdin = input, code = 1, stdout = "stdin\n"},
        {name = "missing_formatter", args = {}, stdin = input, code = 1,
            diagnostic = "install or build carven-format beside carven"},
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
        {name = "check_stdin_clean", args = {"check", "-"}, stdin = expected},
        {name = "batch_workflow", files = {["输入 🌍.cv"] = input, ["second.cv"] = input, ["formatted.cv"] = expected}, steps = {
            {args = {"check", "second.cv", "输入 🌍.cv", "formatted.cv", "输入 🌍.cv"}, code = 1, stdout = "second.cv\n输入 🌍.cv\n"},
            {args = {"write", "输入 🌍.cv", "formatted.cv", "second.cv"}, changed = {["输入 🌍.cv"] = expected, ["second.cv"] = expected}},
            {args = {"check", "输入 🌍.cv", "formatted.cv", "second.cv"}},
        }},
        {name = "directory_workflow", files = {
            ["项目 🌍/nested/你好 file.cv"] = input, ["项目 🌍/build/ignored.cv"] = "invalid",
            ["项目 🌍/.cache/ignored.cv"] = "invalid", ["项目 🌍/ignored.txt"] = "invalid",
        }, steps = {
            {args = {"write", "项目 🌍"}, changed = {["项目 🌍/nested/你好 file.cv"] = expected}},
        }},
        {name = "literal_command_path", files = {["help"] = input}, steps = {
            {args = {"./help"}, stdout = expected},
            {args = {"write", "help"}, changed = {["help"] = expected}},
        }},
        {name = "literal_dash_path", files = {["--help"] = input},
            args = {"./--help"}, stdout = expected},
    }
    if os.host() ~= "windows" then
        table.insert(cases, {
            name = "invalid_utf8_argument", args = {string.char(255)}, code = 1,
            diagnostic = "process argument is not valid UTF-8",
        })
    end
    return cases
end
