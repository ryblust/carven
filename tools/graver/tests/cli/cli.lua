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

local function run_case(target, case)
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
