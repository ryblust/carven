function cases()
    return {
        {id = "ready", label = "Cold leaf + ready await"},
        {id = "stored", label = "Stored cold leaf + ready await", stored = true},
        {id = "yield", label = "Cold leaf + deferred FIFO yield", deferred = true},
        {id = "static_ready", label = "Static ready leaf specialization", static = true},
        {id = "static_yield", label = "Static yielding leaf specialization", static = true, deferred = true},
        {id = "chain", label = "Collapsible arithmetic await chain", chain = true},
    }
end

function defines(case)
    local function bit(value) return value and "1" or "0" end
    return {"CARVEN_ASYNC_BENCH_CHAIN=" .. bit(case.chain),
        "CARVEN_ASYNC_BENCH_STORED=" .. bit(case.stored),
        "CARVEN_ASYNC_BENCH_YIELD=" .. bit(case.deferred),
        "CARVEN_ASYNC_BENCH_STATIC=" .. bit(case.static)}
end
