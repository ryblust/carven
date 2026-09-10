import("xmake.benchmark", {rootdir = os.projectdir()})

function main()
    local compiler, samples, warmups = benchmark.settings()
    local cases = {}
    for _, count in ipairs({64, 128, 256}) do
        local independent, chain, reversed = {}, {}, {}
        for index = 0, count - 1 do
            table.insert(independent, string.format("fn f%d(x: i32) -> i32 { return x; }", index))
            table.insert(chain, string.format("fn f%d(x: i32) -> i32 { return %s; }", index,
                index + 1 < count and string.format("f%d(x)", index + 1) or "x"))
        end
        for index = #chain, 1, -1 do
            table.insert(reversed, chain[index])
        end
        for _, form in ipairs({{"independent", independent}, {"caller_first", chain}, {"callee_first", reversed}}) do
            table.insert(cases, {name = form[1] .. "_" .. count,
                source = table.concat(form[2], "\n") .. "\nfn main() { let _ = f0(1); }\n"})
        end
    end
    for _, count in ipairs({128, 256, 512, 1024}) do
        for _, distinct in ipairs({false, true}) do
            local functions = {}
            for index = 0, count - 1 do
                table.insert(functions, string.format("fn f%d() -> i32 { return %d; }",
                    index, distinct and index or 1))
            end
            table.insert(cases, {name = (distinct and "distinct_constants_" or "repeated_constants_") .. count,
                source = table.concat(functions, "\n") .. "\nfn main() { let _ = f0(); }\n"})
        end
    end
    for _, depth in ipairs({4, 8, 12, 16}) do
        local body = "x += 1;"
        for _ = 1, depth do
            body = "while flag { " .. body .. " break; }"
        end
        table.insert(cases, {name = "terminating_loops_" .. depth,
            source = "fn probe(flag: bool, &x: i32) { " .. body
                .. " }\nfn main() { var x = 0; probe(false, &x); }\n"})
    end
    for _, count in ipairs({64, 128, 256}) do
        local parameters, reads, effects, calls = {}, {}, {}, {}
        for index = 1, count do
            table.insert(parameters, "_: i32")
            table.insert(reads, "value")
            table.insert(effects, index % 4 == 0 and "step(&value)" or "value")
            table.insert(calls, "operation(flag)?;")
        end
        for _, form in ipairs({{"wide_reads", reads}, {"wide_effects", effects}}) do
            table.insert(cases, {name = form[1] .. "_" .. count,
                source = "fn collect(" .. table.concat(parameters, ", ") .. ") {}\n"
                    .. "fn step(&value: i32) -> i32 { value += 1; return value; }\n"
                    .. "fn main() { var value = 1; collect(" .. table.concat(form[2], ", ") .. "); }\n"})
        end
        table.insert(cases, {name = "fallible_roots_" .. count,
            source = "enum Error { Failed, }\n"
                .. "fn operation(flag: bool) -> i32 throw Error { if flag { return 1; } throw Error::Failed; }\n"
                .. "fn probe(flag: bool) throw Error { " .. table.concat(calls, " ") .. " }\n"
                .. "fn main() { try { probe(true)?; } catch { Error(_) => {}, } }\n"})
    end
    for _, depth in ipairs({16, 32, 64}) do
        local expression = "value"
        for _ = 1, depth do expression = "identity(" .. expression .. ")" end
        table.insert(cases, {name = "nested_expression_" .. depth,
            source = "fn identity(value: i32) -> i32 { return value; }\n"
                .. "fn probe(value: i32) -> i32 { return " .. expression .. "; }\nfn main() { probe(1); }\n"})
    end
    print("End-to-end source-to-C++ timings, including process startup and generation.")
    benchmark.temporary(function (root)
        local errors = path.join(root, "stderr.txt")
        for _, case in ipairs(cases) do
            local filename = case.name .. ".cv"
            io.writefile(path.join(root, filename), case.source)
            local results = benchmark.measure(samples, warmups, function ()
                local status = os.execv(compiler, {"--stdout", "--linkage-domain=analysis-pulse", filename}, {
                    curdir = root, stdout = os.nuldev(), stderr = errors, timeout = 60000,
                })
                if status ~= 0 then
                    raise("%s: %s", case.name, io.readfile(errors) or "compiler failed")
                end
            end)
            print("%24s: %9.2f ms", case.name, benchmark.median(results))
        end
    end)
end
