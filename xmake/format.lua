import("lib.detect.find_program")

function main(check)
    local program
    if os.host() == "macosx" then
        local brew = find_program("brew")
        if brew then
            local prefix = try {function () return os.iorunv(brew, {"--prefix", "llvm"}) end}
            if prefix then
                local candidate = path.join(prefix:trim(), "bin", "clang-format")
                if os.isfile(candidate) then
                    program = candidate
                end
            end
        end
    end
    program = program or find_program("clang-format")
    assert(program, "clang-format is required; install llvm on macOS or add clang-format to PATH")

    local files = {}
    for _, root in ipairs({"src", "tests", "crafts", "examples", "tools/graver/src", "tools/graver/tests"}) do
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
        os.vrunv(program, args, {curdir = os.projectdir()})
    end
    -- These fixtures intentionally fail lexing or parsing.
    local rejected = {
        ["tests/cli/commands/dump/lexical_error.cv"] = true,
        ["tests/cli/commands/dump/syntax_error.cv"] = true,
        ["tests/cli/diagnostics/syntax/input.cv"] = true,
    }
    local sources = {}
    for _, root in ipairs({"crafts", "examples", "tests"}) do
        for _, file in ipairs(os.files(path.join(os.projectdir(), root, "**.cv"))) do
            local relative = path.relative(file, os.projectdir()):gsub("\\", "/")
            if not rejected[relative] then table.insert(sources, relative) end
        end
    end
    -- Formatter inputs deliberately exercise unformatted text; check only outputs.
    for _, file in ipairs(os.files(path.join(os.projectdir(), "tools/graver/tests/format/*/expected.cv"))) do
        table.insert(sources, path.relative(file, os.projectdir()))
    end
    table.sort(sources)
    os.vrunv(os.programfile(), {"build", "graver"}, {curdir = os.projectdir()})
    local graver_args = {"run", "graver", check and "check" or "write"}
    table.join2(graver_args, sources)
    os.vrunv(os.programfile(), graver_args, {curdir = os.projectdir()})
    print(check and "Formatting check passed." or "Formatting complete.")
end
