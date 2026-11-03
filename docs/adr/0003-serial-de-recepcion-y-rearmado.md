# 0003. El serial de recepción se resincroniza, y el C3 rearmado declara lo que ocupa

- **Estado:** aceptado (CL2.3, 2026-11-02).
- **Filas:** Q-C04 y Q-C10 del [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md).
- **Código:** `Client::Network::PacketSerials` y `DecryptPacket` (`Client/src/Network/`), y el
  aviso de `Connection::Decrypt`.

## Contexto

Cada C3 o C4 trae un serial de un byte como primer byte de lo cifrado. El servidor lo sube de a uno
desde 0, y el cliente lo compara con el que espera. Las dos cosas pasan en `0x4e1d50`, con el
serial de recepción en el byte global `0x05767914` (la sección "C3/C4" del
[03](../../Client/docs/research/03-red-y-cifrado.md)).

- **Q-C04.** Si el serial que llega no es el que se esperaba, el original loguea, guarda el
  recibido sin sumarle 1 y despacha igual. El siguiente tampoco coincide, y el desfase queda para
  siempre: cada paquete que llega es otro error en el log. No desconecta ni descarta.
- **Q-C10.** Después de descifrar, el original rearma el C3 como `[C1][n]` y el C4 como
  `[C2][n >> 8][n & 0xFF]`, seguidos de lo descifrado sin el serial. `n` cuenta el serial, que no
  se copia: el paquete declara uno menos de lo que ocupa en un C1, y dos menos en un C2. El C3 de
  `[05] F1 02 01` sale como `C1 04 F1 02 01`. Está verificado con `F1 02`.

Las dos son internas: no cambian ningún byte del cable. El desfase de Q-C04 no se vio nunca con el
servidor del laboratorio: en la captura, los 17 C3/C4 del servidor traen los seriales 0 a 16 en
orden (la misma sección del 03).

## Decisión

MuRet se aparta del original en las dos. No son contrato de datos, y la regla del 07 los corrige.

- **El serial se resincroniza.** `PacketSerials::Receive` compara el que llega con el que espera.
  Si no coincide, desde ahí espera el que llegó más 1, y devuelve `SerialCheck::Resynced`. El
  paquete se despacha igual, como en el original. `Connection::Decrypt` deja un aviso en el log por
  cada `Resynced`: con un solo paquete perdido o repetido, es un aviso, no uno por paquete hasta
  que se cierre la conexión.
- **El rearmado declara lo que ocupa.** `DecryptPacket` arma el C1 o el C2 con el tamaño que mide:
  el C3 de `[05] F1 02 01` da `C1 05 F1 02 01`. Así el paquete descifrado pasa por
  `Packet::FromBytes`, que exige que la cabecera declare lo que mide, como cualquier otro.

Lo demás de la recepción sigue al original: con un checksum malo no se despacha y se espera el
serial siguiente (`PacketSerials::SkipReceive`), y la regla de C3 o C4 al enviar es la del
envoltorio.

## Consecuencias

- **Se gana:** después de un desfase, los paquetes que siguen vuelven a estar en orden, y el log
  dice cuándo pasó una vez. Un paquete descifrado es un `Packet` válido, y quien lo lee no tiene
  que saber que vino cifrado.
- **Se pierde:** un handler portado que leyera el tamaño de la cabecera de un paquete descifrado
  vería uno o dos bytes más que en el original. En MuRet solo lo leen `Packet::FromBytes` y el
  `PacketReader`, y quien despacha mide el paquete con `Bytes()`.
- **Lo fijan:**
  - `PacketSerialsTest.ResyncsWithTheOneThatCame`: esperando el 5, el 9 resincroniza y el 10 ya
    está en orden; el original seguiría desfasado.
  - `PacketSerialsTest.OneLostPacketResyncs` y `ARepeatedSerialResyncsBackwards`: un paquete
    perdido y uno repetido.
  - `EncryptedPacketTest.AnotherSerialStillDecryptsAndResyncs`: el paquete con otro serial se
    descifra y se despacha, con `Resynced`.
  - `EncryptedPacketTest.TheRebuiltPacketDeclaresWhatItMeasures`: el C3 da `C1 05`, y el C4 de lo
    mismo da un C2 que declara 6.

El contrato y los pasos están en la sub-ola CL2.3 del
[plan de la CL2](../superpowers/plans/2026-11-02-ola-cl2-protocolo-sin-red.md).
