-- Repository executables may share public Crafts objects when both their generated
-- artifacts and native compilation configuration agree. Packaged consumers keep
-- the ordinary target-private Carven rule.
local variants = {
    {name = "cxx20-none-host"},
    {name = "cxx20-default-host", tests = "default"},
    {name = "cxx20-external-host", tests = "external"},
    {name = "cxx23-none-host", standard = "c++23"},
    {name = "cxx23-default-host", standard = "c++23", tests = "default"},
    {name = "cxx20-none-host-exceptions", exceptions = true},
    {name = "cxx23-none-host-exceptions", standard = "c++23", exceptions = true},
    {name = "cxx20-none-host-exceptions-rtti", exceptions = true, rtti = true},
    {name = "cxx20-default-portable", tests = "default", portable = true},
    {name = "cxx20-external-portable", tests = "external", portable = true},
    {name = "cxx20-external-avx2", tests = "external", avx2 = true},
    {name = "cxx20-none-host-ndebug", ndebug = true},
    {name = "cxx20-default-host-ndebug", tests = "default", ndebug = true},
}

local consumer_local = {
    -- Consumer requests affect the order of static specialization definitions in
    -- these units. Their public interfaces remain identical and are checked below.
    ["crafts/carven/std/json/syntax.cpp"] = true,
    ["crafts/carven/std/utf/block.cpp"] = true,
}

local function generated_root(target)
    return path.join(target:autogendir(), "rules", "carven")
end

local function public_headers(root)
    local files = {}
    for _, prefix in ipairs({"carven/generated/crafts/carven", "carven/api/crafts/carven"}) do
        for _, file in ipairs(os.files(path.join(root, prefix, "**.hpp"))) do
            files[path.unix(path.relative(file, root))] = file
        end
    end
    return files
end


