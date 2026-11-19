# 0024. Señal de MuRet para la estela del tiro penetrante

**Estado** · Propuesto en CL11.4.

## Contexto

El tiro penetrante del arco corto crea la estela `0x105/0`. Sus cuatro
joints `0x610/0..3` usan la posición actual y la partida anterior de la
estela. El impacto intenta `0x60f/6` y la flecha sigue viva.

## Decisión

Conservar la reserva, el orden, los sorteos y la caducidad de la estela y
sus hijos. Los dos pases de dibujo conservan su filtro. La estela usa la
señal de área de MuRet, con una geometría y un material distintos de los
del modelo original. Los joints usan el dibujo común existente.

## Consecuencias

Las pruebas cubren la creación al disparar, los dos puntos de impacto,
la saturación, el paso de los hijos, el seguimiento de la flecha y la
retirada de los efectos. El contraste del aspecto queda para la VM.
