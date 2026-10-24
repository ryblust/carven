target("carven-test-hosting-business-code")
    set_default(false)
    set_kind("static")
    set_languages("c++20")
    add_rules("carven")
    add_includedirs(".", {public = true})
    add_files("document_pair.cv", "business.cv", "document.cv", "document_provider.cpp", "enumeration.cv", "enumeration_provider.cpp")
target_end()

target("carven-test-hosting")
    set_default(false)
    set_kind("binary")
    set_languages("c++23")
    add_deps("carven-hosting")
    add_files("main.cpp")
    add_tests("scalar", {
        group = "interop", run_timeout = 30000, plain = true,
        pass_outputs = {"scalar: 9, 21; rejected, recovered\n", "scalar: 9, 21; rejected, recovered\r\n"},
    })
target_end()

target("carven-test-hosting-business")
    set_default(false)
    set_kind("binary")
    set_languages("c++23")
    add_deps("carven-hosting")
    add_deps("carven-test-hosting-business-code")
    add_files("business.cpp", "document.cpp", "document_pair.cpp", "enumeration.cpp")
    add_tests("workflow", {
        group = "interop", run_timeout = 30000, plain = true,
        pass_outputs = {"business: review -> empty record -> reload -> accepted; 3 submissions\ndocument: events -> reload -> revoke -> destroy during await; detached safely\ndocuments: render replaced; identity compiled; same calls, subscriptions, and objects\n",
            "business: review -> empty record -> reload -> accepted; 3 submissions\r\ndocument: events -> reload -> revoke -> destroy during await; detached safely\r\ndocuments: render replaced; identity compiled; same calls, subscriptions, and objects\r\n"},
    })
target_end()

target("carven-test-hosting-preview-code")
    set_default(false)
    set_kind("static")
    set_languages("c++20")
    add_rules("carven")
    add_includedirs(".", {public = true})
    add_files("preview.cv", "preview_provider.cpp")
target_end()

target("carven-test-hosting-preview")
    set_default(false)
    set_kind("binary")
    set_languages("c++23")
    add_deps("carven-hosting")
    add_deps("carven-test-hosting-preview-code")
    add_files("preview.cpp")
    add_tests("event-loop", {
        group = "interop", run_timeout = 30000, plain = true,
        pass_outputs = {"preview: document edits -> reload -> selected result; old workflow completes; scoped teardown\n",
            "preview: document edits -> reload -> selected result; old workflow completes; scoped teardown\r\n"},
    })
target_end()

-- These consumers use the public headers and static library, with no BMI imports.
for _, name in ipairs({"carven-test-hosting", "carven-test-hosting-business", "carven-test-hosting-preview"}) do
    target(name)
        on_load(function (target)
            target:rule_enable("c++.build.modules", false)
            target:rule_enable("c++.build.modules.scanner", false)
            target:rule_enable("c++.build.modules.builder", false)
        end)
    target_end()
end
