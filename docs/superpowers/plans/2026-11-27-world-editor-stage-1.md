# World editor, stage 1: the world's client files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the engine reads and writes `EncTerrain<N>.map`, `EncTerrain<N>.att`, `EncTerrain<N>.obj` and
`TerrainHeight.OZB` whole, so that every one of them in `Client/assets/Data` is written back with the same bytes and an
edit lands where the client's loader reads it. No editor yet.

**Architecture:** one file model per format beside its loader in `engine/Data/Terrain`, holding the values as the file
stores them (bytes, not the loader's floats) plus every byte the loader skips. `Read<Format>File` turns the file's bytes
into the model and `Write<Format>File` turns the model back into bytes, with the layout of the original's saver and the
encryption its loader undoes. The loaders the client runs (`LoadTerrainMapping` and the rest) are not touched: they copy
the original's tolerance, the file models are what the editor's `Document` (stage 3) holds. A new `EncryptK16` in
`engine/Crypto` is the inverse of `DecryptK16`.

**Tech Stack:** C++20, CMake, GoogleTest; the engine's `Core::Expected`, `DataError`, `Crypto::Xor3`, `Crypto::K16`. No
new dependency.

**Spec:** [`docs/superpowers/specs/2026-11-27-world-editor-design.md`](../specs/2026-11-27-world-editor-design.md),
stage 1 of "Stages", "Byte-exact" and "What a 1.02 world is". The savers and the loaders are in the research repo
(EmaNLJN/mu-server-repack-research-102i, `client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`).

## Global Constraints

- Byte-exact: `Write<Format>File(*Read<Format>File(bytes)) == bytes` for every file of the four formats in
  `Client/assets/Data`: 23 `EncTerrain*.map`, 29 `EncTerrain*.att`, 23 `EncTerrain*.obj`, 19 `TerrainHeight.OZB`
  (names compared ignoring ASCII case).
- `Client/assets/Data` is never written. Tests build what they edit in memory.
- `LoadTerrainMapping`, `LoadTerrainAttributes`, `LoadTerrainAttributesOfAnyWorld`, `LoadTerrainObjects`,
  `LoadTerrainHeights` and `LoadTerrainLight` do not change, and none of their tests is edited.
- Every new function another binary links carries `ENGINE_API` (ADR 0028). A function returning `Core::Expected` does
  not repeat `[[nodiscard]]` (CODING_RULES §7.9); an infallible writer returning `std::vector<std::byte>` has it.
- Models over a few KB live on the heap (`std::unique_ptr`), as the loaders' results do: MSVC Debug tests have a 1 MB
  stack. Tests create them with `std::make_unique`, never as locals.
- Every narrowing is an explicit `static_cast` (MSVC `/WX`: C4242, C4244). The int16 limit is
  `std::numeric_limits<std::int16_t>::max()`, not a new literal (`check_named_values.py --clean-root engine`).
- English in code, comments, tests, commits and PR text; comments only when strictly necessary, each header with one
  line citing research 05 ("Terreno de un mundo") and the original's loader and saver (CODING_RULES §14).
- One behavior, one test, red first (CODING_RULES §13). Angular commits with scopes `crypto` and `data`.
- One PR for the stage, targeting `main`, assigned to Emanuel; it compiles C++, so CI runs the whole suite. Emanuel
  merges it; nobody else does.

## Decisions taken in this plan

Emanuel reads these before any code.

1. **File models beside the loaders, loaders untouched.** The loaders copy what the original accepts (a negative object
   count reads as none, bytes after the layout are ignored, the `.att`'s high bytes are dropped); a model that must
   write back the same bytes cannot drop them. They share the layout constants the loaders' headers already name
   (`TerrainMappingLayer1Offset`, `TerrainAttributesHeaderSize`, `TerrainObjectSize`, `TerrainHeightsValuesOffset`…).
   Each task has a test that the client's loader reads an edit at the cell it was made.
2. **Bytes, as the files store them.** The original's savers turn floats into bytes (`ftol(alpha × 255)` in `0x5f8960`;
   a scale, a clamp and `ftol` per height in `0x5f92b0`). The editor never holds those floats: its tools (stage 3)
   change bytes, and the client's loaders make the floats. Those conversions are not ported.
3. **Every byte the loader skips is kept:** the `.map`'s and `.obj`'s first byte (the savers write 0, the loaders skip
   it), the plain bytes after the layout of a `.map`, `.obj` or `.OZB` (`tail`), the OZB's 4-byte prefix and BMP header,
   and the high byte of each cell of a two-byte `.att`.
4. **The `.att` is written in the layout `0x5f8390` reads and encrypted as it decrypts it.** The header `00 N FF FF`
   and one byte per cell; the client's `EncTerrain<N>.att` is that under XOR3 and then K16, so the writer applies XOR3
   first and K16 last. A file with two bytes per cell (`0x20004`) keeps that layout and its high bytes. The saver
   `0x5f8690` is not copied: it writes the loose `Terrain.att` of the exporter `0x61ada0` unencrypted, and as its
   cells the first `0x10000` bytes of its WORD table, cells 0 to 32,767 as byte pairs (Q-C1831, found by the review
   of this stage).
5. **Objects are written in the model's order, with `count` = the number of objects.** `0x607540` walks its 256 sector
   lists and counts the deleted objects too (Q-C1830, added in Task 6): a bug not copied. The shipped files are
   mostly grouped by sector, but rewriting them by sector would reorder objects nobody touched. A negative count,
   which the loader reads as none, is refused by `ReadTerrainObjectsFile` (`InvalidData`): it could not be written
   back the same. No shipped file has one.
6. **`TerrainLight.OZJ` gets no code in this stage.** An unpainted light is copied byte for byte (the project does it,
   stage 3), and painting is stage 6. The finding below makes re-encoding a choice for Emanuel, with numbers, in the
   stage 6 plan.
7. **`Client::Tests::EncryptK16` stays** in `Client/tests/support/TerrainFiles.h` as the tests' own encryption, written
   independently of the engine's. The new tests build expected bytes with it and with the helpers beside it
   (`PlainTerrainMapping`, `PlainTerrainAttributes`, `EncryptTerrainAttributes`, `PlainTerrainObjects`,
   `SampleTerrainObjects`, `ObjectBits`, `PlainTerrainHeights`).

## Findings (measured 2026-11-27 on `Client/assets/Data`)

- `.map`: 23 files, all `0x30002` bytes, first byte 0, world byte = folder number (also the `test` copies).
- `.att`: 29 `EncTerrain*.att`: 13 with one byte per cell, 16 with two. High bytes not zero: 348 cells in each of the
  four World31 files with words (`EncTerrain31`, `EncTerrain312`, `EncTerrainTest312`, `EncTerraintest31`), 337 in
  World56's; none elsewhere. Every header is `00 N FF FF`; `EncTerrain312.att` says 31, `EncTerrain351/352.att` say 35.
- `.obj`: 23 files, first byte 0, no bytes after the last object, World6's is the 4-byte header with count 0.
- `TerrainHeight.OZB`: 19 files of 66,620 bytes; in all of them the prefix is the BMP's first 4 bytes (`0x63b280`'s
  rule); World11's header is a 24-bit BMP's, World19's is zeros.
