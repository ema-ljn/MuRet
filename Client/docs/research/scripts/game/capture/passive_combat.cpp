#include "CaptureFields.h"
#include "CaptureHandle.h"

#include <windows.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <span>

// Lector pasivo de estado de combate: solo lee la whitelist numérica de este archivo.
namespace Client::Capture
{
namespace
{
constexpr DWORD DefaultSamples = 200;
constexpr DWORD DefaultIntervalMs = 20;
constexpr DWORD MaxSamples = 1000;
constexpr DWORD MinIntervalMs = 20;
constexpr DWORD MaxIntervalMs = 1000;
constexpr DWORD SelectedTargetAddress = 0x006c8e30;
constexpr DWORD AttackClickStateAddress = 0x006c8e38;
constexpr DWORD ModelTableAddress = 0x05769ad8;
constexpr DWORD ModelStride = 0xe0;
constexpr DWORD RibbonPoolAddress = 0x075e9870;
constexpr DWORD RibbonCount = 100;
constexpr DWORD RibbonStride = 0x2f4;
constexpr DWORD GlobalBoneBufferAddress = 0x068b181c;
constexpr DWORD BoneMatrixBytes = 48;
constexpr std::size_t CombatFieldCount = 4;
constexpr std::size_t HeroCombatFieldCount = 14;
constexpr std::size_t ModelFieldCount = 5;
constexpr std::size_t HeroGuardFieldCount = 2;
constexpr std::size_t ModelGuardFieldCount = 3;

inline constexpr std::array<FieldSpec, CombatFieldCount> CombatFields{{
    {"map", MapAddress, 4},
    {"scene", SceneAddress, 4},
    {"selectedTarget", SelectedTargetAddress, 4},
    {"attackClickState", AttackClickStateAddress, 4},
}};

inline constexpr std::array<FieldSpec, HeroCombatFieldCount> HeroCombatFields{{
    {"heroType", 0x02, 2},
    {"heroScale", 0x0c, 4},
    {"heroPosition", 0x10, 12},
    {"heroAngles", 0x1c, 12},
    {"heroAction", 0x111, 1},
    {"heroPreviousAction", 0x112, 1},
    {"heroFrame", 0x114, 4},
    {"heroPreviousFrame", 0x118, 4},
    {"hand0", 0x2b8, 2},
    {"hand0Level", 0x2ba, 1},
    {"hand0Bone", 0x2bd, 1},
    {"hand1", 0x2d8, 2},
    {"hand1Level", 0x2da, 1},
    {"hand1Bone", 0x2dd, 1},
}};

inline constexpr std::array<FieldSpec, ModelFieldCount> ModelFields{{
    {"modelScale", 0x68, 4},
    {"modelOrigin", 0x6c, 12},
    {"modelAction", 0xa0, 1},
    {"modelFrame", 0xa4, 4},
    {"modelKey", 0xa8, 2},
}};

inline constexpr std::array<FieldSpec, HeroGuardFieldCount> HeroGuardFields{{
    {"heroActionAfter", 0x111, 1},
    {"heroFrameAfter", 0x114, 4},
}};

inline constexpr std::array<FieldSpec, ModelGuardFieldCount> ModelGuardFields{{
    {"modelActionAfter", 0xa0, 1},
    {"modelFrameAfter", 0xa4, 4},
    {"modelKeyAfter", 0xa8, 2},
}};

struct Options
{
    DWORD samples = DefaultSamples;
    DWORD intervalMs = DefaultIntervalMs;
    const char* outputPath = nullptr;
};

struct RibbonRecord
{
    DWORD index = 0;
    std::array<unsigned char, 1> alive{};
    std::array<unsigned char, 4> type{};
    std::array<unsigned char, 4> life{};
    std::array<unsigned char, 4> count{};
    std::array<unsigned char, 12> color{};
    std::array<unsigned char, 12> firstPoint{};
    std::array<unsigned char, 12> secondPoint{};
    std::array<unsigned char, 4> key{};
};

class NewOutputFile
{
public:
    NewOutputFile() = default;
    NewOutputFile(const NewOutputFile&) = delete;
    NewOutputFile& operator=(const NewOutputFile&) = delete;

    ~NewOutputFile() noexcept
    {
        Restore();
    }

