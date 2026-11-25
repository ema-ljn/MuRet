#include "Audio/ISoundCatalog.h"
#include "Sound/SoundCatalog.h"
#include "Audio/SoundDefinition.h"
#include "Audio/Sdl/SdlAudio.h"

#include <SDL3/SDL_hints.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace
{
class HintRestorer
{
public:
    HintRestorer()
    {
        if (const char* value = SDL_GetHint(SDL_HINT_AUDIO_DRIVER))
        {
            m_driver = value;
        }
        if (const char* value = SDL_GetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE))
        {
            m_output = value;
        }
    }

    ~HintRestorer()
    {
        Restore(SDL_HINT_AUDIO_DRIVER, m_driver);
        Restore(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, m_output);
    }

private:
    static void Restore(const char* hint, const std::optional<std::string>& value)
    {
        if (value)
        {
            SDL_SetHint(hint, value->c_str());
            return;
        }
        SDL_ResetHint(hint);
    }

    std::optional<std::string> m_driver;
    std::optional<std::string> m_output;
};

constexpr std::uint32_t TestWaveSampleRate = 22050;

void WriteTestWave(const std::filesystem::path& path, std::uint32_t samples = TestWaveSampleRate / 10)
{
    constexpr std::uint16_t Channels = 1;
    constexpr std::uint16_t BitsPerSample = 16;
    const std::uint32_t dataBytes = samples * Channels * BitsPerSample / 8;
    const auto write16 = [](std::ofstream& file, std::uint16_t value)
    {
        file.put(static_cast<char>(value & 0xff));
        file.put(static_cast<char>((value >> 8) & 0xff));
    };
    const auto write32 = [](std::ofstream& file, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            file.put(static_cast<char>((value >> shift) & 0xff));
        }
    };
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write("RIFF", 4);
    write32(file, 36 + dataBytes);
    file.write("WAVEfmt ", 8);
    write32(file, 16);
    write16(file, 1);
    write16(file, Channels);
    write32(file, TestWaveSampleRate);
    write32(file, TestWaveSampleRate * Channels * BitsPerSample / 8);
    write16(file, Channels * BitsPerSample / 8);
    write16(file, BitsPerSample);
    file.write("data", 4);
    write32(file, dataBytes);
    for (std::uint32_t sample = 0; sample < samples; ++sample)
    {
        write16(file, 10000);
    }
}

std::vector<std::uint8_t> ReadRawAudio(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::filesystem::path UniqueTemporaryRoot(std::string_view prefix)
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(suffix));
}

template <typename Play>
std::vector<std::uint8_t> CaptureDiskOutput(const std::filesystem::path& root, const char* filename,
                                            const Client::Audio::ISoundCatalog& catalog, Play play)
{
    const auto outputPath = root / filename;
    if (!SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, outputPath.string().c_str()))
    {
        ADD_FAILURE() << "no se pudo elegir el archivo de salida";
        return {};
    }
    auto playback = Client::Audio::Sdl::SdlAudio::Create(root, catalog);
    if (!playback.HasValue())
    {
        ADD_FAILURE() << playback.Error();
        return {};
    }
    EXPECT_EQ(playback.Value()->DriverName(), "disk");
    play(*playback.Value());
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    playback.Value().reset();
    return ReadRawAudio(outputPath);
}

bool IsAudible(const std::vector<std::uint8_t>& output)
{
    return std::ranges::any_of(output, [](std::uint8_t byte) { return byte != 0; });
}

class RecordingCatalog final : public Client::Audio::ISoundCatalog
{
public:
    explicit RecordingCatalog(Client::Audio::SoundDefinition known) : m_known(known) {}

