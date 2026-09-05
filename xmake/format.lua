import("lib.detect.find_program")

function main(check)
    local program
    if os.host() == "macosx" then
        local brew = find_program("brew")
        if brew then
            local prefix = try {function () return os.iorunv(brew, {"--prefix", "llvm@23"}) end}
            if prefix then
                local candidate = path.join(prefix:trim(), "bin", "clang-format")
                if os.isfile(candidate) then
                    program = candidate
                end
            end
        end
    end
    program = program or find_program("clang-format")
    assert(program, "clang-format 23 is required; install llvm@23 on macOS or add clang-format to PATH")
    local version = os.iorunv(program, {"--version"}):trim()
    assert(version:match("version%s+(%d+)%.") == "23",
        "clang-format major version 23 is required; found %s (%s)", version, program)

    local files = {}
    for _, root in ipairs({"src", "tests", "crafts"}) do
        for _, extension in ipairs({"cpp", "cppm", "h", "hpp"}) do
            table.join2(files, os.files(path.join(os.projectdir(), root, "**." .. extension)))
        end
    end
    table.sort(files)
    for _, file in ipairs(files) do
        local args = check and {"--dry-run", "--Werror"} or {"-i"}
        table.insert(args, file)
        os.vrunv(program, args)
    end
    print(check and "Formatting check passed." or "Formatting complete.")
end
