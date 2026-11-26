#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/Field.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace Client::Network
{
// A read past the end gives zero or an empty view and records TooShort, which Finish returns: a reader reads every
// field and checks once.
class ENGINE_API LayoutReader
{
public:
    // WrongPacket for another type, opcode or subcode; TooShort if the packet ends before its subcode or before
    // `layout.minimumSize`. `packet` must outlive the reader.
    [[nodiscard]] static ProtocolResult<LayoutReader> Open(const Packet& packet, const PacketLayout& layout);
    static ProtocolResult<LayoutReader> Open(const Packet&& packet, const PacketLayout& layout) = delete;

    template <typename F> [[nodiscard]] typename F::Type Read()
    {
        using Type = typename F::Type;
        if constexpr (IsByteArray<Type>::value)
        {
            Type value{};
            std::ranges::copy(Bytes(F::Offset, value.size()), value.begin());
            return value;
        }
        else
        {
            return static_cast<Type>(Unsigned(F::Offset, sizeof(Type), F::Order));
        }
    }

    [[nodiscard]] std::span<const std::byte> Bytes(std::size_t offset, std::size_t count);

    [[nodiscard]] std::size_t Size() const;

    template <typename V> [[nodiscard]] ProtocolResult<V> Finish(V value) const
    {
        if (m_error)
        {
            return Core::Unexpected(*m_error);
        }
        return value;
    }

private:
    explicit LayoutReader(std::span<const std::byte> bytes);

    [[nodiscard]] std::uint32_t Unsigned(std::size_t offset, std::size_t size, std::endian order);

    std::span<const std::byte> m_bytes;
    std::optional<ProtocolError> m_error;
};

// What `read` returns from the opened packet, or the first error of Open or of a read.
template <typename Read>
[[nodiscard]] auto ReadPacket(const Packet& packet, const PacketLayout& layout,
                              Read read) -> ProtocolResult<std::invoke_result_t<Read&, LayoutReader&>>
{
    ProtocolResult<LayoutReader> opened = LayoutReader::Open(packet, layout);
    if (!opened.HasValue())
    {
        return Core::Unexpected(opened.Error());
    }
    LayoutReader& reader = opened.Value();
    return reader.Finish(read(reader));
}

template <typename Read> void ReadPacket(const Packet&& packet, const PacketLayout& layout, Read read) = delete;
} // namespace Client::Network
