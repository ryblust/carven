function cases()
    return {
        {id = "ready", label = "Cold leaf + ready await", expression = "leaf(value, false)"},
        {id = "stored", label = "Stored cold leaf + ready await"},
        {id = "yield", label = "Cold leaf + deferred FIFO yield", expression = "leaf(value, true)"},
        {id = "static_ready", label = "Static ready leaf specialization", expression = "static_leaf(value, false)"},
        {id = "static_yield", label = "Static yielding leaf specialization", expression = "static_leaf(value, true)"},
        {id = "chain", label = "Dynamic nested await chain", expression = "chain(value, nesting)"},
    }
end

function source(case)
    local root = path.join(os.projectdir(), "benchmarks", "async")
    local text = io.readfile(path.join(root, "workload.cv.fixture"))
    if case.id == "static_ready" or case.id == "static_yield" then
        local factory = io.readfile(path.join(root, "static.cv.fixture"))
        text = text:gsub("\nasync fn main%(%)", function () return "\n" .. factory .. "\nasync fn main()" end)
    end
    if case.id == "stored" then
        return (text:gsub("checksum %+= await WORKLOAD;", [[let operation = leaf(value, false);
        let following = value + 1;
        checksum += await operation + following;]]))
    end
    return (text:gsub("WORKLOAD", case.expression))
end

function defines(case)
    local function bit(value) return value and "1" or "0" end
    return {"CARVEN_ASYNC_BENCH_CHAIN=" .. bit(case.id == "chain"),
        "CARVEN_ASYNC_BENCH_STORED=" .. bit(case.id == "stored"),
        "CARVEN_ASYNC_BENCH_YIELD=" .. bit(case.id == "yield" or case.id == "static_yield"),
        "CARVEN_ASYNC_BENCH_STATIC=" .. bit(case.id == "static_ready" or case.id == "static_yield")}
end