    [[nodiscard]] std::optional<Client::Audio::SoundDefinition> Find(std::int32_t id,
                                                                     std::string_view path) const override
    {
        m_lookups.push_back(id);
        if (id == m_known.id && path == m_known.path)
        {
            return m_known;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::span<const Client::Audio::SoundDefinition> StartupSounds() const override
    {
        return {};
    }

    [[nodiscard]] const std::vector<std::int32_t>& Lookups() const
    {
        return m_lookups;
    }

private:
    Client::Audio::SoundDefinition m_known;
    mutable std::vector<std::int32_t> m_lookups;
};

template <typename Catalog>
concept CreatableWith = requires(const std::filesystem::path& root, Catalog&& catalog) {
    Client::Audio::Sdl::SdlAudio::Create(root, std::forward<Catalog>(catalog));
};

static_assert(CreatableWith<const RecordingCatalog&>);
static_assert(!CreatableWith<RecordingCatalog>, "a temporary catalog would dangle once Create returns");
} // namespace

TEST(SdlAudioTest, WritesLoopedSyntheticAudioToDiskDriver)
{
    HintRestorer restoreHints;
    const auto sound = Client::Sound::FindSound(25);
    ASSERT_TRUE(sound);
    const std::filesystem::path root = UniqueTemporaryRoot("muret-sdl-audio-");
    WriteTestWave(root / std::filesystem::path(sound->path));
    const auto mapSounds = Client::Sound::WorldSoundsForMap(11);
    ASSERT_FALSE(mapSounds.empty());
    const auto mapSound = mapSounds.front();
    WriteTestWave(root / std::filesystem::path(mapSound.path));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk"));
    const auto capture = [&](const char* filename, auto play)
    { return CaptureDiskOutput(root, filename, Client::Sound::GameSoundCatalog(), play); };
    const auto nonzero = [](std::uint8_t byte) { return byte != 0; };

    // El clic pertenece al arranque y el bucle sigue sonando después de los 100 ms de la muestra.
    const auto looped = capture("loop.raw",
                                [&](Client::Audio::Sdl::SdlAudio& audio)
                                {
                                    audio.SetEffectsVolume(5);
                                    audio.PlaySound(sound->id, std::nullopt, true);
                                });
    ASSERT_GT(looped.size(), 1024U);
    const auto tail = looped.end() - static_cast<std::ptrdiff_t>(looped.size() / 4);
    EXPECT_TRUE(std::any_of(tail, looped.end(), nonzero));

    const auto unregistered = capture("unregistered.raw", [&](Client::Audio::Sdl::SdlAudio& audio)
                                      { audio.PlaySound(mapSound.id, std::nullopt, false); });
    EXPECT_GT(unregistered.size(), 1024U);
    EXPECT_FALSE(std::ranges::any_of(unregistered, nonzero));

    const auto registered = capture("registered.raw",
                                    [&](Client::Audio::Sdl::SdlAudio& audio)
                                    {
                                        audio.RegisterSound(mapSound.id, mapSound.path);
                                        audio.PlaySound(mapSound.id, std::nullopt, false);
                                    });
    EXPECT_GT(registered.size(), 1024U);
    EXPECT_TRUE(std::ranges::any_of(registered, nonzero));

    // El arranque reservó el ID del susurro aunque faltaba el WAV. La segunda reserva no lo cambia.
    const auto missingSound = Client::Sound::FindSound(26);
    ASSERT_TRUE(missingSound);
    const auto failed = capture("failed.raw",
                                [&](Client::Audio::Sdl::SdlAudio& audio)
                                {
                                    WriteTestWave(root / std::filesystem::path(missingSound->path));
                                    audio.RegisterSound(missingSound->id, missingSound->path);
                                    audio.PlaySound(missingSound->id, std::nullopt, false);
                                });
    EXPECT_GT(failed.size(), 1024U);
    EXPECT_FALSE(std::ranges::any_of(failed, nonzero));

    constexpr auto MusicWave = "Music/finite.wav";
    WriteTestWave(root / MusicWave, TestWaveSampleRate * 5 / 4);
    const auto finished = capture("finished.raw",
                                  [&](Client::Audio::Sdl::SdlAudio& audio)
                                  {
                                      audio.PlayMusic(MusicWave, false);
                                      std::this_thread::sleep_for(std::chrono::milliseconds(1800));
                                      EXPECT_EQ(audio.MusicProgressPercent(), 0);
                                  });
    EXPECT_GT(finished.size(), 1024U);
    EXPECT_TRUE(std::ranges::any_of(finished, nonzero));

    const auto disabled = Client::Audio::Sdl::SdlAudio::Create(root, Client::Sound::GameSoundCatalog(), false, false);
    ASSERT_TRUE(disabled.HasValue());
    EXPECT_TRUE(disabled.Value()->DriverName().empty());
    std::filesystem::remove_all(root);
}

TEST(SdlAudioTest, PlaysOnlySoundsTheInjectedCatalogKnows)
{
    HintRestorer restoreHints;
    const RecordingCatalog catalog({7, "fake/beep.wav", 1, false});
    const std::filesystem::path root = UniqueTemporaryRoot("muret-sdl-audio-catalog-");
    WriteTestWave(root / "fake/beep.wav");
    WriteTestWave(root / "fake/other.wav");
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk"));

    const auto known = CaptureDiskOutput(root, "known.raw", catalog,
                                         [](Client::Audio::Sdl::SdlAudio& audio)
                                         {
                                             audio.RegisterSound(7, "fake/beep.wav");
                                             audio.PlaySound(7, std::nullopt, false);
                                         });
    const auto unknown = CaptureDiskOutput(root, "unknown.raw", catalog,
                                           [](Client::Audio::Sdl::SdlAudio& audio)
                                           {
                                               audio.RegisterSound(8, "fake/other.wav");
                                               audio.PlaySound(8, std::nullopt, false);
                                           });

    EXPECT_TRUE(IsAudible(known));
    EXPECT_GT(unknown.size(), 1024U);
    EXPECT_FALSE(IsAudible(unknown));
    EXPECT_EQ(catalog.Lookups(), (std::vector<std::int32_t>{7, 8}));
    std::filesystem::remove_all(root);
}

TEST(SdlAudioTest, IgnoresRegistrationsWhileEffectsAreOff)
{
    const RecordingCatalog catalog({7, "fake/beep.wav", 1, false});
    const auto audio =
        Client::Audio::Sdl::SdlAudio::Create(std::filesystem::temp_directory_path(), catalog, false, false);
    ASSERT_TRUE(audio.HasValue());

    audio.Value()->RegisterSound(7, "fake/beep.wav");

    EXPECT_TRUE(catalog.Lookups().empty());
}