- `TerrainLight.OZJ` (for stage 6): 19 files, the prefix is the JPEG's first 24 bytes in all. Only 7 look like the
  exporter's output (`0x5f8f60` through `0x63b150`: quality 100, 4:2:0, JFIF only: Worlds 1, 2, 3, 4, 5, 7, 10). Seven
  are 4:4:4 with Photoshop markers (World12's progressive) and five carry a comment and lower quality. Decoding and
  re-encoding once with the exporter's settings (measured with Pillow's libjpeg-turbo, close to 6b's) changes pixels
  nobody painted: at most 1 to 11 levels in nine lights, 17 to 38 in six, 78 to 174 in World8, 12, 25 and 32.
- For stage 2: `Client/assets/Data` also holds 24 server-format `.att`: `Terrain<N>.att` (65,539 bytes, XOR3) in Worlds
  1 to 12, equal after XOR3 to the clean server copy's except Worlds 3, 5 and 12, and `Terrain.att` (131,075, plain, two
  bytes per cell) and `TerrainServer.att` (65,539, plain) in Worlds 31, 32, 34, 35, 55 and 56.

## Review Focus

1. **A file longer than its layout** (`.map`, `.obj`, `.OZB`): the original reads it and ignores the rest; the round
   trip keeps those bytes. Tests: `KeepsTheBytesAfterTheLayout` (Task 2), `KeepsTheBytesAfterTheLastObject` (Task 4),
   `KeepsTheBytesAfterTheHeights` (Task 5).
