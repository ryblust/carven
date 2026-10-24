local process = import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})

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

function main(target, opt, name)
    return process.main(target, opt, {
        args = {name, "世界", ""}, run_timeout = 10000,
        stdin_bytes = inputs[name], stdout_bytes = "ok\n",
    }, target:scriptdir())
end
