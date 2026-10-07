import("core.base.json")

function cases()
    local result = {}
    for _, kind in ipairs({"events", "catalog"}) do
        for _, records in ipairs({1, 32, 256}) do
            table.insert(result, {id = kind .. "_" .. records, kind = kind, records = records,
                label = kind .. " API document", size = records .. " records"})
        end
    end
    return result
end

function source(root)
    return io.readfile(path.join(root, "workload.cv.fixture"))
end

function documents(case)
    -- Serialize fields separately so object key iteration never changes input bytes.
    local records, state = {}, 1729
    for index = 1, case.records do
        state = (state * 1664525 + 1013904223) % 4294967296
        if case.kind == "events" then
            local message = (index % 3 == 0 and "用户请求 café 😀: " or "request completed: ")
                .. string.rep("payload ", state % 17) .. "quoted \"value\"\nnext line\\tail"
            table.insert(records, '{"timestamp":"2026-10-09T12:34:56Z","request_id":' .. state
                .. ',"level":' .. json.encode(index % 5 == 0 and "warn" or "info")
                .. ',"path":' .. json.encode("/api/items/" .. (state % 10000))
                .. ',"elapsed_ms":' .. (state % 1000) .. '.125,"message":' .. json.encode(message) .. '}')
        else
            table.insert(records, '{"id":' .. state .. ',"name":' .. json.encode("item " .. index .. " – 東京")
                .. ',"price":' .. (state % 10000) .. '.99,"available":' .. (index % 3 == 0 and 'false' or 'true')
                .. ',"discount":null,"tags":["featured","size-' .. (state % 20) .. '"],"dimensions":{"width":'
                .. (state % 100) .. ',"height":' .. (state % 80) .. '}}')
        end
    end
    local text = '{"schema":1,"records":[' .. table.concat(records, ',\n') .. ']}'
    assert(json.decode(text), "generated JSON document must be independently decodable")
    local length = #text
    return {
        {id = "valid", text = text, expected = length * 4, outcome = "accepted"},
        {id = "trailing", text = text .. '#', expected = length * 4 + 1, outcome = "JSONError", offset = length},
        {id = "truncated", text = text:sub(1, -2), expected = (length - 1) * 4 + 1,
            outcome = "JSONError", offset = length - 1},
        -- Replacing the final delimiter makes both syntax and encoding invalid.
        {id = "invalid_utf8", text = text:sub(1, -2) .. string.char(255), expected = (length - 1) * 4 + 2,
            outcome = "UTF8Error", offset = length - 1},
    }
end