2. **Two-byte `.att` cells with a high byte** (World31, World56): kept on write, dropped by the loader. Tests:
   `ReadsBothBytesOfEachWord`, `WritesWordsWhenTheFileHadThem` (Task 3), and the golden
   `TheWordFilesKeepTheirHighBytes`.
3. **Float bit patterns in objects** (Devias' NaN `y`, World7's subnormals, negative zero, infinities): written back bit
   for bit. Test: `WritesTheBitsOfEveryFloat` (Task 4).
4. **Object counts at the edges**: none (World6), negative, more than 32,767. Tests: `AFileWithNoObjectsIsTheHeader`,
   `ANegativeCountIsAnError`, `MoreObjectsThanTheCountHoldsIsAnError` (Task 4).
5. **A file whose world byte is not its folder's** (`EncTerrain312.att` says 31, a copied file): the file readers keep
   the byte and do not refuse it; comparing it with the folder is the `Document`'s check (stage 3). Tests:
   `DoesNotCheckTheWorld` (Tasks 2 and 3), `KeepsTheWorldByte` (Task 4).

## File structure

```text
Client/src/engine/Crypto/K16.{h,cpp}                         + EncryptK16
Client/src/engine/Data/Terrain/TerrainMappingFile.{h,cpp}    new: EncTerrain<N>.map whole
Client/src/engine/Data/Terrain/TerrainAttributesFile.{h,cpp} new: EncTerrain<N>.att whole
Client/src/engine/Data/Terrain/TerrainObjectsFile.{h,cpp}    new: EncTerrain<N>.obj whole
Client/src/engine/Data/Terrain/TerrainHeightsFile.{h,cpp}    new: TerrainHeight.OZB whole
Client/tests/data/test_k16.cpp                               + the encryption's tests
Client/tests/data/test_terrain_{mapping,attributes,objects,heights}_file.cpp   new, one per format
Client/tests/data/test_golden_world_files.cpp                new: the round trip of every real file
docs/superpowers/specs/2026-11-27-world-editor-design.md     corrections (Task 6)
Client/docs/research/07-quirks-y-bugs.md                     + Q-C1830, Q-C1831 (Task 6)
```

The engine's sources and the tests are globbed (`Client/CMakeLists.txt`, `mu_add_module_tests`): a new file joins on
reconfigure, with no CMake edit. Everything new is in module `engine/Data` or `engine/Crypto` (`Client/modules.tsv`).

Running tests: configure again after adding a test file, then build its target and run its suite, for example
`cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug --target test_k16 && ctest --preset
linux-x64-debug -R '^K16Test\.'`. In the cloud container, without vcpkg, the stub recipe of the project memory
(`cloud-container-build`) builds the same targets; CI (GCC and MSVC) is the final word.

---

### Task 1: `EncryptK16`

**Files:**
- Modify: `Client/src/engine/Crypto/K16.h`, `Client/src/engine/Crypto/K16.cpp`
- Test: `Client/tests/data/test_k16.cpp`

**Interfaces:**
- Produces: `namespace Client::Crypto { ENGINE_API void EncryptK16(std::span<std::byte> bytes); }`, in place, the file
  from its first byte.

- [ ] **Step 1: Write the failing tests** in `test_k16.cpp`. The file already has `using Client::Tests::EncryptK16;`,
  so call the engine's as `Client::Crypto::EncryptK16`.

```cpp
// cipher[i] = (plain[i] + previous) ^ K16Key[i % 16], previous = cipher[i] + 3D, the first previous 5E.
// Bytes 16 and 17 take the key's first two bytes again, with the running previous.
TEST(K16Test, EncryptsEighteenZerosByHand)
{
    std::vector<std::byte> bytes(18);
    Client::Crypto::EncryptK16(bytes);
    EXPECT_EQ(bytes, (std::vector{std::byte{0x8F}, std::byte{0xBF}, std::byte{0xAE}, std::byte{0x1D}, std::byte{0x88},
                                  std::byte{0x5F}, std::byte{0x57}, std::byte{0xB3}, std::byte{0xCE}, std::byte{0xA4},
                                  std::byte{0xB8}, std::byte{0xC4}, std::byte{0x36}, std::byte{0xC0}, std::byte{0x1A},
                                  std::byte{0xF5}, std::byte{0xE3}, std::byte{0x53}}));
}

// FF + 5E wraps to 5D: 5D ^ D1 = 8C. Then (01 + C9) ^ 73 = B9 and (80 + F6) ^ 52 = 24.
TEST(K16Test, EncryptWrapsInEightBits)
{
    std::vector bytes{std::byte{0xFF}, std::byte{0x01}, std::byte{0x80}};
    Client::Crypto::EncryptK16(bytes);
    EXPECT_EQ(bytes, (std::vector{std::byte{0x8C}, std::byte{0xB9}, std::byte{0x24}}));
}

TEST(K16Test, DecryptUndoesEncrypt)
{
    const std::vector<std::byte> plain = BytePattern(1000, 37, 11);
    std::vector<std::byte> bytes = plain;
    Client::Crypto::EncryptK16(bytes);
    DecryptK16(bytes);
    EXPECT_EQ(bytes, plain);
}
```


