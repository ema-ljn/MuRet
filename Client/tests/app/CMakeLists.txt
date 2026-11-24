# Los tests de app/: un ejecutable por test_*.cpp (tests/CMakeLists.txt).
# Each system tests its own command line syntax (App/CommandLinePosix.cpp or App/CommandLineWindows.cpp).
if(WIN32)
    mu_add_module_tests(OTHER_SYSTEM test_command_line_posix.cpp)
else()
    mu_add_module_tests(OTHER_SYSTEM test_command_line_windows.cpp)
endif()
