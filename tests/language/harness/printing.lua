function main(target)
    local stdout_file, stderr_file = os.tmpfile(), os.tmpfile()
    local status = os.execv(target:targetfile(), {}, {
        try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file,
    })
    -- Preserve NUL bytes while normalizing Windows text-stream line endings.
    local stdout = io.readfile(stdout_file, {encoding = "binary"}):gsub("\r\n", "\n")
    local stderr = io.readfile(stderr_file, {encoding = "binary"}):gsub("\r\n", "\n")
    os.tryrm(stdout_file)
    os.tryrm(stderr_file)
    assert(status == 0, "printing failed: " .. tostring(status))
    assert(stdout == "text: 你好😀\n42\ntrue\n我\n1.25\na\0b\n{unchanged}\nvalue=0007\n9\ndone\nmixed: 42 true 我 1.25 owned\n{value} 7 a\0b \ncount: 3\n1 2 3\n3\nafter 1\n1 false 11\n11\n42 before 1\n42 after 1\n18446744073709551615 -9223372036854775808\n"
        .. 'PrintedOrder {\n    price: PrintedMoney {\n        cents: 1250,\n    },\n    names: [\n        "a",\n        "b\\n",\n    ],\n}\n'
        .. 'PrintedState::Done(\n    42,\n    "ok",\n) PrintedState::Pending PrintedCode::Bad\n'
        .. 'PrintedEmpty {} [\n    1,\n    2,\n    3,\n]\n[\n    4,\n    5,\n]\n'
        .. 'PrintedOrder {\n    price: PrintedMoney {\n        cents: 99,\n    },\n    names: [\n        "a",\n        "b\\n",\n    ],\n} 1\n'
        .. 'PrintedOrder {\n    price: PrintedMoney {\n        cents: 99,\n    },\n    names: [\n        "a",\n        "b\\n",\n    ],\n}\n1..=3\nPrintedBox PrintedEnvelope {\n    box: PrintedBox,\n}\n'
        .. '我\n \n'
        .. [=[constant=123
[
    "a\n",
    "\"\\",
    "",
]
text=123; empty=; unicode=我😀
[
    ',',
    '我',
]
[
    PrintedMoney {
        cents: 7,
    },
]
[]
PrintedEmptyBox
[
    PrintedBox,
    PrintedBox,
]
1
2
PrintedPair {
    first: PrintedMoney {
        cents: 1,
    },
    second: PrintedMoney {
        cents: 2,
    },
}
]=],
        "unexpected stdout: " .. stdout)
    assert(stderr == "error: -3\n\nerror: -4status: false\nerror\n", "unexpected stderr: " .. stderr)
    return true
end
