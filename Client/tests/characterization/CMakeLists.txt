# The behavior golden (refactor plan, AR0.3): each scene of scenes/ runs the whole client, frame by frame, and its
# FrameDigest list must match golden/<scene>.tsv.
mu_add_module_tests()
target_compile_definitions(test_characterization PRIVATE MU_CHARACTERIZATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
