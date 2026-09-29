local function quote(value)
    return (string.format("%q", value):gsub("\\\n", "\\n"))
end

local function excerpt(value, offset)
    local first = math.max(1, offset - 40)
    local last = math.min(#value, offset + 80)
    local text = quote(value:sub(first, last))
    return (first > 1 and "..." or "") .. text .. (last < #value and "..." or "")
end

function format_text_difference(actual, expected)
    local offset, line = 1, 1
    while offset <= math.min(#actual, #expected) and actual:byte(offset) == expected:byte(offset) do
        if expected:byte(offset) == 10 then line = line + 1 end
        offset = offset + 1
    end
    return string.format("first difference at line %d, byte %d (1-based)\n"
        .. "  lengths: actual %d, expected %d\n  actual:   %s\n  expected: %s",
        line, offset, #actual, #expected, excerpt(actual, offset), excerpt(expected, offset))
end

function format_command(program, args)
    local words = {quote(program)}
    for _, arg in ipairs(args) do table.insert(words, quote(arg)) end
    return table.concat(words, " ")
end
