local report = import("report", {rootdir = path.join(os.projectdir(), "tests", "harness")})
local runenvs = import("private.action.run.runenvs")

-- Every scenario owns its filesystem. Only explicit workflow steps share state.
local function read_bytes(filename, failures, label)
    local bytes = io.readfile(filename, {encoding = "binary"})
    if bytes == nil then
        table.insert(failures, label .. ": cannot read " .. filename)
    end
    return bytes or ""
end

local function equal_bytes(actual, expected, label, failures)
    if actual == expected then return end
    table.insert(failures, label .. ": " .. report.format_text_difference(actual, expected))
end

local function run_case(target, case, opt)
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
    local failures = {}
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
        local stdout = read_bytes(prefix .. ".stdout", failures, label .. ": stdout")
        local stderr = read_bytes(prefix .. ".stderr", failures, label .. ": stderr")
        local context = run_error and (" (" .. tostring(run_error) .. ")") or ""
        local expected_code = step.code or 0
        if code ~= expected_code then
            table.insert(failures, label .. ": expected exit " .. expected_code .. ", got " .. tostring(code)
                .. context .. "\n" .. stderr)
        end
        equal_bytes(stdout, step.stdout or "", label .. ": stdout", failures)
        if step.diagnostic then
            if not stderr:find(step.diagnostic, 1, true) then
                table.insert(failures, label .. ": missing diagnostic " .. step.diagnostic)
            end
        else
            equal_bytes(stderr, "", label .. ": stderr", failures)
        end
        for name, bytes in pairs(step.changed or {}) do state[name] = bytes end
        for name, bytes in pairs(state) do
            local file_label = label .. ": file " .. name
            equal_bytes(read_bytes(path.join(tree, name), failures, file_label), bytes, file_label, failures)
        end
        for _, filename in ipairs(os.files(path.join(tree, "**"))) do
            if state[path.relative(filename, tree):gsub("\\", "/")] == nil then
                table.insert(failures, label .. ": unexpected file " .. filename)
            end
        end
        if #failures > 0 then break end
    end
    if #failures > 0 then
        opt.errors = table.concat(failures, "\n")
        return false
    end
    os.rm(work)
    return true
end

function main(target, opt)
    local name = opt.name:match("^[^/]+/(.+)$")
    local commands = {
        help = {args = {"--help"}, run_timeout = 10000, stdout_contains = {"Usage:"}},
        help_topic = {args = {"help", "check"}, run_timeout = 10000,
            stdout_contains = {"Usage: carven format check"}},
    }
    if commands[name] then
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target:dep("carven-format"), opt, commands[name], os.projectdir())
    end
    for _, case in ipairs(import("cases").main()) do
        if case.name == name then
            return run_case(target, case, opt)
        end
    end
    raise("unknown carven-format CLI scenario: %s", opt.name)
end
