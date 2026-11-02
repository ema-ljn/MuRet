# Los tests de platform/: lo de ClientPlatform que se prueba sin ventana. muret_add_module_tests()
# no pasa LINK_LIBS, así que cada archivo se registra a mano y enlaza también ClientPlatform (y
# con él SDL3). Un archivo nuevo acá necesita su línea: si falta, el configure lo nombra.
muret_add_test(NAME test_gl_loader SOURCES test_gl_loader.cpp LINK_LIBS ClientPlatform)
muret_add_test(NAME test_swap_interval SOURCES test_swap_interval.cpp LINK_LIBS ClientPlatform)
