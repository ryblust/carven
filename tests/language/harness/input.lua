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

local function run(target, name)
    local input = os.tmpfile()
    io.writefile(input, inputs[name] or "", {encoding = "binary"})
    local output, errors = os.iorunv(target:targetfile(), {name, "世界", ""}, {
        stdin = input, timeout = 10000,
    })
    os.tryrm(input)
    assert(output:gsub("\r\n", "\n") == "ok\n" and errors == "",
        "standard input " .. name .. ": " .. output .. errors)
end

function main(target)
    -- EOF, errors, and prefetched bytes belong to a process input cursor.
    -- Reuse the binary while starting each independent input in a fresh process.
    for _, name in ipairs({
        "lines", "empty", "bytes", "text", "limit", "line-limit", "invalid", "invalid-text", "utf-errors", "io-error", "cold-fifo",
        "partial-cancel", "fragmented-crlf", "fragmented-utf8",
    }) do
        run(target, name)
    end
    return true
end
