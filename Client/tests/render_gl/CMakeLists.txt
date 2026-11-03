# Los tests de render_gl/: lo de ClientRender que habla con OpenGL (CL5.1). Los que dibujan abren
# una ventana escondida con un contexto 3.3 core (support/GlTestContext.h), dibujan en un
# framebuffer de 64 × 64 y leen los píxeles. Sin pantalla o sin 3.3 se saltean, salvo con
# MURET_CLIENT_REQUIRE_GL=1, que los hace fallar: el paso de Xvfb del CI corre `ctest -L '^gl$'`
# con esa variable (pregunta 9 del plan de la CL5). Llevan la etiqueta gl y 60 s cada uno, como los
# del lab de la CL5.0: un driver colgado en Xvfb falla el test en vez de frenar el CI.
muret_add_module_tests(LINK_LIBS ClientGlTestSupport LABEL gl TIMEOUT 60)
