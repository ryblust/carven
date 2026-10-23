local report = import("report", {rootdir = path.join(os.projectdir(), "tests", "harness")})
local runenvs = import("private.action.run.runenvs")

-- Every scenario owns its filesystem. Only explicit workflow steps share state.
local function read_bytes(filename)
    return assert(io.readfile(filename, {encoding = "binary"}), "cannot read " .. filename)
end

local function equal_bytes(actual, expected, label)
    if actual == expected then return end
    assert(false, label .. ": " .. report.format_text_difference(actual, expected))
end

local function run_case(target, case)
    local steps = case.steps or {case}
    local compiler = target:dep("carven")
    local program = path.absolute(compiler:targetfile(), os.projectdir())
    local addenvs, setenvs = runenvs.make(compiler)
    local envs = runenvs.join(addenvs, setenvs)
    local work = os.tmpfile() .. ".carven-format"
    local tree = path.join(work, "files")
    os.mkdir(tree)
    if case.name == "missing_formatter" then
        -- A formatter on PATH must not replace the missing sibling executable.
        envs.PATH = path.joinenv({path.directory(program), envs.PATH})
        local relocated = path.join(work, "bin", path.filename(program))
        os.mkdir(path.directory(relocated))
        os.cp(program, relocated)
        program = relocated
    end
    local state = {}
    for name, bytes in pairs(case.files or {}) do
        local filename = path.join(tree, name)
        os.mkdir(path.directory(filename))
        io.writefile(filename, bytes, {encoding = "binary"})
        state[name] = bytes
    end
    for index, step in ipairs(steps) do
        local args = table.join({"format"}, step.args)
        local label = case.name .. " step " .. index .. " (" .. report.format_command(program, args)
            .. "; retained on failure: " .. work .. ")"
        local prefix = path.join(work, tostring(index))
        io.writefile(prefix .. ".stdin", step.stdin or "", {encoding = "binary"})
        local code, run_error = os.execv(program, args, {
            try = true, timeout = 10000, curdir = tree, envs = envs,
            stdin = prefix .. ".stdin", stdout = prefix .. ".stdout", stderr = prefix .. ".stderr",
        })
        local stdout, stderr = read_bytes(prefix .. ".stdout"), read_bytes(prefix .. ".stderr")
        local context = run_error and (" (" .. tostring(run_error) .. ")") or ""
        local expected_code = step.code or 0
        assert(code == expected_code, label .. ": expected exit " .. expected_code .. ", got " .. tostring(code)
            .. context .. "\n" .. stderr)
        equal_bytes(stdout, step.stdout or "", label .. ": stdout")
        if step.diagnostic then
            assert(stderr:find(step.diagnostic, 1, true), label .. ": missing diagnostic " .. step.diagnostic)
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
    end
    os.rm(work)
    return true
end

function main(target, opt)
    local name = opt.name:match("^[^/]+/(.+)$")
    for _, case in ipairs(import("cases").main()) do
        if case.name == name then
            return run_case(target, case)
        end
    end
    raise("unknown carven-format CLI scenario: %s", opt.name)
end
