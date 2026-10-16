local runenvs = import("private.action.run.runenvs")

local inputs = {
    ["utf-errors"] = io.readfile(path.join(os.projectdir(), "tests", "language", "io", "utf_errors.txt"), {encoding = "binary"}),
    lines = "a\r\n\n世界\nlast",
    empty = "",
    bytes = string.char(0, 255, 97),
    text = "a\0世界",
    limit = "abcd\n",
    ["line-limit"] = "abcd\r\nrest\n",
    invalid = string.char(255) .. "\nvalid\n",
    ["invalid-text"] = "a" .. string.char(228),
    ["cold-fifo"] = "one\ntwo\nxy",
}

function main(target, name)
    local input = os.tmpfile()
    io.writefile(input, inputs[name], {encoding = "binary"})
    local addenvs, setenvs = runenvs.make(target)
    local output, errors = os.iorunv(target:targetfile(), {name, "世界", ""}, {
        stdin = input, timeout = 10000, envs = runenvs.join(addenvs, setenvs),
    })
    os.tryrm(input)
    assert(output:gsub("\r\n", "\n") == "ok\n" and errors == "",
        "standard input " .. name .. ": " .. output .. errors)
    return true
end