    bool Redirect(const char* path)
    {
        // -mwindows puede dejar stdout sin descriptor; NUL le da al CRT un destino redirigible.
        if (_fileno(stdout) < 0 && (std::freopen("NUL", "w", stdout) == nullptr || _fileno(stdout) < 0))
        {
            std::fputs("error field=stdout reason=initialize_failed\n", stderr);
            return false;
        }
        if (path == nullptr)
            return true;
        HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            std::printf("error field=output reason=create_new_failed win32=%08lx\n",
                        static_cast<unsigned long>(GetLastError()));
            return false;
        }
        const int fileDescriptor = _open_osfhandle(reinterpret_cast<intptr_t>(file), _O_WRONLY | _O_TEXT);
        if (fileDescriptor == -1)
        {
            CloseHandle(file);
            std::printf("error field=output reason=open_osfhandle_failed errno=%08lx\n",
                        static_cast<unsigned long>(errno));
            return false;
        }
        m_savedStdout = _dup(_fileno(stdout));
        if (m_savedStdout == -1 || _dup2(fileDescriptor, _fileno(stdout)) == -1)
        {
            const int error = errno;
            _close(fileDescriptor);
            Restore();
            std::printf("error field=output reason=redirect_failed errno=%08lx\n", static_cast<unsigned long>(error));
            return false;
        }
        _close(fileDescriptor);
        return true;
    }

    bool Finish()
    {
        const bool flushed = std::fflush(stdout) == 0;
        const bool writable = std::ferror(stdout) == 0;
        Restore();
        return flushed && writable;
    }

private:
    void Restore() noexcept
    {
        if (m_savedStdout != -1)
        {
            _dup2(m_savedStdout, _fileno(stdout));
            _close(m_savedStdout);
            m_savedStdout = -1;
        }
    }

