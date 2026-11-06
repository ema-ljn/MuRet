#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Herramienta didáctica propia: solo observa los campos autorizados de la
 * cámara. */
#include "CaptureFields.h"

static const FieldSpec Fields[] = {
    {"map", 0x006c9dd0, 4},         {"scene", 0x006d5dc8, 4},          {"worldTime", 0x05767b88, 4},
    {"angles", 0x07d36868, 12},     {"position", 0x07d36884, 12},      {"zoom", 0x07d3689c, 4},
    {"distance", 0x07d36ba4, 4},    {"targetDistance", 0x006d5ee0, 4}, {"farPlane", 0x006d5d44, 4},
    {"nearPlane", 0x006d5d40, 4},   {"fieldOfView", 0x006d5d48, 4},    {"fixedHeight", 0x006c9d5c, 4},
    {"level", 0x07d3a0ec, 2},       {"effectTarget", 0x00724588, 12},  {"shake", 0x07d325c8, 4},
    {"renderSize", 0x006d5d60, 8},  {"tourState", 0x006e58fc, 4},      {"cullingLevel", 0x07b4ccb8, 4},
    {"mouseUpdate", 0x077f3b10, 4}, {"mouseUpdateMax", 0x006c8dcc, 4}, {"pubLayer", 0x077f36d0, 4},
    {"storm", 0x00723c50, 4}};

enum
{
    FieldCount = sizeof(Fields) / sizeof(Fields[0])
};

/* Offset/width probados: research06 entrada/caminar y emu_walk.py:82. Angles
 * +1c/+20/+24; path cur/sub/n,X[15],Y[15]. No nombre+1d9 ni celdas protegidas.
 * Extras DWORD confirmados ASM59b860/866,59e2ba,5fb731/738,446126.
 * Entidades confirmadas por root:446120/447030, stride1d4, vivoBYTE0 tipoWORD2. */
static const FieldSpec HeroFields[] = {
    {"heroPosition", 0x10, 12}, {"heroAngles", 0x1c, 12},       {"heroAction", 0x111, 1}, {"heroSafe", 0x3e2, 1},
    {"heroMovement", 0x378, 1}, {"heroMovementType", 0x379, 1}, {"heroPath", 0x3e8, 33}};
enum
{
    HeroFieldCount = sizeof(HeroFields) / sizeof(HeroFields[0]),
    SampleCount = 2250,
    SampleMs = 40
};

static void PrintBytes(const char* name, const unsigned char* data, SIZE_T size)
{
    SIZE_T i;
    printf("%s=", name);
    for (i = 0; i < size; ++i)
        printf("%s%02x", i ? "," : "", (unsigned int)data[i]);
    putchar('\n');
}

static int ReadEntities(HANDLE process, const char* prefix, DWORD base, unsigned int count)
{
    unsigned int i;
    unsigned char alive[5], type[10];
    char name[32];
    for (i = 0; i < count; ++i)
    {
        if (!ReadExact(process, base + i * 0x1d4, alive + i, 1, prefix) ||
            !ReadExact(process, base + i * 0x1d4 + 2, type + i * 2, 2, prefix))
            return 0;
    }
    sprintf(name, "%sAlive", prefix);
    PrintBytes(name, alive, count);
    sprintf(name, "%sType", prefix);
    printf("%s=", name);
    for (i = 0; i < count; ++i)
        printf("%s%02x%02x", i ? "," : "", (unsigned int)type[i * 2 + 1], (unsigned int)type[i * 2]);
    putchar('\n');
    return 1;
}

