function cases()
    local result = {}
    for _, operation in ipairs({"count", "find", "prefix"}) do
        for _, set in ipairs({"digits", "scattered"}) do
            table.insert(result, {id = operation .. "_" .. set, label = operation .. " / " .. set,
                operation = operation, set = set, size = "31, 32, 33, 16387, 65536 bytes"})
        end
    end
    table.insert(result, {id = "prefix_field", label = "unquoted CSV field prefix",
        operation = "prefix", set = "field", size = "31, 32, 33, 16387, 65536 bytes"})
    return result
end

function source(case)
    local expression = case.set == "digits" and "byte_range(48, 57)"
        or case.set == "field" and [[set_union(byte_range(0, 9),
    set_union(byte_range(11, 12),
        set_union(byte_range(14, 33),
            set_union(byte_range(35, 43), byte_range(45, 255)))))]]
        or "one_of([0, 9, 34, 47, 65, 97, 127, 128, 200, 255])"
    local body = case.operation == "count" and "return count_where(bytes, selected);"
        or case.operation == "prefix" and "return prefix_end(bytes, start, selected);"
        or "return find_from(bytes, start, selected);"
    local set_names = case.set == "field" and "byte_range, set_union"
        or case.set == "digits" and "byte_range" or "one_of"
    local scan_name = case.operation == "count" and "count_where"
        or case.operation == "prefix" and "prefix_end" or "find_from"
    return "import std::simd.scan using { " .. set_names .. ", " .. scan_name .. [[ };

private const selected = ]] .. expression .. [[;

export(cpp) fn scan_kernel(bytes: [u8], start: usize) -> usize {
    ]] .. body .. [[
}
]]
end

function scenarios(case)
    local result = {}
    local distributions = case.operation == "count" and {"mixed"} or {"early", "late", "absent"}
    for _, size in ipairs({31, 32, 33, 16387, 65536}) do
        for _, distribution in ipairs(distributions) do
            table.insert(result, {length = size, distribution = distribution})
        end
    end
    return result
end
