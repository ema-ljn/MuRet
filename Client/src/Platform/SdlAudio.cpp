#include "Platform/SdlAudio.h"

#include "Audio/SoundCatalog.h"
#include "Core/Expected.h"
#include "Data/DataPath.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace Client::Platform
{
namespace
{
constexpr std::uint8_t MaxEffectsVolume = 9;
constexpr std::int32_t WorldSoundBegin = 0xA0;
constexpr std::int32_t WorldSoundEnd = 0x1C2;
constexpr std::array<int, 10> EffectsVolumeDecibels{-10000, -2000, -1397, -1045, -795, -602, -443, -309, -193, -91};

float EffectsGain(std::uint8_t level)
{
    if (level == 0)
    {
        return 0.0f;
    }
    return std::pow(10.0f, static_cast<float>(EffectsVolumeDecibels[level]) / 2000.0f);
}

bool IsWorldSound(std::int32_t id)
{
    return id >= WorldSoundBegin && id < WorldSoundEnd;
}

std::string SdlError(std::string_view action)
{
    return std::string(action) + ": " + SDL_GetError();
}

std::string PathAsUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

bool PlayTrack(MIX_Track* track, bool loop)
{
    const SDL_PropertiesID options = SDL_CreateProperties();
    if (options == 0)
    {
        return false;
    }
    const bool configured = SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loop ? -1 : 0);
    const bool started = configured && MIX_PlayTrack(track, options);
    SDL_DestroyProperties(options);
    return started;
}
} // namespace

struct SdlAudio::Impl
{
    struct Sound
    {
        MIX_Audio* audio = nullptr;
        MIX_Track* track = nullptr;
    };

    std::filesystem::path dataRoot;
    MIX_Mixer* mixer = nullptr;
    MIX_Track* musicTrack = nullptr;
    MIX_Audio* music = nullptr;
    std::unordered_map<std::int32_t, Sound> sounds;
    std::uint8_t effectsVolume = 5;
    bool effectsEnabled = true;
    bool audioInitialized = false;

