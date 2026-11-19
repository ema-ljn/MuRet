# 0022. Material de llama compartido para las partículas de Kalima

**Estado** · Propuesto en CL11.2 (2026-11-18).

## Contexto

El ataque del `0x194` intenta reservar una partícula `0x5dc/12` en cada
muestra impar, antes de extender su cinta (`0x4fd580`, `0x4fd5a0`). El
ayudante azul del `0x197` usa la misma partícula en doce puntos
(`0x43ebb7`, `0x43ebd2`).

## Decisión

Conservar el tipo, subtipo, orden de reserva y cuatro sorteos por alta
efectiva. El paso usa la vida posterior al decremento para atenuar la
luz y elegir un cuadro de las cuatro columnas del atlas. También consume
su propio sorteo de giro en el último paso.

El dibujo obtiene las dimensiones, la textura y la mezcla del material de
llama `0x5d9`, ya registrado en MuRet. Emite el cuadro girado a partir de la
edad de `0x5dc/12`. El material compartido cambia el aspecto de la llama.

## Consecuencias

Las pruebas cubren ambos productores, los pools llenos y parciales, el
orden de los sorteos, la vida completa y las coordenadas de los cuadros.
La comparación en pantalla queda para la VM.
