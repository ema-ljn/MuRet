#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// The golden folder: the original client's Data\ (Enc1.dat, Dec2.dat, Player\, World1\…).
// It is the repo's, Client/assets/Data, the same as the lab's (analysis/decompilacion-maxima/assets/original-data of
// EmaNLJN/mu-online-reverse-lab, by SHA-256). MU_CLIENT_GOLDEN_DIR points to another one if needed. The rule:
//   - if the variable is unset or empty, the test reads the repo's (D-C9);
//   - if the folder does not exist, or lacks the file the test asks for, the test fails.
// The other folders in this file (the dumps, the wire, the traces) are outside the repo, and without their variable
// the test is skipped.
namespace Client::Tests
{
// Client/assets/Data.
std::filesystem::path RepoDataDir();

// MU_CLIENT_GOLDEN_DIR's value, or the repo's Data if it is unset or empty.
std::filesystem::path GoldenDir();

// GoldenDir's rule, separate from reading the environment so it can be tested.
std::filesystem::path GoldenDirOrRepoData(const std::optional<std::string>& value);

// The rule, separate from reading the environment so it can be tested (test_golden_dir.cpp).
enum class GoldenDirState
{
    NotSet,  // no variable, or empty: the test is skipped
    Missing, // the variable points to something that is not a folder: the test fails
    Ready,
};
std::optional<std::filesystem::path> GoldenDirFromValue(const std::optional<std::string>& value);
GoldenDirState CheckGoldenDir(const std::optional<std::filesystem::path>& dir);

// Ignoring ASCII case (Core::EqualsIgnoringAsciiCase): the golden folder has .OZJ, .ozj and .Ozj.
bool HasExtension(const std::filesystem::path& path, std::string_view extension);

// Base for the tests that read real files. SetUp() fails the test if the golden folder does not exist.
class GoldenDataTest : public ::testing::Test
{
protected:
    void SetUp() override;

    // The file's path, resolved as the original asks for it (case-insensitive). If it is missing, fails the test and
    // returns std::nullopt.
    std::optional<std::filesystem::path> DataFile(std::string_view originalPath) const;

    std::filesystem::path m_dataRoot;
};

// A capture of the original main.exe's memory: its folder inside memoria-cliente/ (mu-golden's README says what each
// has) and the world that was loaded, as the N of the World<N> folder the loaders receive. They hold for the lab's
// copy of Data\, which has the same files (same SHA-256) as the ones that were loaded.
struct MemoryCapture
{
    std::string_view folder;
    std::uint8_t world = 0;
};

// With the character in Devias (World3, g_world = 2): the tables, the terrain and the light (s31_*.bin).
inline constexpr MemoryCapture DeviasCapture{"2026-10-09-devias", 3};
// In Lorencia (World1, g_world = 0), at cell (136, 130): the terrain, the light and the textures (lor_*.bin and
// lor2_*.bin).
inline constexpr MemoryCapture LorenciaCapture{"2026-10-09-lorencia", 1};
// In Lorencia, in another read and in scene 5: the map's 2,870 objects in their 256 lists (obj_*.bin).
inline constexpr MemoryCapture LorenciaObjectsCapture{"2026-10-09-lorencia-obj", 1};

// Whether dump `name` of capture `folder` is in the dump folder `root`.
enum class DumpState
{
    Found,
    Missing,
    // Missing, but present in `root` itself: the variable points to a capture and not to the folder that holds them.
    RootIsACapture,
};
DumpState CheckDump(const std::filesystem::path& root, std::string_view folder, std::string_view name);

// The memory dumps. MU_CLIENT_MEMORY_DIR points to the captures' folder: in the research repo,
// client-research/mu-golden/memoria-cliente, outside Git. The rule is the golden folder's. A memory test needs both:
// it compares what MuRet loads from a file with what the original loaded from the same file.
class MemoryDumpTest : public GoldenDataTest
{
protected:
    void SetUp() override;

    // The path of dump `name` of `capture`. If it is missing, fails the test and returns std::nullopt.
    std::optional<std::filesystem::path> DumpFile(const MemoryCapture& capture, std::string_view name) const;

    std::filesystem::path m_dumpRoot;
};

// The lab's wire. MU_CLIENT_WIRE_DIR points to a folder, outside Git, with:
//   - the muproxy.log the lab's proxy wrote between the original client and the servers (in
//     EmaNLJN/mu-online-reverse-lab, analysis/mainexe/evidence/): it carries the player's login;
//   - the key files of the server it was captured with, Dec1.dat and Enc2.dat (in
//     EmaNLJN/mu-server-repack-research-102i, gameserver/evidence/keyfiles/): with them the server decrypts what the
//     client sends and encrypts its answers.
// The rule is the golden folder's, and the variable is only in the environment, like the dumps'. It does not need
// Data\: the wire is compared with itself.
class WireDataTest : public ::testing::Test
{
protected:
    void SetUp() override;

    // The path of `name` in the wire folder. If it is missing, fails the test and returns std::nullopt.
    std::optional<std::filesystem::path> WireFile(std::string_view name) const;

    std::filesystem::path m_wireRoot;
};

// Both: the golden folder and the wire's. A test that compares the client's keys, Enc1.dat and Dec2.dat, with the
// server's needs both, and is skipped if either variable is missing.
class GoldenAndWireDataTest : public GoldenDataTest
{
protected:
    void SetUp() override;

    // Like WireDataTest::WireFile.
    std::optional<std::filesystem::path> WireFile(std::string_view name) const;

    std::filesystem::path m_wireRoot;
};

// The lab's GL traces. MU_CLIENT_GL_TRACE_DIR points to a folder, outside Git, with the decompressed GLIntercept traces
// (gunzip -k): character-selection-Frame_003207.txt and lorencia-Frame_016215.txt, from graphics-01/ in PR #45 of
// EmaNLJN/mu-online-reverse-lab (question 12 of CL6's plan). The rule is the golden folder's, and the variable is only
// in the environment. A trace test also needs Data\: it compares what MuRet does with the models with what the
// original sent to GL.
class GoldenAndGlTraceTest : public GoldenDataTest
{
protected:
    void SetUp() override;

    // The path of trace `name`. If it is missing, fails the test and returns std::nullopt.
    std::optional<std::filesystem::path> TraceFile(std::string_view name) const;

    std::filesystem::path m_traceRoot;
};

// The FreeType tests' font. MU_CLIENT_FONT_FILE points to a TrueType file, outside Git: in CI, a runner font (DejaVu
// on Ubuntu, one from C:\Windows\Fonts on Windows). The rule is the golden folder's: without the variable, or empty,
// the test is skipped; if it points to something that is not a file, it fails.
class FontFileTest : public ::testing::Test
{
protected:
    void SetUp() override;

    std::filesystem::path m_fontFile;
};
} // namespace Client::Tests
