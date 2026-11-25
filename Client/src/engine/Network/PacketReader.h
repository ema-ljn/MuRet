#pragma once

#include "Core/Expected.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// Corta un flujo TCP en paquetes C1/C2/C3/C4: entran las lecturas tal como llegan, salen paquetes
// enteros.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Cadena de recepción", y el plan de la CL2
// (CL2.1).
//
// TCP entrega bytes, no mensajes: una lectura puede traer medio paquete o siete. El lector junta lo
// que llega y solo mira bytes que llegaron: con una cabecera incompleta espera, en vez de leer
// bytes viejos como el original (Q-C03). Es la versión de producto del FrameCutter de
// labs/cl2.0-flujos/, que recibe de a un byte.
//
// Con una IObfuscation que enmascara lo que llega (FoxAnti, CL2.4), el primer byte de cada paquete,
// tal como vino en el cable, elige el XOR de todos sus bytes, y el lector los desenmascara a medida
// que los mira: un paquete partido entre dos lecturas sigue con el XOR que eligió su primer byte.
// Lo que sale ya viene desenmascarado. Hace falta acá y no después del corte, porque el tamaño
// también viaja con el XOR.
namespace Client::Network
{
class PacketReader
{
public:
    // Sin ofuscación: IdentityObfuscation::Shared().
    PacketReader();

    // Con la ofuscación de la conexión, que tiene que vivir más que el lector: las dos son de la
    // conexión (CL4). Una temporal no compila, porque moriría antes que el lector.
    explicit PacketReader(const IObfuscation& obfuscation);
    PacketReader(const IObfuscation&&) = delete;

    // Uno movido queda como nuevo: sin bytes y sin error, con la misma ofuscación. El movimiento
    // que escribe el compilador vaciaría el buffer y copiaría `m_consumed`, y Pending() daría un
    // número enorme.
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

    // El primer byte del paquete que empieza en `m_consumed`, desenmascarado. Hace falta que haya
    // llegado.
    std::byte UnmaskFirstByte();

    // Desenmascara lo que llegó del paquete que empieza en `m_consumed`, hasta `end` o hasta donde
    // llegó.
    void UnmaskUpTo(std::size_t end);

    // Nunca nulo: un puntero, para que el lector se pueda asignar.
    const IObfuscation* m_obfuscation;
    std::vector<std::byte> m_buffer; // lo que llegó; los primeros `m_consumed` ya salieron
    std::size_t m_consumed = 0;
    // Hasta acá está desenmascarado; si es `m_consumed`, falta la máscara.
    std::size_t m_unmasked = 0;
    std::byte m_mask{0}; // la del paquete que empieza en `m_consumed`
    std::optional<PacketError> m_error;
};
} // namespace Client::Network
