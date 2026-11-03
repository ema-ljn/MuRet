# Los tests de platform/: lo de ClientPlatform que se prueba sin ventana, salvo test_window. muret_add_module_tests()
# no pasa LINK_LIBS, así que cada archivo se registra a mano y enlaza también ClientPlatform (y
# con él SDL3). Un archivo nuevo acá necesita su línea: si falta, el configure lo nombra.
muret_add_test(NAME test_gl_loader SOURCES test_gl_loader.cpp LINK_LIBS ClientPlatform)
muret_add_test(NAME test_swap_interval SOURCES test_swap_interval.cpp LINK_LIBS ClientPlatform)
muret_add_test(NAME test_window_events SOURCES test_window_events.cpp LINK_LIBS ClientPlatform)
muret_add_test(NAME test_input_events SOURCES test_input_events.cpp LINK_LIBS ClientPlatform)
muret_add_test(NAME test_sdl_log SOURCES test_sdl_log.cpp LINK_LIBS ClientPlatform)
muret_add_test(NAME test_system_fonts SOURCES test_system_fonts.cpp LINK_LIBS ClientPlatform)
# La ventana de verdad (CL5.5): necesita pantalla, así que va con la etiqueta gl y el tope de los
# de render_gl/, y enlaza ClientGlTestSupport por la regla de MURET_CLIENT_REQUIRE_GL.
muret_add_test(NAME test_window SOURCES test_window.cpp LINK_LIBS ClientPlatform ClientGlTestSupport LABEL gl
    TIMEOUT 60)
