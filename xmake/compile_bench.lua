import("xmake.benchmark", {rootdir = os.projectdir()})

function batch(compiler, count, samples, warmups)
    return benchmark.temporary(function (root)
        local inputs = {}
        for index = 0, count - 1 do
            local filename = string.format("module_%03d.cv", index)
            io.writefile(path.join(root, filename), string.format("export struct Value%03d { value: i32, }\n", index))
            table.insert(inputs, filename)
        end
        return benchmark.measure(samples, warmups, function (ordinal)
            os.iorunv(compiler, table.join({"compile", "--output-dir", "out-" .. ordinal,
                "--linkage-domain=benchmark:compile:" .. count}, inputs), {curdir = root})
        end)
    end)
end

function result_header()
    print("%-7s %-27s %-16s %12s", "Run", "Scenario", "Size", "Median (ms)")
end

function start_result(index, total, label, size)
    io.write(string.format("[%2d/%2d] %-27s %-16s ", index, total, label, size))
    io.flush()
end

function finish_result(results)
    print("%12.2f", benchmark.median(results))
    io.flush()
end

function workloads()
    local groups = {}
    local cases = {}
    table.insert(groups, {title = "Functions and call chains", cases = cases})
    for _, count in ipairs({16, 64, 256}) do
        local independent, chain, reversed = {}, {}, {}
        for index = 0, count - 1 do
            table.insert(independent, string.format("fn f%d(x: i32) -> i32 { return x; }", index))
            table.insert(chain, string.format("fn f%d(x: i32) -> i32 { return %s; }", index,
                index + 1 < count and string.format("f%d(x)", index + 1) or "x"))
        end
        for index = #chain, 1, -1 do
            table.insert(reversed, chain[index])
        end
        for _, form in ipairs({{"independent", "Independent functions", independent},
            {"caller_first", "Caller declared first", chain},
            {"callee_first", "Callee declared first", reversed}}) do
            table.insert(cases, {name = form[1] .. "_" .. count, label = form[2], size = count .. " functions",
                source = table.concat(form[3], "\n") .. "\nfn main() { let _ = f0(1); }\n"})
        end
    end
    do
        local count = 64
        local stops = {}
        for index = 0, count - 1 do
            table.insert(stops, string.format("fn stop%d() -> void { %s; }", index,
                index + 1 < count and string.format("stop%d()", index + 1) or "fail()"))
        end
        table.insert(cases, {name = "test_stop_chain_" .. count, label = "Test-stop call chain", size = count .. " functions",
            source = table.concat(stops, "\n") .. "\nfn main() { stop0(); }\n"})
    end
    cases = {}
    table.insert(groups, {title = "Shared dependencies", cases = cases})
    for _, depth in ipairs({8, 16, 24}) do
        local declarations = {"struct N0 { value: i32 }"}
        for index = 1, depth do
            table.insert(declarations, string.format("struct N%d { left: N%d, right: N%d }",
                index, index - 1, index - 1))
        end
        table.insert(declarations, string.format("fn inspect(value: N%d) {}", depth))
        table.insert(cases, {name = "shared_nominal_" .. depth, label = "Nominal fields", size = "depth " .. depth,
            source = table.concat(declarations, "\n")})
    end
    for _, depth in ipairs({4, 8, 12}) do
        local statements = {"let x0 = values[0];"}
        for index = 1, depth do
            table.insert(statements, string.format("let x%d = x%d + x%d;", index, index - 1, index - 1))
        end
        table.insert(cases, {name = "shared_native_query_" .. depth, label = "Native result queries", size = "depth " .. depth,
            source = "import <vector> using std::vector;\nfn probe(values: vector<i32>) { "
                .. table.concat(statements, " ") .. string.format(" return x%d; }\n", depth)})
    end
    cases = {}
    table.insert(groups, {title = "Pattern coverage", cases = cases})
    for _, count in ipairs({16, 64, 256}) do
        local members, arms = {}, {}
        for index = 0, count - 1 do
            table.insert(members, string.format("Case%d,", index))
            table.insert(arms, string.format(".Case%d => %d,", index, index))
        end
        table.insert(cases, {name = "wide_enum_" .. count, label = "Enum cases", size = count .. " cases",
            source = "enum Value { " .. table.concat(members, " ") .. " }\n"
                .. "fn probe(value: Value) -> i32 { return match value { "
                .. table.concat(arms, " ") .. " }; }\n"})
    end
    for _, width in ipairs({4, 8, 12}) do
        local types, arms = {}, {}
        for _ = 1, width do table.insert(types, "bool") end
        -- Each arm constrains one field; the other fields must stay unconstrained.
        for selected = 1, width do
            local fields = {}
            for index = 1, width do
                table.insert(fields, index == selected and "false" or "_")
            end
            table.insert(arms, ".Bits(" .. table.concat(fields, ", ") .. ") => " .. selected .. ",")
        end
        table.insert(arms, "_ => 0,")
        table.insert(cases, {name = "independent_payload_" .. width, label = "Independent payload fields",
            size = width .. " fields/arms",
            source = "enum Value { Bits(" .. table.concat(types, ", ") .. "), }\n"
                .. "fn probe(value: Value) -> i32 { return match value { "
                .. table.concat(arms, " ") .. " }; }\n"})
    end
    cases = {}
    table.insert(groups, {title = "Constants", cases = cases})
    do
        local count = 256
        for _, distinct in ipairs({false, true}) do
            local functions = {}
            for index = 0, count - 1 do
                table.insert(functions, string.format("fn f%d() -> i32 { return %d; }",
                    index, distinct and index or 1))
            end
            table.insert(cases, {name = (distinct and "distinct_constants_" or "repeated_constants_") .. count,
                label = distinct and "Distinct constants" or "Repeated constants", size = count .. " functions",
                source = table.concat(functions, "\n") .. "\nfn main() { let _ = f0(); }\n"})
        end
    end
    cases = {}
    table.insert(groups, {title = "Control flow and expressions", cases = cases})
    for _, count in ipairs({128, 4096}) do
        local operands = {}
        for _ = 1, count do table.insert(operands, "operand(value)") end
        table.insert(cases, {name = "operand_chain_" .. count, label = "Runtime operand chain", size = count .. " operands",
            source = "fn operand(value: i32) -> i32 { return value; }\n"
                .. "fn chain(value: i32) -> i32 { return " .. table.concat(operands, " + ") .. "; }\n"})
    end
    do
        local depth = 8
        local body = "x += 1;"
        for _ = 1, depth do
            body = "while flag { " .. body .. " break; }"
        end
        table.insert(cases, {name = "terminating_loops_" .. depth, label = "Loops with break", size = "depth " .. depth,
            source = "fn probe(flag: bool, &x: i32) { " .. body
                .. " }\nfn main() { var x = 0; probe(false, &x); }\n"})
    end
    do
        local count = 128
        local parameters, reads, effects, calls = {}, {}, {}, {}
        for index = 1, count do
            table.insert(parameters, "_: i32")
            table.insert(reads, "value")
            table.insert(effects, index % 4 == 0 and "step(&value)" or "value")
            table.insert(calls, "operation(flag)?;")
        end
        for _, form in ipairs({{"wide_reads", "Argument reads", reads}, {"wide_effects", "Argument side effects", effects}}) do
            table.insert(cases, {name = form[1] .. "_" .. count, label = form[2], size = count .. " arguments",
                source = "fn collect(" .. table.concat(parameters, ", ") .. ") {}\n"
                    .. "fn step(&value: i32) -> i32 { value += 1; return value; }\n"
                    .. "fn main() { var value = 1; collect(" .. table.concat(form[3], ", ") .. "); }\n"})
        end
        table.insert(cases, {name = "fallible_roots_" .. count, label = "Error propagation", size = count .. " calls",
            source = "enum Error { Failed, }\n"
                .. "fn operation(flag: bool) -> i32 throw Error { if flag { return 1; } throw Error::Failed; }\n"
                .. "fn probe(flag: bool) throw Error { " .. table.concat(calls, " ") .. " }\n"
                .. "fn main() { try { probe(true)?; } catch { Error(_) => {}, } }\n"})
    end
    do
        local depth = 32
        local expression = "value"
        for _ = 1, depth do expression = "identity(" .. expression .. ")" end
        table.insert(cases, {name = "nested_expression_" .. depth, label = "Nested expressions", size = "depth " .. depth,
            source = "fn identity(value: i32) -> i32 { return value; }\n"
                .. "fn probe(value: i32) -> i32 { return " .. expression .. "; }\nfn main() { probe(1); }\n"})
    end
    return groups
