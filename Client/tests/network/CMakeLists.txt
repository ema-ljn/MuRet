# Los tests de network/: un ejecutable por test_*.cpp (tests/CMakeLists.txt).
mu_add_module_tests(XOR_REMAP
    test_connection_xor_remap.cpp
    test_golden_connection_xor_remap.cpp
    test_golden_encrypted_packet_xor_remap.cpp
    test_golden_xor_remap_obfuscation.cpp
    test_xor_remap_obfuscation.cpp)
