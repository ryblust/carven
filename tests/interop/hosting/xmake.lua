target("carven-test-hosting-business-code")
    set_default(false)
    set_kind("static")
    set_languages("c++20")
    add_rules("@carven/carven")
    set_policy("build.fence", true)
    add_includedirs(".", {public = true})
    add_files("document_pair.cv", "business.cv", "document.cv", "document_provider.cpp", "enumeration.cv", "enumeration_provider.cpp")
target_end()

target("carven-test-hosting")
    set_default(false)
    set_kind("binary")
    set_languages("c++26")
    add_deps("carven-modules")
    add_files("main.cpp", {languages = "c++23"})
    add_tests("scalar", {
        group = "interop", run_timeout = 30000, plain = true,
        pass_outputs = {"scalar: 9, 21; rejected, recovered\n", "scalar: 9, 21; rejected, recovered\r\n"},
    })
target_end()

target("carven-test-hosting-business")
    set_default(false)
    set_kind("binary")
    set_languages("c++26")
    add_deps("carven-modules")
    add_deps("carven-test-hosting-business-code")
    add_files("business.cpp", "document.cpp", "document_pair.cpp", "enumeration.cpp", {languages = "c++23"})
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
    add_rules("@carven/carven")
    set_policy("build.fence", true)
    add_includedirs(".", {public = true})
    add_files("preview.cv", "preview_provider.cpp")
target_end()

target("carven-test-hosting-preview")
    set_default(false)
    set_kind("binary")
    set_languages("c++26")
    add_deps("carven-modules")
    add_deps("carven-test-hosting-preview-code")
    add_files("preview.cpp", {languages = "c++23"})
    add_tests("event-loop", {
        group = "interop", run_timeout = 30000, plain = true,
        pass_outputs = {"preview: document edits -> reload -> selected result; old workflow completes; scoped teardown\n",
            "preview: document edits -> reload -> selected result; old workflow completes; scoped teardown\r\n"},
    })
target_end()
