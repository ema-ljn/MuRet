# 0008. El wrap `GL_CLAMP` del original va como `GL_CLAMP_TO_EDGE`

- **Estado:** aceptado. Es la recomendación de Claude en el plan de la CL5 ("Lo que más puede
  morder"), con la que se hizo la CL5.1 (2026-11-02).
- **Fila:** Q-C73 del [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md).
- **Código:** `Client::Data::Textures::Wrap` (`Client/src/Data/Textures/Sampling.h`) y la subida de
  `Client::Render::Gl::GlTexture` (`Client/src/Render/Gl/GlTexture.cpp`).

## Contexto

El original sube cada textura con un filtro y un wrap, el cuarto argumento de `0x63b870` y
`0x63bd00`. El wrap es `GL_REPEAT` o `0x2900`, `GL_CLAMP`, de la función fija de OpenGL 1.x. Con
`GL_LINEAR`, `GL_CLAMP` mezcla el último medio texel de cada borde con el color del borde. El
binario importa solo `glTexParameteri` de esa familia, así que nunca cambia ese color, que queda en
el de por defecto, (0, 0, 0, 0): el borde de la imagen sale más oscuro y, en una textura con alfa,
más transparente. Es *estático*, por las importaciones; cómo se ve depende del driver, y está *a
confirmar* en una captura. Con `GL_NEAREST` no cambia nada, porque el texel elegido nunca es el del
borde.

Pasa con las texturas que usan `GL_LINEAR` y `0x2900`: dos de las 18 del login y cinco de las seis
de la carga.

MuRet dibuja con **OpenGL 3.3 core** (D-C3 del
[roadmap](../superpowers/specs/2026-10-30-cliente-design.md), decidido por Emanuel el 2026-10-30),
y ese perfil no tiene `GL_CLAMP`: pedirlo da `GL_INVALID_ENUM`.

## Decisión

`Wrap::Clamp` sube la textura con `GL_CLAMP_TO_EDGE`, que repite el texel del borde y no lo mezcla
con nada. Con `GL_NEAREST` da lo mismo que el original. Con `GL_LINEAR`, no.

El perfil core tiene otro wrap, `GL_CLAMP_TO_BORDER`, que con el color del borde por defecto da lo
que dice la especificación de `GL_CLAMP` mientras las coordenadas vayan de 0 a 1. Se eligió
`GL_CLAMP_TO_EDGE` porque muchos drivers de Windows trataban `GL_CLAMP` así (el plan de la CL5), y
sin una captura no se sabe cuál de los dos veía el jugador.

Si una captura del original muestra el borde oscuro, esas texturas pasan a `GL_CLAMP_TO_BORDER`,
sin tocar el shader, y este ADR pasa a reemplazado por el que lo decida.

## Consecuencias

- **Se gana:** el perfil core entero, sin una extensión ni un contexto de compatibilidad, y una
  subida que no da un error de GL. El borde no se mezcla con negro, como en esos drivers.
- **Se pierde:** si el original se veía como dice la especificación, en las siete texturas con
  `GL_LINEAR` y `0x2900` el último medio texel de cada borde se ve más claro que en el original y,
  en una con alfa, más opaco. Ningún test lo compara con el original: falta la captura.
- **Lo fija:** `GlTextureTest.RepeatWrapsAndClampDoesNot`, en el label `gl` (con ventana, Xvfb y
  Mesa en CI): con la u o la v de 0 a 2, `Repeat` dibuja la textura dos veces y `Clamp` estira el
  último texel, en los dos ejes. Con `GL_CLAMP_TO_BORDER` daría el color del borde pasado el 1, así
  que el test también los distingue.

El contrato está en la sub-ola CL5.1 del
[plan de la CL5](../superpowers/plans/2026-11-02-ola-cl5-render-2d-y-login.md).
