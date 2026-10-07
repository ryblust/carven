function cases()
    return {
        {id = "ready", label = "Cold leaf + ready await", body = "checksum += await leaf(value, false);"},
        {id = "stored", label = "Stored cold leaf + ready await", stored = true,
            body = [[let operation = leaf(value, false);
        let following = value + 1;
        checksum += await operation + following;]]},
        {id = "yield", label = "Cold leaf + deferred FIFO yield", deferred = true,
            body = "checksum += await leaf(value, true);"},
        {id = "static_ready", label = "Static ready leaf specialization", static = true,
            body = "checksum += await static_leaf(value, false);"},
        {id = "static_yield", label = "Static yielding leaf specialization", static = true, deferred = true,
            body = "checksum += await static_leaf(value, true);"},
        {id = "chain", label = "Collapsible arithmetic await chain", chain = true,
            body = "checksum += await chain(value, nesting);"},
    }
end

function source(case, source_root)
    local text = io.readfile(path.join(source_root, "workload.cv.fixture"))
    local helpers = case.static and io.readfile(path.join(source_root, "static_factory.cv.fixture")) or ""
    text = text:gsub("// @HELPERS@", function () return helpers end)
    return (text:gsub("// @BODY@", function () return case.body end))
end

function defines(case)
    local function bit(value) return value and "1" or "0" end
    return {"CARVEN_ASYNC_BENCH_CHAIN=" .. bit(case.chain),
        "CARVEN_ASYNC_BENCH_STORED=" .. bit(case.stored),
        "CARVEN_ASYNC_BENCH_YIELD=" .. bit(case.deferred),
        "CARVEN_ASYNC_BENCH_STATIC=" .. bit(case.static)}
end