end

function main()
    local compiler, samples, warmups = benchmark.settings("compile", 3)
    local groups = workloads()
    local batches = {16, 128}
    local total = #batches
    for _, group in ipairs(groups) do total = total + #group.cases end
    print("Includes process startup, analysis and C++ generation; excludes native C++ compilation.")
    print("Module batches write fresh files; structured cases send inspection output to the null device.")
    print("\nModule batches")
    result_header()
    for index, count in ipairs(batches) do
        start_result(index, total, "Independent modules", count .. " modules")
        local results = batch(compiler, count, samples, warmups)
        finish_result(results)
        benchmark.details("modules_" .. count, results)
    end
    benchmark.temporary(function (root)
        local errors = path.join(root, "stderr.txt")
        local ordinal = #batches
        for _, group in ipairs(groups) do
            print("\n%s", group.title)
            result_header()
            for _, case in ipairs(group.cases) do
                ordinal = ordinal + 1
                start_result(ordinal, total, case.label, case.size)
                local filename = case.name .. ".cv"
                io.writefile(path.join(root, filename), case.source)
                local results = benchmark.measure(samples, warmups, function ()
                    local status = os.execv(compiler, {"compile", "--stdout", "--linkage-domain=benchmark:compile", filename}, {
                        curdir = root, stdout = os.nuldev(), stderr = errors, timeout = 60000,
                    })
                    if status ~= 0 then
                        raise("%s: %s", case.name, io.readfile(errors) or "compiler failed")
                    end
                end)
                finish_result(results)
                benchmark.details(case.name, results)
            end
        end
    end)
    print("\nComplete. Use --verbose to show individual samples and case identifiers.")
end
