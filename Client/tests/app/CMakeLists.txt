# Los tests de app/: un ejecutable por test_*.cpp (tests/CMakeLists.txt).
# Each system tests its own command line syntax (App/CommandLinePosix.cpp or App/CommandLineWindows.cpp).
if(WIN32)
    set(other_system_test test_command_line_posix.cpp)
else()
    set(other_system_test test_command_line_windows.cpp)
endif()
mu_add_module_tests(
    OTHER_SYSTEM ${other_system_test}
    XOR_REMAP test_server_session_loopback.cpp test_server_session_xor_remap.cpp
    EXCLUDE test_make_obfuscation.cpp)

# MakeObfuscation() is the one MU_XOR_REMAP picked for Main: the test compiles the same source and expects its mask.
mu_add_test(NAME test_make_obfuscation SOURCES test_make_obfuscation.cpp ${MU_MAKE_OBFUSCATION_SOURCE})
if(MU_XOR_REMAP)
    target_link_libraries(test_make_obfuscation PRIVATE XorRemap)
    target_compile_definitions(test_make_obfuscation PRIVATE MU_TEST_INBOUND_MASK=0xAA)
else()
    target_compile_definitions(test_make_obfuscation PRIVATE MU_TEST_INBOUND_MASK=0x00)
endif()
