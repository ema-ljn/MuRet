#pragma once

#include "Audio/SoundId.h"
#include "Core/Expected.h"
#include "Kernel/IOnScreenMessages.h"
#include "Kernel/IPacketSender.h"
#include "Kernel/ISoundRequests.h"
#include "Protocol/Outgoing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The kernel's ports in a test: each one keeps its calls, in order.
namespace Client::Tests
{
class FakePacketSender final : public Kernel::IPacketSender
{
public:
    void Send(Protocol::Outgoing packet) const override
    {
        m_sent.push_back(std::move(packet));
    }

    [[nodiscard]] const std::vector<Protocol::Outgoing>& Sent() const
    {
        return m_sent;
    }

private:
    mutable std::vector<Protocol::Outgoing> m_sent;
};

struct SoundRequest
{
    Client::Audio::SoundId id{0};
    std::optional<glm::vec3> origin;
    bool loop = false;

    friend bool operator==(const SoundRequest&, const SoundRequest&) = default;
};

class FakeSoundRequests final : public Kernel::ISoundRequests
{
public:
    void Request(Client::Audio::SoundId id, std::optional<glm::vec3> origin, bool loop) override
    {
        m_requests.push_back(SoundRequest{.id = id, .origin = origin, .loop = loop});
    }
    void Stop(Client::Audio::SoundId id, bool rewind) override
    {
        m_stops.emplace_back(id, rewind);
    }
    void SetEffectsVolume(std::uint8_t level) override
    {
        m_volumes.push_back(level);
    }

    [[nodiscard]] const std::vector<SoundRequest>& Requests() const
    {
        return m_requests;
    }
    [[nodiscard]] const std::vector<std::pair<Client::Audio::SoundId, bool>>& Stops() const
    {
        return m_stops;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& Volumes() const
    {
        return m_volumes;
    }

private:
    std::vector<SoundRequest> m_requests;
    std::vector<std::pair<Client::Audio::SoundId, bool>> m_stops;
    std::vector<std::uint8_t> m_volumes;
};

struct OnScreenMessage
{
    std::string name;
    std::string text;
    std::int32_t type = 0;

    friend bool operator==(const OnScreenMessage&, const OnScreenMessage&) = default;
};

// Answers every Add with `failure` once it has one.
class FakeOnScreenMessages final : public Kernel::IOnScreenMessages
{
public:
    Core::Expected<void, std::string> Add(std::string_view name, std::string_view text, std::int32_t type) override
    {
        m_messages.push_back(OnScreenMessage{.name = std::string(name), .text = std::string(text), .type = type});
        if (m_failure)
            return Core::Unexpected(*m_failure);
        return {};
    }

    void FailWith(std::string failure)
    {
        m_failure = std::move(failure);
    }

    [[nodiscard]] const std::vector<OnScreenMessage>& Messages() const
    {
        return m_messages;
    }

private:
    std::vector<OnScreenMessage> m_messages;
    std::optional<std::string> m_failure;
};
} // namespace Client::Tests