rule("repository-crafts")
    after_load(function (target)
        if target:values("carven.shared_crafts_provider") then return end
        local name = target:name()
        local tests = target:extraconf("rules", "@carven/carven", "tests") or "none"
        local standard
        for _, language in ipairs(table.wrap(target:get("languages"))) do
            if language:startswith("c++") then standard = language end
        end
        local defines = table.wrap(target:get("defines"))
        local vectors = table.wrap(target:get("vectorexts"))
        local backend = table.contains(defines, "CARVEN_SIMD_FORCE_PORTABLE") and "portable"
            or table.contains(vectors, "avx2") and "avx2" or "host"
        local key = (standard or "c++20"):gsub("%+", "x") .. "-" .. tests .. "-" .. backend
        if table.contains(table.wrap(target:get("exceptions")), "cxx") then key = key .. "-exceptions" end
        local flags = table.wrap(target:get("cxxflags"))
        if table.contains(flags, "-frtti") or table.contains(flags, "/GR") then key = key .. "-rtti" end
        if table.contains(defines, "NDEBUG") then key = key .. "-ndebug" end
        local provider = "carven-crafts-" .. key
        import("core.project.project")
        assert(project.target(provider), "unsupported repository Crafts configuration for " .. name .. ": " .. key)
        target:extraconf_set("rules", "@carven/carven", "linkage_domain", "carven-repository-crafts:" .. key)
        target:add("deps", provider, {inherit = false})
        target:data_set("carven.shared_crafts", provider)
    end)
    after_config(function (target)
        import("core.project.project")
        local provider_name = target:data("carven.shared_crafts")
        local is_provider = target:values("carven.shared_crafts_provider")
        if not provider_name and not is_provider then return end
        if provider_name then
            local provider = assert(project.target(provider_name))
            -- Position-dependent include roots differ; code-affecting options must
            -- agree. The one extra warning checks a consumer's generated async code.
            for _, setting in ipairs({"languages", "exceptions", "defines", "undefines", "vectorexts",
                "runtimes", "optimize", "symbols", "strip", "fpmodels", "encodings", "cxflags", "cxxflags"}) do
                local function values(candidate)
                    local result = {}
                    for _, value in ipairs(table.wrap(candidate:get(setting))) do
                        if value ~= "-Werror=unused-but-set-variable" then table.insert(result, value) end
                    end
                    return table.concat(result, "\0")
                end
                assert(values(target) == values(provider), "repository Crafts configuration mismatch: "
                    .. target:name() .. " and " .. provider_name .. " differ in " .. setting)
            end
        end
        local root = generated_root(target)
        for _, source in ipairs(os.files(path.join(os.projectdir(), "crafts", "carven", "**.cv"))) do
            local relative = path.unix(path.relative(source, os.projectdir())):gsub("%.cv$", ".cpp")
            if (is_provider and consumer_local[relative]) or (provider_name and not consumer_local[relative]) then
                target:remove("files", path.join(root, relative))
            end
        end
        if is_provider then
            target:remove("files", path.join(root, "carven", "generated", "carven-test-main.cpp"))
        else
            for _, source in ipairs(os.files(path.join(os.projectdir(), "crafts", "carven", "**.cpp"))) do
                target:remove("files", source)
            end
        end
    end)
    before_build(function (target)
        -- The global prepare phase has generated both complete batches before this
        -- check can permit the consumer's compilation and link jobs to proceed.
        local provider_name = target:data("carven.shared_crafts")
        if not provider_name then return end
        import("core.project.project")
        local provider = assert(project.target(provider_name))
        local provider_root, consumer_root = generated_root(provider), generated_root(target)
        local expected, actual = public_headers(provider_root), public_headers(consumer_root)
        local context = "Crafts object reuse for " .. target:name() .. " with " .. provider_name .. ": "
        local function equal_file(relative, left, right)
            assert(left and right and os.isfile(left) and os.isfile(right),
                context .. "missing public artifact " .. relative)
            assert(io.readfile(left, {encoding = "binary"}) == io.readfile(right, {encoding = "binary"}),
                context .. "public artifact differs: " .. relative
                    .. "; this source cannot use the repository shared Crafts configuration")
        end
        local function check_includes(file)
            local source = io.readfile(file)
            for include in source:gmatch('#include%s*[<"]([^>"\n]+)[>"]') do
                if include:startswith("carven/generated/") or include:startswith("carven/api/") then
                    assert(expected[include], context .. "shared artifact depends on a private header: " .. include)
                end
            end
        end
        assert(not table.empty(expected), context .. "provider generated no public headers")
        for relative, file in pairs(expected) do
            equal_file(relative, file, actual[relative])
            check_includes(file)
        end
        for relative in pairs(actual) do
            assert(expected[relative], context .. "consumer adds a public header: " .. relative)
        end
        for _, source in ipairs(os.files(path.join(os.projectdir(), "crafts", "carven", "**.cv"))) do
            local relative = path.unix(path.relative(source, os.projectdir())):gsub("%.cv$", ".cpp")
            if not consumer_local[relative] then
                local file = path.join(provider_root, relative)
                equal_file(relative, file, path.join(consumer_root, relative))
                check_includes(file)
            end
        end
    end)
rule_end()

for _, variant in ipairs(variants) do
    target("carven-crafts-" .. variant.name)
        set_default(false)
        set_kind("object")
        set_languages(variant.standard or "c++20")
        set_values("carven.shared_crafts_provider", true)
        add_rules("repository-crafts")
        add_rules("@carven/carven", {tests = variant.tests,
            linkage_domain = "carven-repository-crafts:" .. variant.name})
        if variant.portable then add_defines("CARVEN_SIMD_FORCE_PORTABLE") end
        if variant.avx2 then add_vectorexts("avx2") end
        if variant.ndebug then add_defines("NDEBUG") end
        if variant.exceptions then set_exceptions("cxx") end
        if variant.rtti then
            add_cxxflags("-frtti", {tools = "clang", force = true})
            add_cxxflags("/GR", "/U_HAS_EXCEPTIONS", "/D_HAS_EXCEPTIONS=1",
                {tools = {"cl", "clang_cl"}, force = true})
        end
    target_end()
end
