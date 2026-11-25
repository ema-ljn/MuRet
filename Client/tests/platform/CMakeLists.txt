# Los tests de platform/: lo de Platform que se prueba sin ventana, salvo test_window. mu_add_module_tests()
# no pasa LINK_LIBS, así que cada archivo se registra a mano y enlaza también Platform. A test that calls SDL itself
# links SDL3 too, and test_sdl_audio links Audio. Un archivo nuevo acá necesita su línea: si falta, el configure lo
# nombra.
mu_add_test(NAME test_gl_loader SOURCES test_gl_loader.cpp LINK_LIBS Platform SDL3::SDL3)
mu_add_test(NAME test_swap_interval SOURCES test_swap_interval.cpp LINK_LIBS Platform)
mu_add_test(NAME test_window_events SOURCES test_window_events.cpp LINK_LIBS Platform SDL3::SDL3)
mu_add_test(NAME test_input_events SOURCES test_input_events.cpp LINK_LIBS Platform SDL3::SDL3)
mu_add_test(NAME test_sdl_log SOURCES test_sdl_log.cpp LINK_LIBS Platform SDL3::SDL3)
mu_add_test(NAME test_system_fonts SOURCES test_system_fonts.cpp LINK_LIBS Platform)
mu_add_test(NAME test_clipboard SOURCES test_clipboard.cpp LINK_LIBS Platform SDL3::SDL3)
# La ventana de verdad (CL5.5): necesita pantalla, así que va con la etiqueta gl y el tope de los
# de render_gl/, y enlaza GlTestSupport por la regla de MU_CLIENT_REQUIRE_GL.
mu_add_test(NAME test_window SOURCES test_window.cpp LINK_LIBS Platform GlTestSupport LABEL gl
    TIMEOUT 60)
mu_add_test(NAME test_sdl_audio SOURCES test_sdl_audio.cpp LINK_LIBS Audio SDL3::SDL3)