    int m_savedStdout = -1;
};

bool ParseUnsigned(const char* text, DWORD maximum, DWORD& value)
{
    DWORD parsed = 0;
    if (*text == '\0')
        return false;
    for (; *text != '\0'; ++text)
    {
        if (*text < '0' || *text > '9')
            return false;
        const DWORD digit = static_cast<DWORD>(*text - '0');
        if (parsed > (maximum - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

bool ParseOptions(int argc, char** argv, Options& options)
{
    bool hasMuWindow = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--mu-window") == 0)
        {
            if (hasMuWindow)
                return false;
            hasMuWindow = true;
            continue;
        }
        if (index + 1 >= argc)
            return false;
        const char* value = argv[++index];
        if (std::strcmp(argv[index - 1], "--output") == 0)
            options.outputPath = value;
        else if (std::strcmp(argv[index - 1], "--samples") == 0)
        {
            if (!ParseUnsigned(value, MaxSamples, options.samples) || options.samples == 0)
                return false;
        }
        else if (std::strcmp(argv[index - 1], "--interval-ms") == 0)
        {
            if (!ParseUnsigned(value, MaxIntervalMs, options.intervalMs) || options.intervalMs < MinIntervalMs)
                return false;
        }
        else
            return false;
    }
    return hasMuWindow;
}

DWORD ModelAddress(DWORD modelTable, DWORD actorType)
{
    return modelTable + actorType * ModelStride;
}

bool ReadModelFields(HANDLE process, DWORD modelTable, DWORD actorType, FieldValues<ModelFieldCount>& values)
{
    const DWORD address = ModelAddress(modelTable, actorType);
    for (std::size_t index = 0; index < ModelFields.size(); ++index)
    {
        const auto& field = ModelFields[index];
        if (address > MAXDWORD - field.address - static_cast<DWORD>(field.size - 1))
        {
            std::printf("error field=%s reason=address_overflow\n", field.name);
            return false;
        }
        if (!ReadExact(process, address + field.address, std::span(values[index]).first(field.size), field.name))
            return false;
    }
    return true;
}

bool ReadBoneMatrices(HANDLE process, const char* field, DWORD buffer, const FieldValues<HeroCombatFieldCount>& fields,
                      std::array<std::array<unsigned char, BoneMatrixBytes>, 2>& matrices)
{
    const std::array<std::size_t, 2> boneFields{10, 13};
    for (std::size_t hand = 0; hand < boneFields.size(); ++hand)
    {
        const auto bone = fields[boneFields[hand]][0];
        const DWORD offset = static_cast<DWORD>(bone) * BoneMatrixBytes;
        if (buffer > MAXDWORD - offset - (BoneMatrixBytes - 1))
        {
            std::printf("error field=%s reason=address_overflow\n", field);
            return false;
        }
        auto& matrix = matrices[hand];
        if (!ReadExact(process, buffer + offset, matrix, field))
            return false;
    }
    return true;
}

bool ReadRibbonRecord(HANDLE process, DWORD index, DWORD hero, RibbonRecord& record, bool& owned)
{
    const DWORD base = RibbonPoolAddress + index * RibbonStride;
    record.index = index;
    if (!ReadExact(process, base, record.alive, "ribbonAlive"))
        return false;
    std::array<unsigned char, 4> owner{};
    if (!ReadExact(process, base + 0x0c, owner, "ribbonOwner"))
        return false;
    owned = record.alive[0] != 0 && FieldValue<DWORD>(owner) == hero;
    if (!owned)
        return true;
    if (!ReadExact(process, base + 0x04, record.type, "ribbonType") ||
        !ReadExact(process, base + 0x08, record.life, "ribbonLife") ||
        !ReadExact(process, base + 0x10, record.count, "ribbonCount") ||
        !ReadExact(process, base + 0x14, record.color, "ribbonColor") ||
        !ReadExact(process, base + 0x20, record.firstPoint, "ribbonFirstPoint") ||
        !ReadExact(process, base + 0x188, record.secondPoint, "ribbonSecondPoint") ||
        !ReadExact(process, base + 0x2f0, record.key, "ribbonKey"))
        return false;
    return true;
}

bool ReadRibbons(HANDLE process, DWORD hero, std::array<RibbonRecord, RibbonCount>& records, DWORD& recordCount)
{
    recordCount = 0;
    for (DWORD index = 0; index < RibbonCount; ++index)
    {
        RibbonRecord record;
        bool owned = false;
        if (!ReadRibbonRecord(process, index, hero, record, owned))
            return false;
        if (owned)
            records[recordCount++] = record;
    }
    return true;
}

struct CombatSample
{
    DWORD ticksBefore = 0;
    DWORD ticksAfter = 0;
    DWORD heroBefore = 0;
    DWORD heroAfter = 0;
    DWORD mapAfter = 0;
    DWORD sceneAfter = 0;
    DWORD targetAfter = 0;
    DWORD clickStateAfter = 0;
    DWORD bodyPosePointer = 0;
    DWORD modelTable = 0;
    FieldValues<CombatFieldCount> combat{};
    FieldValues<HeroCombatFieldCount> hero{};
    FieldValues<ModelFieldCount> model{};
    FieldValues<HeroGuardFieldCount> heroGuardAfter{};
    FieldValues<ModelGuardFieldCount> modelAfter{};
    std::array<std::array<unsigned char, BoneMatrixBytes>, 2> globalBoneMatrices{};
    std::array<std::array<unsigned char, BoneMatrixBytes>, 2> ownBoneMatrices{};
    std::array<RibbonRecord, RibbonCount> ribbons{};
    DWORD ribbonCount = 0;
    bool guardFieldsAfterPresent = false;
    bool observedGuardMatch = false;
};

bool ReadSample(HANDLE process, CombatSample& sample)
{
    sample.ticksBefore = GetTickCount();
    if (!ReadFields(process, CombatFields, sample.combat) ||
        !ReadValue(process, HeroPointerAddress, sample.heroBefore, "heroPointer"))
        return false;
    if (sample.heroBefore != 0)
    {
        constexpr FieldSpec BodyPosePointerField{"bodyPosePointer", 0x120, 4};
        if (!ReadFields(process, HeroCombatFields, sample.hero, sample.heroBefore))
            return false;
        if (!AddressFits(sample.heroBefore, BodyPosePointerField))
        {
            std::printf("error field=%s reason=address_overflow\n", BodyPosePointerField.name);
            return false;
        }
        if (!ReadValue(process, sample.heroBefore + BodyPosePointerField.address, sample.bodyPosePointer,
                       BodyPosePointerField.name))
            return false;
        const DWORD actorType = FieldValue<WORD>(sample.hero[0]);
        // 4f9587/4fd6a8: el global contiene un puntero al arreglo, no el arreglo inline.
        if (!ReadValue(process, ModelTableAddress, sample.modelTable, "modelTablePointer") ||
            !ReadModelFields(process, sample.modelTable, actorType, sample.model) ||
            !ReadBoneMatrices(process, "globalBoneMatrix", GlobalBoneBufferAddress, sample.hero,
                              sample.globalBoneMatrices) ||
            (sample.bodyPosePointer != 0 && !ReadBoneMatrices(process, "ownBoneMatrix", sample.bodyPosePointer,
                                                              sample.hero, sample.ownBoneMatrices)) ||
            !ReadRibbons(process, sample.heroBefore, sample.ribbons, sample.ribbonCount))
            return false;
    }
    if (!ReadValue(process, HeroPointerAddress, sample.heroAfter, "heroPointerAfter") ||
        !ReadValue(process, MapAddress, sample.mapAfter, "mapAfter") ||
        !ReadValue(process, SceneAddress, sample.sceneAfter, "sceneAfter") ||
        !ReadValue(process, SelectedTargetAddress, sample.targetAfter, "selectedTargetAfter") ||
        !ReadValue(process, AttackClickStateAddress, sample.clickStateAfter, "attackClickStateAfter"))
        return false;
    if (sample.heroBefore != 0 && sample.heroBefore == sample.heroAfter)
    {
        const DWORD actorType = FieldValue<WORD>(sample.hero[0]);
        const DWORD modelAddress = ModelAddress(sample.modelTable, actorType);
        if (!ReadFields(process, HeroGuardFields, sample.heroGuardAfter, sample.heroBefore) ||
            !ReadFields(process, ModelGuardFields, sample.modelAfter, modelAddress))
            return false;
        sample.guardFieldsAfterPresent = true;
        sample.observedGuardMatch =
            FieldValue<unsigned char>(sample.heroGuardAfter[0]) == FieldValue<unsigned char>(sample.hero[4]) &&
            FieldValue<DWORD>(sample.heroGuardAfter[1]) == FieldValue<DWORD>(sample.hero[6]) &&
            FieldValue<unsigned char>(sample.modelAfter[0]) == FieldValue<unsigned char>(sample.model[2]) &&
            FieldValue<DWORD>(sample.modelAfter[1]) == FieldValue<DWORD>(sample.model[3]) &&
            FieldValue<WORD>(sample.modelAfter[2]) == FieldValue<WORD>(sample.model[4]);
    }
    sample.observedGuardMatch = sample.observedGuardMatch && sample.heroBefore == sample.heroAfter &&
                                FieldValue<DWORD>(sample.combat[0]) == sample.mapAfter &&
                                FieldValue<DWORD>(sample.combat[1]) == sample.sceneAfter &&
                                FieldValue<DWORD>(sample.combat[2]) == sample.targetAfter &&
                                FieldValue<DWORD>(sample.combat[3]) == sample.clickStateAfter;
    sample.ticksAfter = GetTickCount();
    return true;
}

void PrintSample(DWORD index, const CombatSample& sample)
{
    std::printf("sample=%lu\nticksBefore=%08lx\n", static_cast<unsigned long>(index),
                static_cast<unsigned long>(sample.ticksBefore));
    PrintFields(CombatFields, sample.combat);
    std::printf("heroPointerBefore=%08lx\nheroPresent=%d\n", static_cast<unsigned long>(sample.heroBefore),
                sample.heroBefore != 0);
    if (sample.heroBefore != 0)
    {
        PrintFields(HeroCombatFields, sample.hero);
        std::printf("bodyPosePointer=%08lx\n", static_cast<unsigned long>(sample.bodyPosePointer));
        std::printf("modelTablePointer=%08lx\n", static_cast<unsigned long>(sample.modelTable));
        PrintFields(ModelFields, sample.model);
        std::printf("guardFieldsAfterPresent=%d\n", sample.guardFieldsAfterPresent);
        if (sample.guardFieldsAfterPresent)
        {
            PrintFields(HeroGuardFields, sample.heroGuardAfter);
            PrintFields(ModelGuardFields, sample.modelAfter);
        }
        const std::array<std::size_t, 2> boneFields{10, 13};
        for (std::size_t hand = 0; hand < boneFields.size(); ++hand)
        {
            std::printf("globalBoneMatrix hand=%lu bone=%02x ", static_cast<unsigned long>(hand),
                        static_cast<unsigned int>(sample.hero[boneFields[hand]][0]));
            PrintBytes("bytes", sample.globalBoneMatrices[hand]);
            if (sample.bodyPosePointer != 0)
            {
                std::printf("ownBoneMatrix hand=%lu bone=%02x ", static_cast<unsigned long>(hand),
                            static_cast<unsigned int>(sample.hero[boneFields[hand]][0]));
                PrintBytes("bytes", sample.ownBoneMatrices[hand]);
            }
        }
        std::printf("ownedRibbonCount=%lu\n", static_cast<unsigned long>(sample.ribbonCount));
        for (DWORD ribbonIndex = 0; ribbonIndex < sample.ribbonCount; ++ribbonIndex)
        {
            const auto& ribbon = sample.ribbons[ribbonIndex];
            std::printf("ribbon index=%lu ", static_cast<unsigned long>(ribbon.index));
            PrintField("alive", ribbon.alive);
            PrintField("type", ribbon.type);
            PrintField("life", ribbon.life);
            PrintField("count", ribbon.count);
            PrintField("color", ribbon.color);
            PrintField("firstPoint", ribbon.firstPoint);
            PrintField("secondPoint", ribbon.secondPoint);
            PrintField("key", ribbon.key);
        }
    }
    std::printf("mapAfter=%08lx\nsceneAfter=%08lx\nselectedTargetAfter=%08lx\nattackClickStateAfter=%08lx\n"
                "heroPointerAfter=%08lx\nunstable=1\nobservedGuardMatch=%d\nticksAfter=%08lx\n",
                static_cast<unsigned long>(sample.mapAfter), static_cast<unsigned long>(sample.sceneAfter),
                static_cast<unsigned long>(sample.targetAfter), static_cast<unsigned long>(sample.clickStateAfter),
                static_cast<unsigned long>(sample.heroAfter), sample.observedGuardMatch,
                static_cast<unsigned long>(sample.ticksAfter));
}

bool CaptureSamples(HANDLE process, DWORD pid, const Options& options)
{
    if (!MatchCameraSignatures(process))
        return false;
    std::printf("passive_combat 1\nsequence=non_atomic\nidentity=two_16_byte_signatures_not_full_hash\n"
                "pid=%08lx\nsamples=%lu\nintervalMs=%lu\n",
                static_cast<unsigned long>(pid), static_cast<unsigned long>(options.samples),
                static_cast<unsigned long>(options.intervalMs));
    const DWORD started = GetTickCount();
    for (DWORD index = 0; index < options.samples; ++index)
    {
        const DWORD due = index * options.intervalMs;
        const DWORD elapsed = GetTickCount() - started;
        if (elapsed < due)
            Sleep(due - elapsed);
        CombatSample sample;
        if (!ReadSample(process, sample))
            return false;
        PrintSample(index, sample);
        std::printf("ticksEnd=%08lx\nend_sample\n", static_cast<unsigned long>(GetTickCount()));
        if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0)
            return false;
    }
    std::puts("end");
    return std::fflush(stdout) == 0 && std::ferror(stdout) == 0;
}

int Capture(DWORD pid, const Options& options)
{
    CaptureHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (process.Get() == nullptr)
    {
        std::printf("error field=OpenProcess win32=%08lx\n", static_cast<unsigned long>(GetLastError()));
        return 1;
    }
    const bool complete = CaptureSamples(process.Get(), pid, options);
    if (const auto error = process.Close())
    {
        std::printf("error field=CloseHandle win32=%08lx\n", static_cast<unsigned long>(*error));
        return 1;
    }
    return complete ? 0 : 1;
}

int Run(int argc, char** argv)
{
    Options options;
    NewOutputFile output;
    if (!ParseOptions(argc, argv, options))
    {
        std::puts("error reason=invalid_arguments expected=--mu-window [--output FILE] [--samples 1..1000] "
                  "[--interval-ms 20..1000]");
        return 1;
    }
    if (!output.Redirect(options.outputPath))
        return 1;
    DWORD pid = 0;
    int status = 1;
    if (MuWindowPid(pid))
        status = Capture(pid, options);
    if (!output.Finish())
    {
        std::fputs("error field=stdout reason=write_failed\n", stderr);
        return 1;
    }
    return status;
}
} // namespace
} // namespace Client::Capture

int main(int argc, char** argv)
{
    return Client::Capture::Run(argc, argv);
}
