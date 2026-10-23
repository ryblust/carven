import("lib.detect.find_tool")
import("core.project.config")
import("core.project.project")
import("private.action.run.runenvs")

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
    local args = check and {"--dry-run", "--Werror"} or {"-i"}
    for _, file in ipairs(files) do
        table.insert(args, path.relative(file, os.projectdir()))
    end
    if #files > 0 then
        os.vrunv(tool.program, args, {curdir = os.projectdir()})
    end
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
    local formatter_args = {check and "check" or "write"}
    table.join2(formatter_args, sources)
    os.vrunv(formatter, formatter_args, {curdir = os.projectdir(), addenvs = addenvs, setenvs = setenvs})
    print(check and "Formatting check passed." or "Formatting complete.")
end
