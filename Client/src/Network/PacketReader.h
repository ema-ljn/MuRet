// Corta un flujo TCP en paquetes C1/C2/C3/C4: entran las lecturas tal como llegan, salen paquetes
// enteros.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Cadena de recepción", y el plan de la CL2
// (CL2.1).
//
// TCP entrega bytes, no mensajes: una lectura puede traer medio paquete o siete. El lector junta lo
// que llega y solo mira bytes que llegaron: con una cabecera incompleta espera, en vez de leer
// bytes viejos como el original (Q-C03). Es la versión de producto del FrameCutter de
// labs/cl2.0-flujos/, que recibe de a un byte.

#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Client::Network
{

class PacketReader
{
public:
    PacketReader() = default;

    // Uno movido queda como nuevo: sin bytes y sin error. El movimiento que escribe el compilador
    // vaciaría el buffer y copiaría `consumed`, y Pending() daría un número enorme.
    PacketReader(PacketReader&& other) noexcept;
    PacketReader& operator=(PacketReader&& other) noexcept;
    PacketReader(const PacketReader&) = default;
    PacketReader& operator=(const PacketReader&) = default;
    ~PacketReader() = default;

    // Agrega una lectura. Copia los bytes: `bytes` puede morir después. Con un error, no guarda
    // nada. Cuesta, en promedio, lo que trae, aunque no se saquen los paquetes después de cada
    // Feed.
    void Feed(std::span<const std::byte> bytes);

    // El paquete que sigue, std::nullopt si falta que llegue, o el error.
    //
    // Un error se repite desde ahí, aunque llegue más: TCP no tiene separadores, así que después
    // de una cabecera mala no se sabe dónde empieza el próximo paquete (el original descarta el
    // buffer y sigue adivinando, Q-C38). Quien lo usa cierra la conexión (CL4).
    [[nodiscard]] Core::Expected<std::optional<Packet>, PacketError> Next();

    // Cuántos bytes llegaron y todavía no salieron en un paquete. Después de un error, 0: lo que
    // había se descartó.
    [[nodiscard]] std::size_t Pending() const;

private:
    Core::Expected<std::optional<Packet>, PacketError> Fail(PacketError reason);

    std::vector<std::byte> buffer; // lo que llegó; los primeros `consumed` ya salieron
    std::size_t consumed = 0;
    std::optional<PacketError> error;
};

} // namespace Client::Network
