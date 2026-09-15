-- Every scenario owns its filesystem. Only explicit workflow steps share state.
local function read_bytes(filename)
    return assert(io.readfile(filename, {encoding = "binary"}), "cannot read " .. filename)
end

local function equal_bytes(actual, expected, label)
    if actual == expected then return end
    local offset, line = 1, 1
    while offset <= math.min(#actual, #expected) and actual:byte(offset) == expected:byte(offset) do
        if expected:byte(offset) == 10 then line = line + 1 end
        offset = offset + 1
    end
    assert(false, label .. " at line " .. line .. ", byte " .. offset
        .. "\n- expected: " .. string.format("%q", expected:sub(math.max(1, offset - 40), offset + 120))
        .. "\n+ actual:   " .. string.format("%q", actual:sub(math.max(1, offset - 40), offset + 120)))
end

function main(target)
    local program = path.absolute(target:dep("graver"):targetfile(), os.projectdir())
    local count = 0
    local function scenario(case)
        local work = os.tmpfile() .. ".graver"
        local tree = path.join(work, "files")
        os.mkdir(tree)
        local state = {}
        for name, bytes in pairs(case.files or {}) do
            local filename = path.join(tree, name)
            os.mkdir(path.directory(filename))
            io.writefile(filename, bytes, {encoding = "binary"})
            state[name] = bytes
        end
        for index, step in ipairs(case.steps or {case}) do
            local label = case.name .. " step " .. index .. " (retained on failure: " .. work .. ")"
            local prefix = path.join(work, tostring(index))
            io.writefile(prefix .. ".stdin", step.stdin or "", {encoding = "binary"})
            local code = os.execv(program, step.args, {
                try = true, timeout = 10000, curdir = tree,
                stdin = prefix .. ".stdin", stdout = prefix .. ".stdout", stderr = prefix .. ".stderr",
            })
            local stdout, stderr = read_bytes(prefix .. ".stdout"), read_bytes(prefix .. ".stderr")
            assert(code == step.code, label .. ": expected exit " .. step.code .. ", got " .. tostring(code) .. "\n" .. stderr)
            if step.contains then
                assert(stdout:find(step.contains, 1, true), label .. ": missing stdout fragment " .. step.contains)
            else
                equal_bytes(stdout, step.stdout or "", label .. ": stdout")
            end
            if step.diagnostic then
                assert(stderr:find(step.diagnostic, 1, true), label .. ": missing diagnostic " .. step.diagnostic .. "\n" .. stderr)
            else
                equal_bytes(stderr, "", label .. ": stderr")
            end
            for name, bytes in pairs(step.changed or {}) do state[name] = bytes end
            for name, bytes in pairs(state) do
                equal_bytes(read_bytes(path.join(tree, name)), bytes, label .. ": file " .. name)
            end
            for _, filename in ipairs(os.files(path.join(tree, "**"))) do
                assert(state[path.relative(filename, tree):gsub("\\", "/")] ~= nil, label .. ": unexpected file " .. filename)
            end
            for _, directory in ipairs(os.dirs(path.join(tree, "**"))) do
                assert(not path.filename(directory):find(".graver-", 1, true), label .. ": staging directory leaked")
            end
        end
        os.rm(work)
        count = count + 1
    end
    local input = "fn f(){let x=call(1,2);}// 尾注释\r\n"
    local expected = "fn f() {\n    let x = call(1, 2);\n} // 尾注释\n"
    -- Argument and stream contracts. Each case prepares only the files it uses.
    local cases = {
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
    }
    for _, case in ipairs(cases) do scenario(case) end
    scenario {name = "write_stdin", args = {"write", "-"}, stdin = input, code = 2, diagnostic = "stdin cannot"}
    scenario {name = "mixed_write_failure", files = {["input.cv"] = input, ["error.cv"] = "fn f("},
        args = {"write", "input.cv", "error.cv"}, code = 2, diagnostic = "CV-SYNTAX"}
    scenario {name = "batch_workflow", files = {["input.cv"] = input, ["second.cv"] = input, ["formatted.cv"] = expected}, steps = {
        {args = {"check", "second.cv", "input.cv", "./second.cv", "formatted.cv"}, code = 1, stdout = "input.cv\nsecond.cv\n"},
        {args = {"write", "input.cv", "formatted.cv", "second.cv"}, code = 0, changed = {["input.cv"] = expected, ["second.cv"] = expected}},
        {args = {"check", "input.cv", "formatted.cv", "second.cv"}, code = 0},
    }}
    scenario {name = "directory_workflow", files = {
        ["project/nested/你好 file.cv"] = input, ["project/build/ignored.cv"] = "invalid",
        ["project/.cache/ignored.cv"] = "invalid", ["project/ignored.txt"] = "invalid",
    }, steps = {
        {args = {"check", "project"}, code = 1, stdout = "project/nested/你好 file.cv\n"},
        {args = {"write", "project"}, code = 0, changed = {["project/nested/你好 file.cv"] = expected}},
        {args = {"check", "project"}, code = 0},
    }}
    -- One reserved command name and one flag-looking name cover literal paths.
    scenario {name = "literal_command_path", files = {["help"] = input}, steps = {
        {args = {"./help"}, code = 0, stdout = expected},
        {args = {"check", "help"}, code = 1, stdout = "help\n"},
        {args = {"write", "help"}, code = 0, changed = {["help"] = expected}},
    }}
    scenario {name = "literal_dash_path", files = {["--help"] = input},
        args = {"--help"}, code = 0, stdout = expected}
    scenario {name = "help", args = {"help"}, code = 0, contains = "Usage:"}
    scenario {name = "help_topic", args = {"help", "check"}, code = 0, contains = "Usage: graver check"}
    local cli_count = count
    local fixtures = os.dirs(path.join(os.projectdir(), "tools/graver/tests/format/*"))
    table.sort(fixtures)
    assert(#fixtures > 0, "no format fixtures discovered")
    for _, folder in ipairs(fixtures) do
        local formatted = read_bytes(path.join(folder, "expected.cv"))
        local files = {["input.cv"] = read_bytes(path.join(folder, "input.cv"))}
        scenario {name = "format/" .. path.filename(folder), files = files, steps = {
            {args = {"input.cv"}, code = 0, stdout = formatted},
            {args = {}, stdin = formatted, code = 0, stdout = formatted},
        }}
    end
    print("Graver: " .. cli_count .. " CLI contracts and " .. #fixtures .. " format examples passed")
    return true
end
