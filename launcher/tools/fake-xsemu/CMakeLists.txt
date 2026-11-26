# A stand-in for XSEmu.exe with the command line and JSON lines of docs/launcher-protocol.md, so the
# launcher can be developed and tested without the emulator or a game. Draws an original abstract
# test scene; no game content.
add_executable(fake-xsemu main.cpp FakeRun.cpp FakeRun.h FakeWarmup.cpp FakeWarmup.h Scene.cpp Scene.h)
target_link_libraries(fake-xsemu PRIVATE Qt6::Gui Qt6::Network)
set_target_properties(fake-xsemu PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
xsemu_warnings(fake-xsemu)
