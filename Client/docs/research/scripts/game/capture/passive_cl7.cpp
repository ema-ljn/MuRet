#include "CaptureFields.h"
#include "CaptureHandle.h"
#include "CaptureOutput.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <span>

namespace Client::Capture
{
namespace
{
constexpr DWORD SampleCount = 2250;
constexpr DWORD SampleMs = 40;
constexpr DWORD MillisecondsPerSecond = 1000;
constexpr DWORD EntitySamplePeriod = 25;
constexpr std::size_t MaxEntityCount = 5;
constexpr std::size_t EntityTypeBytes = 2;
constexpr DWORD EntityStride = 0x1d4;
constexpr DWORD EntityTypeOffset = 2;
constexpr DWORD BirdBase = 0x07d2dca8;
constexpr DWORD FishBase = 0x07d35350;
constexpr unsigned int BirdCount = 5;
constexpr unsigned int FishCount = 3;
constexpr DWORD CellAttributeBase = 0x07d0d348;
constexpr DWORD CellLayerBase = 0x07abcc8c;
constexpr DWORD TerrainSide = 256;
constexpr double CellWorldSize = 100.0;
constexpr float TerrainWorldSize = 25600.0F;
constexpr std::size_t MapField = 0;
constexpr std::size_t SceneField = 1;
static_assert(SeriesFields[MapField].address == MapAddress);
static_assert(SeriesFields[SceneField].address == SceneAddress);

struct SeriesSample
{
    DWORD before = 0;
    DWORD after = 0;
    DWORD hero = 0;
    DWORD heroAfter = 0;
    DWORD mapAfter = 0;
    DWORD sceneAfter = 0;
    FieldValues<SeriesFields.size()> values{};
    FieldValues<HeroFields.size(), HeroPathBytes> heroValues{};
    std::array<unsigned char, HeroPositionBytes> heroAfterPosition{};
    std::array<unsigned char, 2> wall{};
    unsigned char layer = 0;
    std::array<DWORD, 2> cell{};
    bool visual = false;
    bool stable = false;
};

bool ReadEntities(HANDLE process, const char* prefix, DWORD base, unsigned int count)
{
    std::array<unsigned char, MaxEntityCount> alive{};
    std::array<unsigned char, MaxEntityCount * EntityTypeBytes> type{};
    if (count > alive.size())
    {
        std::printf("error field=%s reason=entity_count_out_of_range\n", prefix);
        return false;
    }
    for (unsigned int index = 0; index < count; ++index)
    {
        const DWORD offset = index * EntityStride;
        if (base > MAXDWORD - offset - EntityTypeOffset - (EntityTypeBytes - 1))
        {
            std::printf("error field=%s reason=address_overflow\n", prefix);
            return false;
        }
        if (!ReadExact(process, base + offset, std::span(alive).subspan(index, 1), prefix) ||
            !ReadExact(process, base + offset + EntityTypeOffset,
                       std::span(type).subspan(index * EntityTypeBytes, EntityTypeBytes), prefix))
            return false;
    }
    std::array<char, 32> name{};
    const int length = std::snprintf(name.data(), name.size(), "%sAlive", prefix);
    if (length < 0 || static_cast<std::size_t>(length) >= name.size())
        return false;
    PrintBytes(name.data(), std::span(alive).first(count));
    std::printf("%sType=", prefix);
    for (unsigned int index = 0; index < count; ++index)
        std::printf("%s%02x%02x", index != 0 ? "," : "", static_cast<unsigned int>(type[index * EntityTypeBytes + 1]),
                    static_cast<unsigned int>(type[index * EntityTypeBytes]));
    std::putchar('\n');
    return true;
}

bool ReadVisualCell(HANDLE process, SeriesSample& sample)
{
    const auto position = FieldValue<std::array<float, 3>>(sample.heroValues[HeroPositionField]);
    // Celda VISUAL acotada, no la lógica protegida que va adelantada.
    if (!(position[0] >= 0 && position[0] < TerrainWorldSize && position[1] >= 0 && position[1] < TerrainWorldSize))
        return true;
    sample.cell[0] = static_cast<DWORD>(static_cast<double>(position[0]) / CellWorldSize);
    sample.cell[1] = static_cast<DWORD>(static_cast<double>(position[1]) / CellWorldSize);
    const DWORD index = sample.cell[1] * TerrainSide + sample.cell[0];
    if (!ReadExact(process, CellAttributeBase + index * 2, sample.wall, "visualCellAttribute") ||
        !ReadValue(process, CellLayerBase + index, sample.layer, "visualCellLayer1"))
        return false;
    sample.visual = true;
    return true;
}

bool ReadBoundaries(HANDLE process, SeriesSample& sample)
{
    if (!ReadValue(process, MapAddress, sample.mapAfter, "mapAfter") ||
        !ReadValue(process, SceneAddress, sample.sceneAfter, "sceneAfter") ||
        !ReadValue(process, HeroPointerAddress, sample.heroAfter, "heroPointerAfter"))
        return false;
    sample.stable = FieldValue<DWORD>(sample.values[MapField]) == sample.mapAfter &&
                    FieldValue<DWORD>(sample.values[SceneField]) == sample.sceneAfter &&
                    sample.hero == sample.heroAfter;
    if (sample.hero != 0 && sample.hero == sample.heroAfter)
    {
        if (!ReadExact(process, sample.hero + HeroPositionOffset, sample.heroAfterPosition, "heroPositionAfter"))
            return false;
        sample.stable = sample.stable && std::memcmp(sample.heroValues[HeroPositionField].data(),
                                                     sample.heroAfterPosition.data(), HeroPositionBytes) == 0;
    }
    return true;
}

bool ReadSample(HANDLE process, SeriesSample& sample)
{
    sample.before = GetTickCount();
    if (!ReadFields(process, SeriesFields, sample.values) ||
        !ReadValue(process, HeroPointerAddress, sample.hero, "heroPointer"))
        return false;
    if (sample.hero != 0 &&
        (!ReadFields(process, HeroFields, sample.heroValues, sample.hero) || !ReadVisualCell(process, sample)))
        return false;
    if (!ReadBoundaries(process, sample))
        return false;
    sample.after = GetTickCount();
    return true;
}

void PrintSample(DWORD id, const SeriesSample& sample)
{
    std::printf("sample %lu\nticksBefore=%08lx\nticksAfter=%08lx\n", static_cast<unsigned long>(id),
                static_cast<unsigned long>(sample.before), static_cast<unsigned long>(sample.after));
    PrintFields(SeriesFields, sample.values);
    std::printf("heroPointer=%08lx\nheroPresent=%d\n", static_cast<unsigned long>(sample.hero), sample.hero != 0);
    if (sample.hero != 0)
        PrintFields(HeroFields, sample.heroValues);
    std::printf("visualCellPresent=%d\n", sample.visual);
    if (sample.visual)
    {
        PrintField("visualCell", {reinterpret_cast<const unsigned char*>(sample.cell.data()), sizeof(sample.cell)});
        PrintField("visualCellAttribute", sample.wall);
        PrintField("visualCellLayer1", {&sample.layer, sizeof(sample.layer)});
    }
    std::printf("mapAfter=%08lx\nsceneAfter=%08lx\nheroPointerAfter=%08lx\nboundaryStable=%d\n",
                static_cast<unsigned long>(sample.mapAfter), static_cast<unsigned long>(sample.sceneAfter),
                static_cast<unsigned long>(sample.heroAfter), sample.stable);
    if (sample.hero != 0 && sample.hero == sample.heroAfter)
        PrintField("heroPositionAfter", sample.heroAfterPosition);
}

bool CaptureOne(HANDLE process, DWORD id)
{
    SeriesSample sample;
    if (!ReadSample(process, sample))
        return false;
    PrintSample(id, sample);
    const bool entitiesSampled = FieldValue<DWORD>(sample.values[MapField]) == 0 && id % EntitySamplePeriod == 0;
    std::printf("entitiesSampled=%d\n", entitiesSampled);
    // Lecturas separadas: no se lee el padding +1 ni el resto del objeto.
    if (entitiesSampled &&
        (!ReadEntities(process, "bird", BirdBase, BirdCount) || !ReadEntities(process, "fish", FishBase, FishCount)))
        return false;
    std::printf("ticksEntitiesAfter=%08lx\nend_sample\n", static_cast<unsigned long>(GetTickCount()));
    return std::fflush(stdout) == 0 && !std::ferror(stdout);
}

bool PrintSeriesHeader(DWORD pid)
{
    std::puts("camera_series 1\nsequence=non_atomic\nidentity=two_16_byte_signatures_not_full_hash");
    std::printf("rateHz=%08lx\nsamplesPlanned=%08lx\npid=%08lx\n",
                static_cast<unsigned long>(MillisecondsPerSecond / SampleMs), static_cast<unsigned long>(SampleCount),
                static_cast<unsigned long>(pid));
    for (const auto& field : SeriesFields)
        std::printf("field %s %08lx %lu\n", field.name, static_cast<unsigned long>(field.address),
                    static_cast<unsigned long>(field.size));
    for (const auto& field : HeroFields)
        std::printf("heroField %s %08lx %lu\n", field.name, static_cast<unsigned long>(field.address),
                    static_cast<unsigned long>(field.size));
    std::puts("field heroPointer 0743a170 4\nderived visualCellAttribute 07d0d348 2\nderived visualCellLayer1 07abcc8c "
              "1\nentity bird 07d2dca8 5 1d4 alive_0_byte_type_2_word\nentity fish 07d35350 3 1d4 "
              "alive_0_byte_type_2_word\nboundary=map_scene_heroPointer_heroPosition_only");
    return std::fflush(stdout) == 0 && !std::ferror(stdout);
}

bool CaptureSamples(HANDLE process, DWORD pid)
{
    if (!MatchCameraSignatures(process) || !PrintSeriesHeader(pid))
        return false;
    const DWORD started = GetTickCount();
    for (DWORD id = 0; id < SampleCount; ++id)
    {
        const DWORD due = id * SampleMs;
        const DWORD elapsed = GetTickCount() - started;
        if (elapsed < due)
            Sleep(due - elapsed);
        if (!CaptureOne(process, id))
            return false;
    }
    std::printf("samplesCompleted=%08lx\nend\n", static_cast<unsigned long>(SampleCount));
    return true;
}

int CaptureSeries(DWORD pid)
{
    CaptureHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (process.Get() == nullptr)
    {
        std::printf("error field=OpenProcess win32=%08lx\n", static_cast<unsigned long>(GetLastError()));
        return 1;
    }
    const bool complete = CaptureSamples(process.Get(), pid);
    if (const auto error = process.Close())
    {
        std::printf("error field=CloseHandle win32=%08lx\n", static_cast<unsigned long>(*error));
        return 1;
    }
    return complete ? 0 : 1;
}

int RunSeries(int argc, char** argv)
{
    const bool hasOutput = argc == 4 && std::strcmp(argv[2], "--output") == 0;
    CaptureOutput output;
    if (hasOutput && !output.Redirect(argv[3]))
    {
        std::fprintf(stderr, "error field=freopen errno=%08x\n", static_cast<unsigned int>(errno));
        return 1;
    }
    DWORD pid = 0;
    int status = 1;
    if (argc != 2 && !hasOutput)
        std::puts("error reason=invalid_arguments expected=PID_or_--mu-window_[--output_PATH]");
    else if (std::strcmp(argv[1], "--mu-window") == 0)
    {
        if (MuWindowPid(pid))
            status = CaptureSeries(pid);
    }
    else if (ParsePid(argv[1], pid))
        status = CaptureSeries(pid);
    else
        std::puts("error reason=invalid_pid");
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
    return Client::Capture::RunSeries(argc, argv);
}
