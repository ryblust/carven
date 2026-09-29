local report = import("report", {rootdir = path.join(os.projectdir(), "tests", "harness")})

-- Every scenario owns its filesystem. Only explicit workflow steps share state.
local function read_bytes(filename)
    return assert(io.readfile(filename, {encoding = "binary"}), "cannot read " .. filename)
end

local function equal_bytes(actual, expected, label)
    if actual == expected then return end
    assert(false, label .. ": " .. report.format_text_difference(actual, expected))
end

local step_fields = {
    args = true, stdin = true, code = true, stdout = true,
    contains = true, diagnostic = true, changed = true,
}
local case_fields = {name = true, files = true, steps = true}
for field in pairs(step_fields) do case_fields[field] = true end

local function check_fields(value, allowed, label)
    for field in pairs(value) do
        assert(allowed[field], label .. " has unknown field: " .. tostring(field))
    end
end

local function run_case(target, case)
    check_fields(case, case_fields, "Graver CLI case " .. case.name)
    if case.steps then
        for field in pairs(step_fields) do
            assert(case[field] == nil,
                "Graver CLI case " .. case.name .. " ignores top-level step field: " .. field)
        end
    end
    local steps = case.steps or {case}
    assert(#steps > 0, "Graver CLI scenario has no execution steps: " .. case.name)
    for index, step in ipairs(steps) do
        local label = case.name .. " step " .. index
        if case.steps then check_fields(step, step_fields, label) end
        assert(type(step.args) == "table" and type(step.code) == "number",
            label .. " requires arguments and an expected exit code")
    end
    local program = path.absolute(target:dep("graver"):targetfile(), os.projectdir())
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
    for index, step in ipairs(steps) do
        local label = case.name .. " step " .. index .. " (" .. report.format_command(program, step.args)
            .. "; retained on failure: " .. work .. ")"
        local prefix = path.join(work, tostring(index))
        io.writefile(prefix .. ".stdin", step.stdin or "", {encoding = "binary"})
        local code, run_error = os.execv(program, step.args, {
            try = true, timeout = 10000, curdir = tree,
            stdin = prefix .. ".stdin", stdout = prefix .. ".stdout", stderr = prefix .. ".stderr",
        })
        local stdout, stderr = read_bytes(prefix .. ".stdout"), read_bytes(prefix .. ".stderr")
        local context = run_error and (" (" .. tostring(run_error) .. ")") or ""
        assert(code == step.code, label .. ": expected exit " .. step.code .. ", got " .. tostring(code)
            .. context .. "\n" .. stderr)
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
    return true
end

function main(target, opt)
    local name = opt.name:match("^[^/]+/(.+)$")
    for _, case in ipairs(import("cases").main()) do
        if case.name == name then
            return run_case(target, case)
        end
    end
    raise("unknown Graver CLI scenario: %s", opt.name)
end
