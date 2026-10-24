import("lib.detect.find_tool")
import("core.project.config")
import("core.project.project")
import("private.action.run.runenvs")

local function run_files(program, args, files, opt)
    -- Keep quoted batches below Windows' 32767-character process limit.
    local prefix_size = #os.args({program}) + #os.args(args) + 2
    local batch = table.clone(args)
    local size = prefix_size
    for _, file in ipairs(files) do
        local file_size = #os.args({file}) + 1
        if size + file_size > 24000 and #batch > #args then
            os.vrunv(program, batch, opt)
            batch = table.clone(args)
            size = prefix_size
        end
        table.insert(batch, file)
        size = size + file_size
    end
    if #batch > #args then
        os.vrunv(program, batch, opt)
    end
end

function main(check)
    config.load()
    local target = project.target("carven-format")
    local formatter = path.absolute(target:targetfile(), os.projectdir())
    local addenvs, setenvs = runenvs.make(target)
    local tool = find_tool("clang-format", {require_version = ">=23.0"})
    assert(tool, "clang-format 23 or newer is required; install LLVM 23 or add it to PATH")

    local files = {}
    for _, root in ipairs({
        "src", "tests", "crafts", "examples", "benchmarks",
        "tools/format/src", "tools/format/tests", "tools/workspace", "tools/analyzer", "tools/hosting",
    }) do
        for _, extension in ipairs({"cpp", "cppm", "h", "hpp"}) do
            table.join2(files, os.files(path.join(os.projectdir(), root, "**." .. extension)))
        end
    end
    table.sort(files)
    for index, file in ipairs(files) do
        files[index] = path.relative(file, os.projectdir())
    end
    run_files(tool.program, check and {"--dry-run", "--Werror"} or {"-i"}, files,
        {curdir = os.projectdir()})
    local sources = {}
    for _, root in ipairs({"crafts", "examples", "tests", "benchmarks", "tools/lsp"}) do
        for _, file in ipairs(os.files(path.join(os.projectdir(), root, "**.cv"))) do
            local relative = path.relative(file, os.projectdir()):gsub("\\", "/")
            table.insert(sources, relative)
        end
    end
    for _, file in ipairs(os.files(path.join(os.projectdir(), "benchmarks", "**.cv.fixture"))) do
        table.insert(sources, path.relative(file, os.projectdir()))
    end
    -- Formatter inputs deliberately exercise unformatted text; check only outputs.
    for _, file in ipairs(os.files(path.join(os.projectdir(), "tools/format/tests/format/*/expected.cv"))) do
        table.insert(sources, path.relative(file, os.projectdir()))
    end
    table.sort(sources)
    run_files(formatter, {check and "check" or "write"}, sources,
        {curdir = os.projectdir(), addenvs = addenvs, setenvs = setenvs})
    print(check and "Formatting check passed." or "Formatting complete.")
end