static int CaptureOne(HANDLE process, DWORD id)
{
    DWORD before = GetTickCount(), after, hero, heroAfter, mapAfter, sceneAfter, map, scene;
    unsigned char values[FieldCount][MaxFieldBytes], heroValues[HeroFieldCount][33], heroAfterPosition[12];
    unsigned char wall[2], layer1;
    DWORD cell[2] = {0, 0};
    int visual = 0, stable = 0;
    SIZE_T i;
    for (i = 0; i < FieldCount; ++i)
        if (!ReadExact(process, Fields[i].address, values[i], Fields[i].size, Fields[i].name))
            return 0;
    memcpy(&map, values[0], 4);
    memcpy(&scene, values[1], 4);
    if (!ReadExact(process, 0x0743a170, &hero, 4, "heroPointer"))
        return 0;
    if (hero)
    {
        float position[3];
        for (i = 0; i < HeroFieldCount; ++i)
        {
            if (hero > MAXDWORD - HeroFields[i].address - (DWORD)(HeroFields[i].size - 1))
            {
                printf("error field=%s reason=address_overflow\n", HeroFields[i].name);
                return 0;
            }
            if (!ReadExact(process, hero + HeroFields[i].address, heroValues[i], HeroFields[i].size,
                           HeroFields[i].name))
                return 0;
        }
        memcpy(position, heroValues[0], 12);
        /* Es la celda VISUAL acotada, no la logica protegida que va adelantada. */
        if (position[0] >= 0 && position[0] < 25600 && position[1] >= 0 && position[1] < 25600)
        {
            DWORD index;
            cell[0] = (DWORD)((double)position[0] / 100.0);
            cell[1] = (DWORD)((double)position[1] / 100.0);
            index = cell[1] * 256 + cell[0];
            if (!ReadExact(process, 0x07d0d348 + index * 2, wall, 2, "visualCellAttribute") ||
                !ReadExact(process, 0x07abcc8c + index, &layer1, 1, "visualCellLayer1"))
                return 0;
            visual = 1;
        }
    }
    if (!ReadExact(process, 0x006c9dd0, &mapAfter, 4, "mapAfter") ||
        !ReadExact(process, 0x006d5dc8, &sceneAfter, 4, "sceneAfter") ||
        !ReadExact(process, 0x0743a170, &heroAfter, 4, "heroPointerAfter"))
        return 0;
    stable = map == mapAfter && scene == sceneAfter && hero == heroAfter;
    if (hero && hero == heroAfter)
    {
        if (!ReadExact(process, hero + 0x10, heroAfterPosition, 12, "heroPositionAfter"))
            return 0;
        stable = stable && memcmp(heroValues[0], heroAfterPosition, 12) == 0;
    }
    after = GetTickCount();
    printf("sample %lu\nticksBefore=%08lx\nticksAfter=%08lx\n", (unsigned long)id, (unsigned long)before,
           (unsigned long)after);
    for (i = 0; i < FieldCount; ++i)
        PrintField(Fields[i].name, values[i], Fields[i].size);
    printf("heroPointer=%08lx\nheroPresent=%d\n", (unsigned long)hero, hero != 0);
    if (hero)
        for (i = 0; i < HeroFieldCount; ++i)
        {
            if (HeroFields[i].size == 33)
                PrintBytes(HeroFields[i].name, heroValues[i], 33);
            else
                PrintField(HeroFields[i].name, heroValues[i], HeroFields[i].size);
        }
    printf("visualCellPresent=%d\n", visual);
    if (visual)
    {
        PrintField("visualCell", (const unsigned char*)cell, 8);
        PrintField("visualCellAttribute", wall, 2);
        PrintField("visualCellLayer1", &layer1, 1);
    }
    printf("mapAfter=%08lx\nsceneAfter=%08lx\nheroPointerAfter=%08lx\nboundaryStable=%d\n", (unsigned long)mapAfter,
           (unsigned long)sceneAfter, (unsigned long)heroAfter, stable);
    if (hero && hero == heroAfter)
        PrintField("heroPositionAfter", heroAfterPosition, 12);
    printf("entitiesSampled=%d\n", map == 0 && id % 25 == 0);
    if (map == 0 && id % 25 == 0)
    {
        /* Lecturas separadas: no se lee el padding+1 ni el resto del objeto. */
        if (!ReadEntities(process, "bird", 0x07d2dca8, 5) || !ReadEntities(process, "fish", 0x07d35350, 3))
            return 0;
    }
    printf("ticksEntitiesAfter=%08lx\nend_sample\n", (unsigned long)GetTickCount());
    return fflush(stdout) == 0 && !ferror(stdout);
}

int main(int argc, char** argv)
{
    DWORD pid = 0, id, started;
    HANDLE process = NULL;
    SIZE_T i;
    int complete = 0, hasOutput = argc == 4 && strcmp(argv[2], "--output") == 0;
    if (hasOutput && freopen(argv[3], "w", stdout) == NULL)
    {
        fprintf(stderr, "error field=freopen errno=%08x\n", (unsigned int)errno);
        return 1;
    }
    if (argc != 2 && !hasOutput)
    {
        puts("error reason=invalid_arguments expected=PID_or_--mu-window_[--output_PATH]");
        goto finish;
    }
    if (strcmp(argv[1], "--mu-window") == 0)
    {
        if (!MuWindowPid(&pid))
            goto finish;
    }
    else if (!ParsePid(argv[1], &pid))
    {
        puts("error reason=invalid_pid");
        goto finish;
    }
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == NULL)
    {
        printf("error field=OpenProcess win32=%08lx\n", (unsigned long)GetLastError());
        goto finish;
    }
    if (!MatchSignature(process, 0x00635ae0, CameraSignature, "camera_signature") ||
        !MatchSignature(process, 0x005fcc30, InverseSignature, "inverse_signature"))
        goto finish;
    puts("camera_series "
         "1\nsequence=non_atomic\nidentity=two_16_byte_signatures_not_full_hash\nrateHz=00000019\nsamplesPlanned="
         "000008ca");
    printf("pid=%08lx\n", (unsigned long)pid);
    for (i = 0; i < FieldCount; ++i)
        printf("field %s %08lx %lu\n", Fields[i].name, (unsigned long)Fields[i].address, (unsigned long)Fields[i].size);
    for (i = 0; i < HeroFieldCount; ++i)
        printf("heroField %s %08lx %lu\n", HeroFields[i].name, (unsigned long)HeroFields[i].address,
               (unsigned long)HeroFields[i].size);
    puts("field heroPointer 0743a170 4\nderived visualCellAttribute 07d0d348 2\nderived visualCellLayer1 07abcc8c "
         "1\nentity bird 07d2dca8 5 1d4 alive_0_byte_type_2_word\nentity fish 07d35350 3 1d4 "
         "alive_0_byte_type_2_word\nboundary=map_scene_heroPointer_heroPosition_only");
    if (fflush(stdout) != 0 || ferror(stdout))
        goto finish;
    started = GetTickCount();
    for (id = 0; id < SampleCount; ++id)
    {
        DWORD due = id * SampleMs, elapsed = GetTickCount() - started;
        if (elapsed < due)
            Sleep(due - elapsed);
        if (!CaptureOne(process, id))
            goto finish;
    }
    puts("samplesCompleted=000008ca\nend");
    complete = 1;
finish:
    if (process != NULL && !CloseHandle(process))
    {
        printf("error field=CloseHandle win32=%08lx\n", (unsigned long)GetLastError());
        complete = 0;
    }
    if (fflush(stdout) != 0 || ferror(stdout))
    {
        fputs("error field=stdout reason=write_failed\n", stderr);
        return 1;
    }
    return complete ? 0 : 1;
}
