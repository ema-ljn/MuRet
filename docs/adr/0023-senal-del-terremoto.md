# 0023. Señal propia para el pulso del terremoto

**Estado** · Propuesto en CL11.3 (2026-11-18).

## Contexto

En la vida 11 del terremoto de estado 0, el alta de `0x103/0`
(`0x521f6c`) precede a las grietas. El paso `0x521253` desplaza el
pulso y reduce la luz del terreno alrededor de su posición. La acción
original tiene una sola clave y su paso genérico de animación vuelve
sin modificar el cuadro.

## Decisión

El pulso ocupa un lugar del pool común durante 15 pasos. Los dos pases
de dibujo conservan su filtro y representan el pulso con la señal de
área que ya usa MuRet. La señal se ubica en la posición del efecto y usa
el brillo de malla en su intensidad. La geometría y el material difieren
del original.

## Consecuencias

Las pruebas observan el orden de altas, los sorteos, las dos fases del
paso, el campo de luz dinámica, la liberación del lugar y el dibujo sin
malla. El contraste en pantalla queda para la VM.