- [ ] **Step 2: Run them to see them fail.** Build `test_k16`. Expected: does not compile, `EncryptK16` is not a member
  of `Client::Crypto`.
- [ ] **Step 3: Implement `EncryptK16` in `K16.cpp`**, the mirror of `DecryptK16` with the same `uint8_t` casts. In
  `K16.h`, one line beside the decryption's formula: the encryption, and that the savers do it inline (`0x5f8960`,
  `0x607540`).
- [ ] **Step 4: Run `ctest -R '^K16Test\.'`.** Expected: the three new tests and the five old ones pass.
- [ ] **Step 5: Commit** `feat(crypto): add EncryptK16, the inverse of DecryptK16`.

### Task 2: `EncTerrain<N>.map` whole, and the golden round trip

**Files:**
- Create: `Client/src/engine/Data/Terrain/TerrainMappingFile.h`, `TerrainMappingFile.cpp`
- Test: `Client/tests/data/test_terrain_mapping_file.cpp`, `Client/tests/data/test_golden_world_files.cpp` (created
  here, extended by Tasks 3 to 5)

**Interfaces:**
- Consumes: `Crypto::EncryptK16`, `Crypto::DecryptK16`; `TerrainMappingSize`, `TerrainMappingWorldOffset`,
  `TerrainMappingLayer1Offset`, `TerrainMappingLayer2Offset`, `TerrainMappingAlphaOffset` (`TerrainMapping.h`).
- Produces, in `Client::Data::Terrain`:

```cpp
struct TerrainMappingFile
{
    std::uint8_t version = 0;
    std::uint8_t world = 0;
    TerrainLayer<std::uint8_t> layer1{};
    TerrainLayer<std::uint8_t> layer2{};
    TerrainLayer<std::uint8_t> alpha{};
    std::vector<std::byte> tail; // plain bytes after TerrainMappingSize
};
ENGINE_API Core::Expected<std::unique_ptr<TerrainMappingFile>, DataError>
ReadTerrainMappingFile(std::span<const std::byte> file);
[[nodiscard]] ENGINE_API std::vector<std::byte> WriteTerrainMappingFile(const TerrainMappingFile& mapping);
```

  Test helpers in `test_golden_world_files.cpp`'s anonymous namespace, reused by Tasks 3 to 5:
  `std::vector<std::filesystem::path> WorldFiles(const std::filesystem::path& root, std::string_view prefix,
  std::string_view extension)` (every regular file under `root` whose name starts with `prefix` and has `extension`,
  both ignoring ASCII case with `Core::EqualsIgnoringAsciiCase` and `Tests::HasExtension`, sorted by their generic
  relative path). The comparison is the existing `Tests::SameBytes` (`tests/support/SameBytes.h`), whose message
  already gives the sizes or the first offset where they differ.

