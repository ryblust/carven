local examples_dir = path.join(os.projectdir(), "examples")

local cases = {
    {name = "strings", dir = "strings", output = "Hello, 世界!\nGoodbye!\nHello, 世界!\nBytes: 14, ID: 002a\n"},
    {name = "hello-world", dir = "helloworld", output = "Hello World\n"},
    {name = "receipt", dir = "basics", output = "Total in cents:\n860\n"},
    {name = "inventory", dir = "ownership",
        output = "Snapshot:\n7\nDispatched:\n9\nReplacement stock:\n1\n"},
    {name = "shipping", dir = "modules", output = "Shipping in cents:\n500\n"},
    {name = "booking", dir = "failures/basic",
        output = "Seats remaining:\n3\nNot enough seats for:\n6\n"},
    {name = "order-quote", dir = "failures/composition", output = [[
Standard delivery
Quote in cents:
800
Alternate carrier
Quote in cents:
1200
Stock shortage
Requested:
6
Available:
5
Invalid quantity
Quantity must be between 1 and 100:
0
Oversized order
Quantity must be between 1 and 100:
101
Unknown destination
Unknown zone:
9
Unavailable destination
No carrier for zone:
3
]]},
    {name = "configuration", dir = "failures/recovery", output = [[
Primary setting
Port:
443
Backup setting
Port:
9000
Built-in default
Port:
8080
Invalid primary is not hidden
Bad digit at byte:
1
Invalid backup propagates
Port must be between 1 and 65535
Zero is not a port
Port must be between 1 and 65535
]]},
    {name = "policies", dir = "failures/callbacks", output = [[
Basic policy
Accepted:
8
Rejected by basic policy
Amount must be positive:
0
Captured policy
Accepted:
4
Rejected by captured policy
Requested:
8
Policy limit:
5
Invalid input through captured policy
Amount must be positive:
-2
]]},
    {name = "native-parser", dir = "interop/import", exceptions = true,
        output = "Port:\n8080\nInvalid port\nInvalid port\nInvalid port\n"},
    {name = "cpp-host", dir = "interop/export", output = "Price in cents:\n1080\n"},
}

local names = {}
for _, case in ipairs(cases) do
    local name = "carven-example-" .. case.name
    table.insert(names, name)
    target(name)
        set_default(false)
        set_kind("binary")
        set_languages("c++20")
        add_rules("@carven/carven")

        add_includedirs(path.join(examples_dir, "support"), path.join(examples_dir, case.dir))
        add_files(path.join(examples_dir, case.dir, "*.cv"))
        if case.name == "cpp-host" then
            add_files(path.join(examples_dir, case.dir, "main.cpp"))
        end
        if case.exceptions then
            set_exceptions("cxx")
            add_cxxflags("-frtti", {tools = "clang", force = true})
            add_cxxflags("/GR", "/U_HAS_EXCEPTIONS", "/D_HAS_EXCEPTIONS=1",
                {tools = {"cl", "clang_cl"}, force = true})
        end
        add_tests("output", {
            group = "examples",
            run_timeout = 30000,
            plain = true,
            pass_outputs = {case.output, (case.output:gsub("\n", "\r\n"))},
        })
    target_end()
end

target("examples")
    set_default(false)
    set_kind("phony")
    add_deps(table.unpack(names))
