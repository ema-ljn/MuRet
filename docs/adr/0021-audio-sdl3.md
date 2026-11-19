# 0021. Audio del cliente con SDL3_mixer

**Estado** · Propuesto para CL10 (2026-11-18).

## Contexto

El cliente c32bbd5f reproduce MP3 y Ogg por `wzAudio.dll`. Los WAV usan
DirectSound. Las capas de escena y de red emiten pedidos con el ID, el origen
y el modo de repetición. Las reservas y los cambios de pista tienen reglas
propias de este binario. [Análisis del ASM](../../Client/docs/research/2026-11-18-cl10-audio.md).

## Decisión

Las escenas conservan la selección nativa de pistas y los pedidos de sonido.
SDL3_mixer entrega audio en Linux y Windows a través de `Audio::IPlayback`.
El mezclador abre una pista de música y admite efectos simultáneos de ID
distinto. Un equipo sin dispositivo disponible continúa en silencio. La
inicialización portable habilita los efectos y la música como el launcher
original. El ejecutable c32 obtiene ambos valores del registro de Windows.
Si se ejecuta solo y falta la clave, arranca con música deshabilitada.
`muret-client.ini` acepta los dos valores por separado en `[Audio]`, con las
claves `SoundOnOff` y `MusicOnOff`. El slider del juego ajusta los efectos.

## Consecuencias

El dispositivo, el remuestreo y la salida PCM son responsabilidad de SDL.
MuRet reproduce en 2D con el flag del binario estudiado. Los pedidos conservan
su origen cuando el llamador lo suministra. Las opciones de reproducción, pausa,
ecualizador y volumen de música de la DLL quedan fuera de la interfaz porque
el cliente c32 las deja sin usar. Los tests de música, del catálogo y
del backend ejercitan la selección, las reservas y la reproducción con un
dispositivo de prueba. El contraste dinámico con el cliente original queda en
la validación de VM. [API de SDL3_mixer](https://wiki.libsdl.org/SDL3_mixer/MIX_PlayTrack).
