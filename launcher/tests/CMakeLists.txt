find_package(Qt6 REQUIRED COMPONENTS Test)

function(xsemu_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE xsemu_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        XSEMU_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        XSEMU_PROTOCOL_DOC="${CMAKE_CURRENT_SOURCE_DIR}/../../docs/launcher-protocol.md")
    xsemu_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

xsemu_test(tst_protocol)
xsemu_test(tst_titleconfig)
xsemu_test(tst_library)
xsemu_test(tst_coverart)
xsemu_test(tst_shadercache)
xsemu_test(tst_settings)
xsemu_test(tst_runners)

if(WIN32)
    # a window from another process reparented into a Qt host: needs the real windows platform
    add_executable(reparent-helper WIN32 reparent-helper.cpp)
    xsemu_warnings(reparent-helper)
    xsemu_test(tst_reparent)
    target_link_libraries(tst_reparent PRIVATE Qt6::Widgets user32)
    add_dependencies(tst_reparent reparent-helper)
    target_compile_definitions(tst_reparent PRIVATE XSEMU_REPARENT_HELPER="$<TARGET_FILE:reparent-helper>")
    set_tests_properties(tst_reparent PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
endif()

if(XSEMU_FAKE_EMULATOR)
    xsemu_test(tst_controllers)
    add_dependencies(tst_controllers fake-xsemu)
    target_compile_definitions(tst_controllers PRIVATE XSEMU_FAKE_EMULATOR_PATH="$<TARGET_FILE:fake-xsemu>")

    # The in-game view end to end: real clicks on the shipped widgets, fake-xsemu hosted in the view.
    # Runs offscreen by default; CI also runs it on Xvfb so the game window is a real embedded child.
    xsemu_test(tst_ingame)
    target_link_libraries(tst_ingame PRIVATE xsemu_ui)
    add_dependencies(tst_ingame fake-xsemu)
    target_compile_definitions(tst_ingame PRIVATE XSEMU_FAKE_EMULATOR_PATH="$<TARGET_FILE:fake-xsemu>")
endif()
