local function publish_generated_clang_tidy(target)
    if not target:rule("carven") then return end
    local root = target:data("carven.generated_root")
    if not root then return end
    -- Keep the base profile adjacent so custom output directories also
    -- inherit it, without applying generated-code exceptions to siblings.
    os.cp(path.join(os.projectdir(), ".clang-tidy"),
          path.join(path.directory(root), ".clang-tidy"), {copy_if_different = true})
    os.cp(path.join(os.projectdir(), "xmake", "generated.clang-tidy"),
          path.join(root, ".clang-tidy"), {copy_if_different = true})
end

rule("carven-repository")
    on_config(function (target)
        if not target:rule("carven") then return end
        import("core.project.project")
        target:extraconf_set("rules", "carven", "toolchain", {
            program = path.absolute(project.target("carven"):targetfile(), os.projectdir()),
            crafts = path.join(os.projectdir(), "crafts"),
        })
    end)

    after_config(publish_generated_clang_tidy)
    -- Generation cleans its output inventory. Restore build-owned profiles
    -- after preparation, including a first build or changed semantic inputs.
    after_prepare(publish_generated_clang_tidy)
rule_end()
