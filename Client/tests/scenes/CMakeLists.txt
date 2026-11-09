# Los tests de scenes/: un ejecutable por test_*.cpp (tests/CMakeLists.txt).
muret_add_module_tests()

# Este fixture de escena consume el GlFrameTarget real además de sus observadores CPU.
target_link_libraries(test_particle_runtime_integration PRIVATE ClientGlTestSupport)