    ~Impl()
    {
        if (mixer != nullptr)
        {
            StopMusic();
            for (auto& [id, sound] : sounds)
            {
                (void)id;
                DestroySound(sound);
            }
            if (musicTrack != nullptr)
            {
                MIX_DestroyTrack(musicTrack);
            }
            MIX_DestroyMixer(mixer);
        }
        if (audioInitialized)
        {
            MIX_Quit();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }

    void StopMusic()
    {
        if (musicTrack != nullptr)
        {
            MIX_StopTrack(musicTrack, 0);
            MIX_SetTrackAudio(musicTrack, nullptr);
        }
        if (music != nullptr)
        {
            MIX_DestroyAudio(music);
            music = nullptr;
        }
    }

    void DestroySound(Sound& sound)
    {
        if (sound.track != nullptr)
        {
            MIX_StopTrack(sound.track, 0);
            MIX_SetTrackAudio(sound.track, nullptr);
            MIX_DestroyTrack(sound.track);
            sound.track = nullptr;
        }
        if (sound.audio != nullptr)
        {
            MIX_DestroyAudio(sound.audio);
            sound.audio = nullptr;
        }
    }

    void LoadSound(Sound& sound, std::string_view relativePath)
    {
        std::error_code error;
        const auto path = Data::ResolveDataPath(dataRoot, relativePath, error);
        if (!path)
        {
            return;
        }
        const std::string utf8Path = PathAsUtf8(*path);
        sound.audio = MIX_LoadAudio(mixer, utf8Path.c_str(), true);
        if (sound.audio == nullptr)
        {
            return;
        }
        sound.track = MIX_CreateTrack(mixer);
        if (sound.track == nullptr || !MIX_SetTrackAudio(sound.track, sound.audio))
        {
            DestroySound(sound);
            return;
        }
        MIX_SetTrackGain(sound.track, EffectsGain(effectsVolume));
    }
};

SdlAudio::SdlAudio(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Core::Expected<std::unique_ptr<SdlAudio>, std::string> SdlAudio::Create(const std::filesystem::path& dataRoot,
                                                                        bool effectsEnabled, bool musicEnabled)
{
    auto impl = std::make_unique<Impl>();
    impl->dataRoot = dataRoot;
    impl->effectsEnabled = effectsEnabled;
    if (!effectsEnabled && !musicEnabled)
    {
        return std::unique_ptr<SdlAudio>(new SdlAudio(std::move(impl)));
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        return Core::Unexpected(SdlError("SDL no pudo iniciar el audio"));
    }
    if (!MIX_Init())
    {
        const std::string error = SdlError("SDL_mixer no pudo iniciar");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return Core::Unexpected(error);
    }
    impl->audioInitialized = true;
    impl->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (impl->mixer == nullptr)
    {
        const std::string error = SdlError("SDL_mixer no pudo abrir el dispositivo de audio");
        // Impl owns the initialized mixer and SDL subsystems from here.
        return Core::Unexpected(error);
    }
    if (musicEnabled)
    {
        impl->musicTrack = MIX_CreateTrack(impl->mixer);
        if (impl->musicTrack == nullptr)
        {
            const std::string error = SdlError("SDL_mixer no pudo crear la pista de música");
            return Core::Unexpected(error);
        }
    }
    auto audio = std::unique_ptr<SdlAudio>(new SdlAudio(std::move(impl)));
    if (effectsEnabled)
    {
        for (const Audio::SoundDefinition& sound : Audio::StartupSounds())
        {
            audio->RegisterSound(sound.id, sound.path);
        }
    }
    return audio;
}

SdlAudio::~SdlAudio() = default;

std::string_view SdlAudio::DriverName() const
{
    const char* driver = m_impl->audioInitialized ? SDL_GetCurrentAudioDriver() : nullptr;
    return driver != nullptr ? std::string_view(driver) : std::string_view{};
}

void SdlAudio::PlaySound(std::int32_t id, std::optional<glm::vec3> origin, bool loop)
{
    const auto sound = m_impl->sounds.find(id);
    if (sound == m_impl->sounds.end() || sound->second.track == nullptr)
    {
        return;
    }
    MIX_Track* track = sound->second.track;
    if (MIX_TrackPaused(track))
    {
        MIX_SetTrackLoops(track, loop ? -1 : 0);
        MIX_ResumeTrack(track);
        return;
    }
    if (MIX_TrackPlaying(track))
    {
        MIX_SetTrackLoops(track, loop ? -1 : 0);
        return;
    }
    (void)origin;
    PlayTrack(track, loop);
}

void SdlAudio::StopSound(std::int32_t id, bool rewind)
{
    const auto it = m_impl->sounds.find(id);
    if (it == m_impl->sounds.end() || it->second.track == nullptr)
    {
        return;
    }
    MIX_Track* track = it->second.track;
    if (rewind)
    {
        if (MIX_TrackPlaying(track) || MIX_TrackPaused(track))
        {
            MIX_StopTrack(track, 0);
        }
        MIX_SetTrackPlaybackPosition(track, 0);
    }
    else if (MIX_TrackPlaying(track))
    {
        MIX_PauseTrack(track);
    }
}

void SdlAudio::PlayMusic(std::string_view relativePath, bool loop)
{
    if (m_impl->musicTrack == nullptr)
    {
        return;
    }
    m_impl->StopMusic();
    std::error_code error;
    const auto path = Data::ResolveDataPath(m_impl->dataRoot, relativePath, error);
    if (!path)
    {
        return;
    }
    const std::string utf8Path = PathAsUtf8(*path);
    m_impl->music = MIX_LoadAudio(m_impl->mixer, utf8Path.c_str(), false);
    if (m_impl->music == nullptr)
    {
        return;
    }
    if (!MIX_SetTrackAudio(m_impl->musicTrack, m_impl->music))
    {
        m_impl->StopMusic();
        return;
    }
    PlayTrack(m_impl->musicTrack, loop);
}

void SdlAudio::StopMusic()
{
    m_impl->StopMusic();
}

void SdlAudio::SetEffectsVolume(std::uint8_t level)
{
    m_impl->effectsVolume = std::min(level, MaxEffectsVolume);
    const float gain = EffectsGain(m_impl->effectsVolume);
    for (auto& [id, sound] : m_impl->sounds)
    {
        (void)id;
        if (sound.track != nullptr)
        {
            MIX_SetTrackGain(sound.track, gain);
        }
    }
}

void SdlAudio::SetListener(glm::vec3 position)
{
    (void)position;
}

void SdlAudio::RegisterSound(std::int32_t id, std::string_view path)
{
    if (!m_impl->effectsEnabled || id < 0)
    {
        return;
    }
    const auto definition = Audio::FindSound(id, path);
    if (!definition || definition->voices == 0)
    {
        return;
    }
    // 0x41eac0 reserva el ID antes de abrir el WAV. Un fallo conserva el ID sin pista.
    if (const auto [sound, inserted] = m_impl->sounds.try_emplace(id); inserted)
    {
        m_impl->LoadSound(sound->second, definition->path);
    }
}

void SdlAudio::ReleaseWorldSounds()
{
    for (auto it = m_impl->sounds.begin(); it != m_impl->sounds.end();)
    {
        if (!IsWorldSound(it->first))
        {
            ++it;
            continue;
        }
        m_impl->DestroySound(it->second);
        it = m_impl->sounds.erase(it);
    }
}

int SdlAudio::MusicProgressPercent() const
{
    if (m_impl->musicTrack == nullptr || m_impl->music == nullptr)
    {
        return 0;
    }
    const std::int64_t position = MIX_GetTrackPlaybackPosition(m_impl->musicTrack);
    const std::int64_t duration = MIX_GetAudioDuration(m_impl->music);
    // wzAudio.dll 0x10002292 reinicia el progreso al terminar. SDL retiene el último cursor.
    if (position < 0 || duration <= 0 || !MIX_TrackPlaying(m_impl->musicTrack))
    {
        return 0;
    }
    const std::int64_t positionSeconds = MIX_TrackFramesToMS(m_impl->musicTrack, position) / 1000;
    const std::int64_t durationSeconds = MIX_AudioFramesToMS(m_impl->music, duration) / 1000;
    if (durationSeconds <= 0)
    {
        return 0;
    }
    // 0x1000489a..0x100048c6 publica el porcentaje truncado, incluso por encima de 100.
    return static_cast<int>(
        std::min<std::int64_t>(100 * positionSeconds / durationSeconds, std::numeric_limits<int>::max()));
}
} // namespace Client::Platform