- [ ] **Step 1: Write the failing unit tests** in `test_terrain_mapping_file.cpp`, suite `TerrainMappingFileTest`.
  `file` below is `Tests::EncryptK16(Tests::PlainTerrainMapping(3))`.
  - `ReadsTheLayersOfTheFile`: `version == 0`, `world == 3`, and for every cell `layer1 == Layer1Pattern(cell)`,
    `layer2 == Layer2Pattern(cell)`, `alpha == AlphaPattern(cell)`; `tail` empty.
  - `WritesTheLayoutOf0x5f8960`: a model made by hand (`make_unique`, world 7, the three patterns) writes
    `Tests::EncryptK16(Tests::PlainTerrainMapping(7))`.
  - `KeepsTheBytesAfterTheLayout`: the plain file plus `01 02 03 04 05 06 07`, encrypted: `tail` is those 7 bytes and
    the write equals the input.
  - `KeepsTheFirstByte`: the plain file with byte 0 = `0x2A`, encrypted: `version == 0x2A`, the write equals the input.
  - `DoesNotCheckTheWorld`: a file whose world byte is 200 reads, with `world == 200`.
  - `AShortFileIsAnError`: `TerrainMappingSize - 1` bytes give `DataError::FileTooShort`.
  - `TheClientsLoaderReadsAnEdit`: read `file`, set `layer2[CellIndex(255, 255)] = 0x11` and `alpha[CellIndex(0, 0)] =
    255`, write; `LoadTerrainMapping(written, 3)` has `layer2` `0x11` at the last cell, `alpha` with the bits of
    `255.0f * TerrainAlphaScale` at the first, and cell 1 of each layer as in `file`.
- [ ] **Step 2: Write the failing golden test** `GoldenWorldFiles.EveryMappingIsWrittenBackTheSame` (fixture
  `using GoldenWorldFiles = Client::Tests::GoldenDataTest;`, root `m_dataRoot`): `WorldFiles(m_dataRoot, "EncTerrain",
  ".map")` has 23 files; each one reads, and its write is `SameBytes` with it (the message names the file and the
  offset).
- [ ] **Step 3: Run both to see them fail.** Expected: do not compile, `TerrainMappingFile.h` does not exist.
- [ ] **Step 4: Implement** `TerrainMappingFile.{h,cpp}`. Read: `FileTooShort` below `TerrainMappingSize`; decrypt a
  copy of the whole file (K16 is one stream, so the tail decrypts with it); slice the layers at the named offsets.
  Write: lay out version, world, the three layers and the tail, then `EncryptK16` over all of it, as `0x5f8960` does
  after writing the plain file.
- [ ] **Step 5: Run `ctest -R '^(TerrainMappingFileTest|GoldenWorldFiles)\.'`** and the old `TerrainMappingTest` and
  `GoldenTerrainMapping`. Expected: all pass.
- [ ] **Step 6: Commit** `feat(data): read and write EncTerrain.map whole`.

### Task 3: `EncTerrain<N>.att` whole

**Files:**
- Create: `Client/src/engine/Data/Terrain/TerrainAttributesFile.h`, `TerrainAttributesFile.cpp`
- Test: `Client/tests/data/test_terrain_attributes_file.cpp`; modify `Client/tests/data/test_golden_world_files.cpp`

**Interfaces:**
- Consumes: Task 1; `Crypto::Xor3`; `TerrainAttributesHeaderSize`, `TerrainAttributesWorldOffset`,
  `TerrainAttributesSizeWithByteCells`, `TerrainAttributesSizeWithWordCells` (`TerrainAttributes.h`); Task 2's test
  helpers.
- Produces, in `Client::Data::Terrain`:

```cpp
struct TerrainAttributesFile
{
    std::uint8_t world = 0;
    TerrainLayer<std::uint8_t> cells{};                  // the byte the game reads; the low one of a WORD
    std::optional<TerrainLayer<std::uint8_t>> highBytes; // only in a 0x20004 file
};
ENGINE_API Core::Expected<std::unique_ptr<TerrainAttributesFile>, DataError>
ReadTerrainAttributesFile(std::span<const std::byte> file);
[[nodiscard]] ENGINE_API std::vector<std::byte> WriteTerrainAttributesFile(const TerrainAttributesFile& attributes);
```

