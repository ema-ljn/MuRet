#pragma once

#include "Audio/ISoundCatalog.h"
#include "Audio/Playback.h"
#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <glm/ext/vector_float3.hpp>

namespace Client::Platform
{
class SdlAudio final : public Audio::IPlayback
{
public:
    // The catalog must outlive the playback: RegisterSound looks sounds up in it.
    [[nodiscard]] static Core::Expected<std::unique_ptr<SdlAudio>, std::string>
    Create(const std::filesystem::path& dataRoot, std::reference_wrapper<const Audio::ISoundCatalog> catalog,
           bool effectsEnabled = true, bool musicEnabled = true);

    ~SdlAudio() override;
    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

    [[nodiscard]] std::string_view DriverName() const;
    void PlaySound(std::int32_t id, std::optional<glm::vec3> origin, bool loop) override;
    void StopSound(std::int32_t id, bool rewind) override;
    void PlayMusic(std::string_view relativePath, bool loop) override;
    void StopMusic() override;
    void SetEffectsVolume(std::uint8_t level) override;
    void SetListener(glm::vec3 position) override;
    void RegisterSound(std::int32_t id, std::string_view path) override;
    void ReleaseWorldSounds() override;
    [[nodiscard]] int MusicProgressPercent() const override;

private:
    struct Impl;
    explicit SdlAudio(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};
} // namespace Client::Platform
