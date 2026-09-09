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
    for _, root in ipairs({"src", "tests", "crafts", "examples"}) do
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
    print(check and "Formatting check passed." or "Formatting complete.")
end
