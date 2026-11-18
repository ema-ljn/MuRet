#include "Audio/SoundCatalog.h"
#include "Platform/SdlAudio.h"

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
#include <string>
#include <thread>
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

void WriteTestWave(const std::filesystem::path& path)
{
    constexpr std::uint32_t SampleRate = 22050;
    constexpr std::uint16_t Channels = 1;
    constexpr std::uint16_t BitsPerSample = 16;
    constexpr std::uint32_t Samples = 2205;
    constexpr std::uint32_t DataBytes = Samples * Channels * BitsPerSample / 8;
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
    write32(file, 36 + DataBytes);
    file.write("WAVEfmt ", 8);
    write32(file, 16);
    write16(file, 1);
    write16(file, Channels);
    write32(file, SampleRate);
    write32(file, SampleRate * Channels * BitsPerSample / 8);
    write16(file, Channels * BitsPerSample / 8);
    write16(file, BitsPerSample);
    file.write("data", 4);
    write32(file, DataBytes);
    for (std::uint32_t sample = 0; sample < Samples; ++sample)
    {
        write16(file, 10000);
    }
}

std::vector<std::uint8_t> ReadRawAudio(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

TEST(SdlAudioTest, WritesLoopedSyntheticAudioToDiskDriver)
{
    HintRestorer restoreHints;
    const auto sound = Client::Audio::FindSound(25);
    ASSERT_TRUE(sound);
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("muret-sdl-audio-" + std::to_string(suffix));
    WriteTestWave(root / std::filesystem::path(sound->path));
    const auto mapSounds = Client::Audio::WorldSoundsForMap(11);
    ASSERT_FALSE(mapSounds.empty());
    const auto mapSound = mapSounds.front();
    WriteTestWave(root / std::filesystem::path(mapSound.path));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk"));
    const auto capture = [&](const char* filename, auto play) -> std::vector<std::uint8_t>
    {
        const auto outputPath = root / filename;
        if (!SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, outputPath.string().c_str()))
        {
            ADD_FAILURE() << "no se pudo elegir el archivo de salida";
            return {};
        }
        auto playback = Client::Platform::SdlAudio::Create(root);
        if (!playback.HasValue())
        {
            ADD_FAILURE() << playback.Error();
            return {};
        }
        play(*playback.Value());
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        playback.Value().reset();
        return ReadRawAudio(outputPath);
    };
    const auto nonzero = [](std::uint8_t byte) { return byte != 0; };

    // El clic pertenece al arranque y el bucle sigue sonando después de los 100 ms de la muestra.
    const auto looped = capture("loop.raw",
                                [&](Client::Platform::SdlAudio& audio)
                                {
                                    audio.SetEffectsVolume(5);
                                    audio.PlaySound(sound->id, std::nullopt, true);
                                });
    ASSERT_GT(looped.size(), 1024U);
    const auto tail = looped.end() - static_cast<std::ptrdiff_t>(looped.size() / 4);
    EXPECT_TRUE(std::any_of(tail, looped.end(), nonzero));

    const auto unregistered = capture("unregistered.raw", [&](Client::Platform::SdlAudio& audio)
                                      { audio.PlaySound(mapSound.id, std::nullopt, false); });
    EXPECT_GT(unregistered.size(), 1024U);
    EXPECT_FALSE(std::ranges::any_of(unregistered, nonzero));

    const auto registered = capture("registered.raw",
                                    [&](Client::Platform::SdlAudio& audio)
                                    {
                                        audio.RegisterSound(mapSound.id, mapSound.path);
                                        audio.PlaySound(mapSound.id, std::nullopt, false);
                                    });
    EXPECT_GT(registered.size(), 1024U);
    EXPECT_TRUE(std::ranges::any_of(registered, nonzero));

    // El arranque reservó el ID del susurro aunque faltaba el WAV. La segunda reserva no lo cambia.
    const auto missingSound = Client::Audio::FindSound(26);
    ASSERT_TRUE(missingSound);
    const auto failed = capture("failed.raw",
                                [&](Client::Platform::SdlAudio& audio)
                                {
                                    WriteTestWave(root / std::filesystem::path(missingSound->path));
                                    audio.RegisterSound(missingSound->id, missingSound->path);
                                    audio.PlaySound(missingSound->id, std::nullopt, false);
                                });
    EXPECT_GT(failed.size(), 1024U);
    EXPECT_FALSE(std::ranges::any_of(failed, nonzero));
    std::filesystem::remove_all(root);
}
