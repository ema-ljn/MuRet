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

El cargador usa los nombres canónicos del `Data` soportado y lo abre para
lectura. El arranque nativo `0x4b9df9` llama al renombrado `0x61ef10`. Sus
dos tablas contienen 209 pares WAV y seis pares MP3. Al buscar las rutas
con el `Data` del laboratorio, faltan ambos archivos por los que empieza
cada tabla y todas las demás rutas antiguas. El original saltea allí los
dos recorridos de `MoveFileA`. MuRet conserva ese resultado y trata la
conversión de otras distribuciones de `Data` como preparación de recursos.

El arranque informa el driver activo mediante `SDL_GetCurrentAudioDriver`
cuando habilita audio y conserva el aviso existente si no puede abrirlo.
Esto reemplaza `0x4ba5e6 → 0x41ff30`, que enumeraba drivers DirectSound
y armaba el registro con el callback `0x41fe90`. El formato del log de
MuRet cubre también las nueve llamadas a `0x41fbf0` que agregaban
separadores en `0x41fcfc`, `0x4b9f42`, `0x4b9fc8`, `0x4b9fe3`,
`0x4ba228`, `0x4ba5c8`, `0x4ba5dc`, `0x4ba62e` y `0x4ba649`.

## Consecuencias

El dispositivo, el remuestreo y la salida PCM son responsabilidad de SDL.
MuRet reproduce en 2D con el flag del binario estudiado. Los pedidos conservan
su origen cuando el llamador lo suministra. Las opciones de reproducción, pausa,
ecualizador y volumen de música de la DLL quedan fuera de la interfaz porque
el cliente c32 las deja sin usar. Los tests de música, del catálogo y
del backend ejercitan la selección, las reservas y la reproducción con un
dispositivo de prueba. `SdlAudioTest.WritesLoopedSyntheticAudioToDiskDriver`
comprueba el nombre del driver abierto y su ausencia al deshabilitar ambas
fuentes. El contraste dinámico con el cliente original queda en la validación
de VM. [API de SDL3_mixer](https://wiki.libsdl.org/SDL3_mixer/MIX_PlayTrack).
