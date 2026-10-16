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
