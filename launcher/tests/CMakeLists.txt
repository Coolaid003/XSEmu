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

if(XSEMU_FAKE_EMULATOR)
    xsemu_test(tst_controllers)
    add_dependencies(tst_controllers fake-xsemu)
    target_compile_definitions(tst_controllers PRIVATE XSEMU_FAKE_EMULATOR_PATH="$<TARGET_FILE:fake-xsemu>")
endif()