- [ ] **Step 1: Write the failing unit tests**, suite `TerrainAttributesFileTest`, with
  `Tests::EncryptTerrainAttributes(Tests::PlainTerrainAttributes(4, AttributeCells::Bytes or Words))`:
  - `ReadsOneBytePerCell`: `world == 4`, every cell `AttributePattern(cell)`, no `highBytes`.
  - `ReadsBothBytesOfEachWord`: every cell `AttributePattern(cell)`, `highBytes` every cell
    `AttributeHighBytePattern(cell)`.
  - `WritesTheLayoutTheLoaderReads`: a model by hand (world 4, `AttributePattern`, no high bytes) writes the Bytes
    file.
  - `WritesWordsWhenTheFileHadThem`: the same with `highBytes` = `AttributeHighBytePattern` writes the Words file.
  - `KeepsAnAttributeTheLoaderRefuses`: plain cell 0 set to `0x80` before encrypting: reads with `cells[0] == 0x80` and
    writes back the same bytes (`LoadTerrainAttributes` gives `InvalidData` for it; reporting it is stage 3's check).
  - `AnyOtherSizeIsAnError`: `0x10003`, `0x10005` and `0x20003` bytes give `DataError::UnexpectedSize`.
  - `TheHeaderIs00WorldFFFF`: plain headers `01 04 FF FF`, `00 04 FE FF` and `00 04 FF FE` give `InvalidData`.
  - `DoesNotCheckTheWorld`: a header with world 31 reads as `world == 31`, as `EncTerrain312.att`'s does.
  - `TheClientsLoaderReadsAnEdit`: read the Words file, set `cells[CellIndex(255, 255)] = 0x7F`, write;
    `LoadTerrainAttributes(written, 4)` has `0x7F` there, and reading `written` again gives the same `highBytes`.
- [ ] **Step 2: Write the failing golden tests:** `GoldenWorldFiles.EveryAttributesFileIsWrittenBackTheSame` (29 files
  `EncTerrain*.att`) and `GoldenWorldFiles.TheWordFilesKeepTheirHighBytes`: 16 files have `highBytes`; the cells whose
  high byte is not 0 are 348 in each of `world31/EncTerrain31.att`, `EncTerrain312.att`, `EncTerrainTest312.att`,
  `EncTerraintest31.att`, 337 in `world56/EncTerrain56.att`, and 0 in the other 11.
- [ ] **Step 3: Run to see them fail** (do not compile).
- [ ] **Step 4: Implement.** Read: size check, `DecryptK16` then `Xor3` on a copy (`0x5f8390`), header check (`00`,
  any, `FF`, `FF`), cells at `TerrainAttributesHeaderSize + cell × width`, low byte first. Write: header `00 world FF
  FF`, the cells (with their high bytes after each low one when present), then `Xor3`, then `EncryptK16`.
- [ ] **Step 5: Run** the two new suites' tests and the old `TerrainAttributesTest` and `GoldenTerrainAttributes`.
  Expected: all pass.
- [ ] **Step 6: Commit** `feat(data): read and write EncTerrain.att whole`.

### Task 4: `EncTerrain<N>.obj` whole

**Files:**
- Create: `Client/src/engine/Data/Terrain/TerrainObjectsFile.h`, `TerrainObjectsFile.cpp`
- Test: `Client/tests/data/test_terrain_objects_file.cpp`; modify `Client/tests/data/test_golden_world_files.cpp`

**Interfaces:**
- Consumes: Task 1; `TerrainObject`, `TerrainObjectsWorldOffset`, `TerrainObjectsCountOffset`,
  `TerrainObjectsHeaderSize`, `TerrainObjectSize` (`TerrainObjects.h`); `Core::ByteReader`; Task 2's test helpers.
- Produces, in `Client::Data::Terrain`:

```cpp
struct TerrainObjectsFile
{
    std::uint8_t version = 0;
    std::uint8_t world = 0;
    std::vector<TerrainObject> objects;
    std::vector<std::byte> tail; // plain bytes after the last object
};
ENGINE_API Core::Expected<TerrainObjectsFile, DataError> ReadTerrainObjectsFile(std::span<const std::byte> file);
ENGINE_API Core::Expected<std::vector<std::byte>, DataError> WriteTerrainObjectsFile(const TerrainObjectsFile& objects);
```

- [ ] **Step 1: Write the failing unit tests**, suite `TerrainObjectsFileTest`, with
  `Tests::EncryptK16(Tests::PlainTerrainObjects(2, 3, Tests::SampleTerrainObjects(3)))`:
  - `ReadsTheHeaderAndEveryObject`: `version == 0`, `world == 2`, three objects whose `ObjectBits` equal the sample's.
  - `WritesTheLayoutOfTheLoader`: a model by hand with the sample writes that file (`0x607370` reads and `0x607540`
    writes the same layout: version, world, int16 count, 30-byte records).
  - `WritesTheBitsOfEveryFloat`: one object with position bits `0x7FC00001` (a NaN with a payload), `0x80000000`
    (negative zero), `0x00000001` (a subnormal) and scale bits `0xFF800000` (minus infinity): the written plain bytes
    carry those bits at their offsets and reading them back gives the same `ObjectBits`.
  - `AFileWithNoObjectsIsTheHeader`: count 0 and no records (World6's) read as no objects and write the same 4 bytes.
  - `KeepsTheBytesAfterTheLastObject`: 5 plain bytes after the records are the `tail` and the write equals the input.
  - `KeepsTheWorldByte`: world byte 31 reads as `world == 31` and is written back.
  - `ANegativeCountIsAnError`: count -1 gives `DataError::InvalidData`.
  - `AShortFileIsAnError`: 3 bytes, and count 3 with only 2 records, give `DataError::FileTooShort`.
  - `MoreObjectsThanTheCountHoldsIsAnError`: 32,768 objects give `DataError::InvalidData`; 32,767 write
    `4 + 30 × 32,767` bytes.
  - `TheClientsLoaderReadsAnEdit`: read, add 100 to object 1's `position[0]` and append a fourth object, write;
    `LoadTerrainObjects(written, 2)` has four objects with those bits.
- [ ] **Step 2: Write the failing golden test** `GoldenWorldFiles.EveryObjectsFileIsWrittenBackTheSame` (23 files
  `EncTerrain*.obj`).
- [ ] **Step 3: Run to see them fail** (do not compile).
- [ ] **Step 4: Implement.** Read: `FileTooShort` below the header; decrypt a copy; count from
  `TerrainObjectsCountOffset`, `InvalidData` if negative; each record with `ByteReader` and `std::bit_cast` so no bit
  changes (as `TerrainObjects.cpp` reads them); `FileTooShort` if a record is missing; the rest is `tail`. Write:
  `InvalidData` above `std::numeric_limits<std::int16_t>::max()` objects; version, world, count, the records, the
  tail; `EncryptK16`.
- [ ] **Step 5: Run** the new tests and the old `TerrainObjectsTest` and `GoldenTerrainObjects`. Expected: all pass.
- [ ] **Step 6: Commit** `feat(data): read and write EncTerrain.obj whole`.

### Task 5: `TerrainHeight.OZB` whole

**Files:**
- Create: `Client/src/engine/Data/Terrain/TerrainHeightsFile.h`, `TerrainHeightsFile.cpp`
- Test: `Client/tests/data/test_terrain_heights_file.cpp`; modify `Client/tests/data/test_golden_world_files.cpp`

**Interfaces:**
- Consumes: `TerrainHeightsPrefixSize`, `TerrainHeightsBitmapHeaderSize`, `TerrainHeightsValuesOffset`,
  `TerrainHeightsSize`, `TerrainHeightsScale` (`TerrainHeights.h`); Task 2's test helpers.
- Produces, in `Client::Data::Terrain`:

```cpp
struct TerrainHeightsFile
{
    std::array<std::byte, TerrainHeightsPrefixSize> prefix{};
    std::array<std::byte, TerrainHeightsBitmapHeaderSize> bitmapHeader{};
    TerrainLayer<std::uint8_t> heights{};
    std::vector<std::byte> tail;
};
ENGINE_API Core::Expected<std::unique_ptr<TerrainHeightsFile>, DataError>
ReadTerrainHeightsFile(std::span<const std::byte> file);
[[nodiscard]] ENGINE_API std::vector<std::byte> WriteTerrainHeightsFile(const TerrainHeightsFile& heights);
```

  The file is not encrypted. `0x5f92b0` writes back the header it read (`0x07d0ced8`) and the heights in table order
  (it reverses the rows twice), and `0x63b280` puts the BMP's first 4 bytes before it: no row flip anywhere.

- [ ] **Step 1: Write the failing unit tests**, suite `TerrainHeightsFileTest`, with `Tests::PlainTerrainHeights()`:
  - `ReadsThePrefixTheHeaderAndTheHeights`: `prefix` and `bitmapHeader` are the file's bytes 0 to 3 and 4 to `0x43B`,
    every cell `HeightPattern(cell)`.
  - `WritesWhatItRead`: the write equals the file.
  - `HeightsAreRowByRow`: `heights[CellIndex(0, 1)]` is the file's byte at `TerrainHeightsValuesOffset + 256`.
  - `KeepsTheBytesAfterTheHeights`: 3 bytes more are the `tail`, the write equals the input.
  - `AShortFileIsAnError`: `TerrainHeightsSize - 1` bytes give `DataError::FileTooShort`.
  - `TheClientsLoaderReadsAnEdit`: `heights[CellIndex(255, 255)] = 200`, write; `LoadTerrainHeights(written, 1)` has
    `200.0f * TerrainHeightsScale` there.
- [ ] **Step 2: Write the failing golden test** `GoldenWorldFiles.EveryHeightsFileIsWrittenBackTheSame` (19 files
  `TerrainHeight.OZB`).
- [ ] **Step 3: Run to see them fail** (do not compile).
- [ ] **Step 4: Implement:** slices in and out at the named offsets.
- [ ] **Step 5: Run** the new tests and the old `TerrainHeightsTest` and `GoldenTerrainHeights`. Expected: all pass.
- [ ] **Step 6: Commit** `feat(data): read and write TerrainHeight.OZB whole`.

### Task 6: Close the stage: the spec, the quirk and the PR

**Files:**
- Modify: `docs/superpowers/specs/2026-11-27-world-editor-design.md`, `Client/docs/research/07-quirks-y-bugs.md`

- [ ] **Step 1: Correct the spec** with what this stage found:
  - "What a 1.02 world is", the writers' column: `.att` `0x5f8690` "(the loose `Terrain.att`: the header and the
    first `0x10000` bytes of its WORD table, unencrypted, Q-C1831)"; `.obj` `0x607540` "(by sector; its count
    includes deleted objects, Q-C1830)"; `TerrainLight.OZJ` `0x5f8f60` "(JPEG through `0x63b150`, quality 100) and
    `0x63b280` (the prefix)".
  - "Byte-exact", promise 2: drop "Where the original's writer is not located (`.obj`)…"; say that objects keep the
    document's order (this plan's decision 5) and that the `.att` is encrypted as its loader decrypts it (decision 4).
  - "Byte-exact", the light paragraph: add this plan's measurement and that stage 6's plan decides the encoding with
    Emanuel.
  - "Stages", stage 1: the four formats and `EncryptK16`; the light's encoder moves to stage 6.
  - "Left to the builder": drop the two items this stage located (the `.obj` writer, the light's JPEG settings).
- [ ] **Step 2: Add Q-C1830** to the catalog of `07-quirks-y-bugs.md`, after Q-C28 (the other `EncTerrain.obj` row),
  in Spanish like its other 824 rows: `0x607540`, the `.obj` saver of the editor mode, walks the 256 sector lists
  from `0x7d325d4`, writes only the live objects (`+0x0` nonzero) but counts every node, so a file saved with deleted
  objects still in the lists declares more objects than it holds, and `0x607370` asks for records that are not there.
  Its counter is an `int` written in 2 bytes, so past 32,767 nodes it wraps. Class: bug. MuRet:
  `WriteTerrainObjectsFile` writes the number of records it writes
  (`TerrainObjectsFileTest.WritesTheLayoutOfTheLoader`); the old behavior lives in dead code nothing runs, so it gets
  no test. Wave: world editor, stage 1. Then Q-C1831
  after it, found by the stage's review: `0x5f8690`, the `.att` saver, writes the header and `fwrite(0x7d0d348,
  0x10000)`, the first half of its WORD table: cells 0 to 32,767 as low and high byte pairs, so `0x5f8390` gives every
  odd cell a high byte (0) and rows 128 to 255 are lost. Class: bug. MuRet: `WriteTerrainAttributesFile` writes the
  layout the loader reads (`TerrainAttributesFileTest.WritesTheLayoutTheLoaderReads`); dead code, no test of the old
  behavior.
- [ ] **Step 3: Verify the whole stage:** `ctest -R
  '^(K16Test|TerrainMapping|TerrainAttributes|TerrainObjects|TerrainHeights|GoldenTerrain|GoldenWorldFiles)'` passes;
  `check_layers.py` and `check_named_values.py` pass with the arguments `.github/workflows/ci.yml` gives them;
  `clang-tidy -checks='-*,misc-include-cleaner'` reports nothing on the new files.
- [ ] **Step 4: Commit** `docs(editor): record what stage 1 of the world editor found`.
- [ ] **Step 5: Open the PR** `feat(data): write the world's client files byte-exact (world editor, stage 1)` against
  `main`, assigned to Emanuel: what each format's model keeps, the decisions above, the findings, the tests. Watch its
  CI until green; do not merge.
