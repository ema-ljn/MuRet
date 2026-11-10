#include "Scenes/WorldModels.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/Textures/Sampling.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameModels.h"
#include "Scenes/LoginModels.h"
#include "World/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Client::Scenes::WorldModels con el Data\ del repo: los modelos de Object56\ que carga 0x616ec0 para
// el mapa 55 (el k de Object<k + 1>.bmd) y sus texturas, que siguen la tabla de la carga desde 0x41c
// (06 § El mundo del login).
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Render::TextureSlot;
using Client::Scenes::WorldModels;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingSink;

constexpr std::uint8_t Login = 55;
constexpr std::uint8_t Lorencia = 0;
constexpr std::uint8_t Devias = 2;

using GoldenWorldModels = Client::Tests::GoldenDataTest;

// 0x616ec0: los 112 pedidos con nombre de 0x619a21..0x619ea1 y los dos especiales.
// Volver al lazo Object<k+1> deja sin modelo incluso Tree01 y Beer03.
TEST_F(GoldenWorldModels, LorenciaLoadsTheNamedObjects)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, nullptr).HasValue());

    EXPECT_EQ(models.Count(), 114u);
    constexpr std::array<std::uint16_t, 10> Named{0x00, 0x37, 0x5a, 0x5b, 0x5c, 0x5d, 0x5f, 0x60, 0x85, 0x99};
    for (const std::uint16_t model : Named)
    {
        EXPECT_NE(models.Find(model), nullptr) << std::hex << model;
        EXPECT_NE(models.SkeletonOf(model), nullptr) << std::hex << model;
    }
    // Sin GameModels previo: Bird/Fish más 102 texturas de objetos. Las tres globales
    // que el caso compuesto reutiliza acá reciben sus propios slots de Object1.
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->ToLoad().size(), 104u);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x482});
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x478}), "bottle.tga");
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x480}), "pot3.tga");
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x481}), "apple.jpg");
}

// El consumidor real de la tabla: los 109 tipos que piden los 2.870 registros de World1.
TEST_F(GoldenWorldModels, EveryLorenciaObjectTypeHasItsNamedModelAndSkeleton)
{
    const auto objects = Client::World::LoadWorldObjects(m_dataRoot, Lorencia);
    ASSERT_TRUE(objects.HasValue()) << objects.Error();
    ASSERT_EQ(objects.Value().size(), 2870u);
    std::set<std::int16_t> types;
    for (const Client::Data::Terrain::TerrainObject& object : objects.Value())
        types.insert(object.type);
    ASSERT_EQ(types.size(), 109u);

    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, nullptr).HasValue());
    for (const std::int16_t type : types)
    {
        ASSERT_GE(type, 0);
        const auto model = static_cast<std::uint16_t>(type);
        ASSERT_NE(models.Find(model), nullptr) << model;
        EXPECT_NE(models.SkeletonOf(model), nullptr) << model;
    }
}

// Pedidos especiales 0x617356/0x617387, antes de reiniciar el cursor a 0x41c. El volcado
// lor_slots.bin ya tiene bottle.tga en 0x30f, pot3.tga en 0x2b3 y apple.jpg en 0x2b4:
// con esa tabla global se piden 99 texturas nuevas, no las 102 de una tabla aislada.
TEST_F(GoldenWorldModels, LorenciaAssignsTheBirdAndFishBeforeTheObjectTextures)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Scenes::GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x30f}), "bottle.tga");
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x2b3}), "pot3.tga");
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x2b4}), "apple.jpg");
    ASSERT_EQ(game.TextureSlots().NameOf(TextureSlot{0x69}), "doorc01.jpg");
    ASSERT_TRUE(game.TextureSlots().NameOf(TextureSlot{0x6a}).empty());
    ASSERT_TRUE(store.Find(TextureSlot{0x69}).has_value());
    ASSERT_FALSE(store.Find(TextureSlot{0x6a}).has_value());
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, &game.TextureSlots()).HasValue());

    const auto bird = models.MeshTextures(0xad);
    const auto fish = models.MeshTextures(0xb2);
    ASSERT_EQ(bird.size(), 1u);
    ASSERT_EQ(fish.size(), 1u);
    EXPECT_EQ(bird.front(), TextureSlot{0x69});
    EXPECT_EQ(fish.front(), TextureSlot{0x6a});
    const auto tree = models.MeshTextures(0);
    ASSERT_FALSE(tree.empty());
    EXPECT_EQ(tree.front(), TextureSlot{0x41c});
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->ToLoad().size(), 101u);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x47f});
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x69}), "bird.tga");
    EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{0x6a}), "fish.tga");
    constexpr std::array<TextureSlot, 3> Sampled{TextureSlot{0x69}, TextureSlot{0x6a}, TextureSlot{0x41c}};
    for (const TextureSlot slot : Sampled)
    {
        const auto texture = store.Find(slot);
        ASSERT_TRUE(texture.has_value());
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Nearest);
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
    }
    models.Release();
    for (const TextureSlot slot : Sampled)
        EXPECT_FALSE(store.Find(slot).has_value());
    constexpr std::array<TextureSlot, 3> Shared{TextureSlot{0x30f}, TextureSlot{0x2b3}, TextureSlot{0x2b4}};
    for (const TextureSlot slot : Shared)
        EXPECT_TRUE(store.Find(slot).has_value());
}

// 0x61744d..0x6174d9: en Devias, el burro (0x201, obj_donkey) y la bandera (0x202, obj_flag) de Npc\, con sus
// texturas desde 0x69 (0x60d530) y de Npc\, antes de los objetos de Object3\, que van desde 0x41c.
TEST_F(GoldenWorldModels, DeviasLoadsTheDonkeyAndTheFlagBeforeItsObjects)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Devias, store, log, nullptr).HasValue());

    for (const std::uint16_t special : {std::uint16_t{0x201}, std::uint16_t{0x202}})
    {
        SCOPED_TRACE(special);
        ASSERT_NE(models.Find(special), nullptr);
        ASSERT_NE(models.SkeletonOf(special), nullptr);
        const auto textures = models.MeshTextures(special);
        ASSERT_FALSE(textures.empty());
        for (const TextureSlot slot : textures)
        {
            EXPECT_GE(slot.value, 0x69);
            EXPECT_LT(slot.value, 0x41c);
        }
    }
    EXPECT_EQ(models.MeshTextures(0x201).front(), TextureSlot{0x69});
    EXPECT_NE(models.Find(0), nullptr);
    ASSERT_FALSE(models.MeshTextures(0).empty());
    EXPECT_EQ(models.MeshTextures(0).front(), TextureSlot{0x41c});
    EXPECT_EQ(models.Find(0xad), nullptr);
    models.Release();
    EXPECT_EQ(store.Size(), 0u);
}

// El mapa de selección 54 conserva el lazo genérico: los 77 ObjectNN de Object55.
TEST_F(GoldenWorldModels, World55KeepsTheGenericObjects)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, 54, store, log, nullptr).HasValue());
    EXPECT_EQ(models.Count(), 77u);
    EXPECT_NE(models.Find(0), nullptr);
    EXPECT_NE(models.Find(1), nullptr);
    EXPECT_EQ(models.Find(0xad), nullptr);
    EXPECT_EQ(models.Find(0xb2), nullptr);
    EXPECT_EQ(models.Find(Client::Scenes::DragonModel), nullptr);
}

using GoldenLorenciaModelTextureSlots = Client::Tests::MemoryDumpTest;

// Nombres de textura del original: solo metadata, sin copiar el volcado al repo.
TEST_F(GoldenLorenciaModelTextureSlots, TheWorldNamesMatchTheNativeTable)
{
    const auto dump = DumpFile(Client::Tests::LorenciaCapture, "lor_slots.bin");
    ASSERT_TRUE(dump.has_value());
    const std::optional<std::vector<char>> table = Client::Tests::ReadFileBytes(*dump);
    ASSERT_TRUE(table.has_value());
    constexpr std::size_t SlotBytes = 0x38;
    constexpr std::size_t NameBytes = 0x20;
    ASSERT_EQ(table->size(), Client::Render::OriginalTextureSlotCount * SlotBytes);
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Scenes::GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Lorencia, store, log, &game.TextureSlots()).HasValue());
    ASSERT_NE(models.TextureSlots(), nullptr);

    std::size_t compared = 0;
    for (std::uint16_t slot = 0x69; slot < 0x47f; ++slot)
    {
        if (slot > 0x6a && slot < 0x41c)
            continue;
        const std::string_view record(table->data() + (slot * SlotBytes), NameBytes);
        const std::string_view name = record.substr(0, record.find('\0'));
        EXPECT_EQ(models.TextureSlots()->NameOf(TextureSlot{slot}), name) << std::hex << slot;
        ++compared;
    }
    EXPECT_EQ(compared, 101u);
}

// El modelo k es Object<k + 1>.bmd: en el Data\ del laboratorio están 79 de los 160 (del Object02 al
// Object91 con huecos), y en el mapa 55 también el dragón.
TEST_F(GoldenWorldModels, World56LoadsObjectKPlusOne)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    const auto loaded = models.Load(m_dataRoot, Login, store, log, nullptr);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();

    EXPECT_EQ(models.Count(), 80u) << "79 objetos y el dragón";
    EXPECT_EQ(models.Find(0), nullptr) << "Object01.bmd no está";
    EXPECT_NE(models.Find(1), nullptr) << "Object02.bmd";
    EXPECT_NE(models.Find(33), nullptr) << "Object34.bmd, el dragón de piedra";
    EXPECT_NE(models.Find(90), nullptr) << "Object91.bmd, el remolino 90";
    EXPECT_EQ(models.Find(91), nullptr) << "Object92.bmd no está";
    ASSERT_NE(models.SkeletonOf(83), nullptr) << "las hojas, con su acción de 13 keys";
    EXPECT_EQ(models.SkeletonOf(83)->actions.front().keyCount, 13);
}

// Uno que falta se saltea sin avisar (Q-C23): el tipo 82, que pide el .obj, queda sin modelo.
TEST_F(GoldenWorldModels, AMissingModelIsSkipped)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, nullptr).HasValue());
    EXPECT_EQ(models.Find(82), nullptr) << "Object83.bmd no está";
    EXPECT_EQ(models.SkeletonOf(82), nullptr);
    EXPECT_TRUE(models.MeshTextures(82).empty());
    for (std::size_t k = 0; k < sink.lines.size(); ++k)
    {
        if (sink.levels[k] == LogLevel::Warning)
        {
            EXPECT_EQ(sink.lines[k].find("Object83"), std::string::npos) << sink.lines[k];
        }
    }
}

// 0x616ec0 en el mapa 55 (0x6188ae): el dragón que vuela es el modelo 0xb4, de Object56\Dragon.bmd
// (dragon.bmd en el Data\, Q-C127), con 3 mallas, 93 huesos y una acción de 41 keys, y sus texturas
// desde 0x60d530(0x69).
TEST_F(GoldenWorldModels, TheDragonIsModel0xb4)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, nullptr).HasValue());
    const auto* dragon = models.Find(Client::Scenes::DragonModel);
    ASSERT_NE(dragon, nullptr);
    EXPECT_EQ(dragon->meshes.size(), 3u);
    EXPECT_EQ(dragon->bones.size(), 93u);
    ASSERT_NE(models.SkeletonOf(Client::Scenes::DragonModel), nullptr);
    ASSERT_EQ(models.SkeletonOf(Client::Scenes::DragonModel)->actions.size(), 1u);
    EXPECT_EQ(models.SkeletonOf(Client::Scenes::DragonModel)->actions.front().keyCount, 41);
    const auto slots = models.MeshTextures(Client::Scenes::DragonModel);
    ASSERT_EQ(slots.size(), 3u);
    EXPECT_EQ(slots[0], TextureSlot{0x69});
    EXPECT_EQ(slots[1], TextureSlot{0x6a});
    EXPECT_EQ(slots[2], TextureSlot{0x6b});
    // Ningún otro mapa lo tiene.
    WorldModels lorencia;
    ASSERT_TRUE(lorencia.Load(m_dataRoot, 0, store, log, nullptr).HasValue());
    EXPECT_EQ(lorencia.Find(Client::Scenes::DragonModel), nullptr);
}

// 0x60d530(0x41c) y las texturas de cada modelo en el orden de k, con GL_NEAREST y repitiendo
// (0x60d5f0(k, "Object56\\", 0x2901, 0x2600, 1)). Los 65 nombres de Object56\ se buscan primero en la
// tabla: los tres del dragón están en 0x69..0x6b, y en la del original (lor_slots.bin, las de 0x615700)
// están castlegate02, cswalltop y castlegatehide en 0x3e5..0x3e7, y fire02 y fire01 en 0x3ef y 0x3f0. Las
// 57 nuevas van del 0x41c al 0x454 y el logo queda en 0x455 (Q-C92).
TEST_F(GoldenWorldModels, TheLogoTexturesFollowTheWorld)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const auto meshesNamed = [](std::initializer_list<const char*> names)
    {
        Client::Data::Models::Model model = Client::Tests::SampleModel();
        model.meshes.clear();
        for (const char* name : names)
        {
            model.meshes.push_back(Client::Tests::SampleMesh());
            model.meshes.back().textureName = name;
        }
        return model;
    };
    Client::Render::Models::ModelTextureSlots table(TextureSlot{0x3e5});
    table.Assign(meshesNamed({"castlegate02.jpg", "cswalltop.JPG", "castlegatehide.jpg"}), "Skill\\", {});
    table.SetNext(TextureSlot{0x3ef});
    table.Assign(meshesNamed({"fire02.jpg", "fire01.jpg"}), "Skill\\", {});
    WorldModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, Login, store, log, &table).HasValue());

    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->Next(), Client::Scenes::LoginLogoFirstSlot);
    for (std::uint16_t slot = 0x41c; slot < 0x455; ++slot)
    {
        const auto texture = store.Find(TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << std::hex << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Nearest) << std::hex << slot;
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat) << std::hex << slot;
    }
    EXPECT_FALSE(store.Find(TextureSlot{0x455}).has_value());
    // La malla 0 del Object02 es la primera textura nueva.
    ASSERT_FALSE(models.MeshTextures(1).empty());
    EXPECT_EQ(models.MeshTextures(1).front(), TextureSlot{0x41c});

    models.Release();
    EXPECT_FALSE(store.Find(TextureSlot{0x41c}).has_value());
}

// Con la tabla de GameModels, que todavía no tiene las de 0x615700 más allá de las cuatro primeras
// (Data::Models::SkillTextureCalls), los cinco nombres de las habilidades se piden de Object56\: los tres
// del castillo están y suben, fire02 y fire01 no y se saltean con un aviso, el login sigue y el logo
// queda cinco más allá.
TEST_F(GoldenWorldModels, TwoTexturesFromAnotherFolderAreSkippedWithAWarning)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Scenes::GameModels game;
    ASSERT_TRUE(game.Load(m_dataRoot, store, log).HasValue());
    WorldModels models;
    const auto loaded = models.Load(m_dataRoot, Login, store, log, &game.TextureSlots());
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x45a});
    std::size_t warnings = 0;
    for (std::size_t k = 0; k < sink.lines.size(); ++k)
    {
        if (sink.levels[k] == LogLevel::Warning && sink.lines[k].find("Object56") != std::string::npos)
        {
            ++warnings;
        }
    }
    EXPECT_EQ(warnings, 2u);
    ASSERT_FALSE(models.MeshTextures(89).empty()) << "el fuego, Object90.bmd";
}

// 0x619bc2 carga SteelWall después de StoneWall, pero 0x619eb7 asigna texturas por ID.
// Estos BMD propios distinguen ambos órdenes sin Data original ni volcado de memoria.
TEST(WorldModelsTest, LorenciaAssignsNamedTexturesByIdAfterLoadingTheSpecials)
{
    Client::Tests::TemporaryFolder folder{"lorencia-texture-id-order"};
    struct Request
    {
        std::string_view file;
        std::string_view texture;
    };
    constexpr std::array<Request, 4> Requests{{{"Bird01.bmd", "test-bird.jpg"},
                                               {"Fish01.bmd", "test-fish.jpg"},
                                               {"StoneWall01.bmd", "test-stone.jpg"},
                                               {"SteelWall01.bmd", "test-steel.jpg"}}};
    constexpr std::uint8_t ClearVersion = 0x0a;
    for (const Request& request : Requests)
    {
        Client::Data::Models::Model model = Client::Tests::SampleModel();
        model.meshes.front().textureName = request.texture;
        Client::Tests::WriteSceneFile(folder.Path(), std::filesystem::path("Object1") / request.file,
                                      Client::Tests::PlainBmd(Client::Tests::BmdContent(model), ClearVersion));
    }
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(folder.Path(), Lorencia, store, log, nullptr).HasValue());
    const auto expectSlot = [&](std::uint16_t model, TextureSlot expected)
    {
        const auto slots = models.MeshTextures(model);
        ASSERT_EQ(slots.size(), 1u) << model;
        EXPECT_EQ(slots.front(), expected) << model;
    };
    expectSlot(0xad, TextureSlot{0x69});
    expectSlot(0xb2, TextureSlot{0x6a});
    expectSlot(0x41, TextureSlot{0x41c});
    expectSlot(0x45, TextureSlot{0x41d});
    ASSERT_NE(models.TextureSlots(), nullptr);
    EXPECT_EQ(models.TextureSlots()->Next(), TextureSlot{0x41e});
    EXPECT_EQ(models.TextureSlots()->ToLoad().size(), 4u);
}

// Los BMD truncados hacen observable cada pedido mediante su aviso inmediato. Esto no
// registra la intercalación de Load/Assign cuando los modelos son válidos.
TEST(WorldModelsTest, LorenciaRequestsTheSpecialsAndNamedModelsInCallOrder)
{
    Client::Tests::TemporaryFolder folder{"lorencia-model-call-order"};
    const std::filesystem::path objects = folder.Path() / "Object1";
    ASSERT_TRUE(std::filesystem::create_directory(objects));
    constexpr std::array<std::string_view, 7> Files{"Bird01.bmd",      "Fish01.bmd",   "StoneWall01.bmd",
                                                    "SteelWall01.bmd", "Cannon01.bmd", "Bridge01.bmd",
                                                    "Object01.bmd"};
    for (const std::string_view name : Files)
    {
        std::ofstream file(objects / name, std::ios::binary);
        file << "BMD";
        file.close();
        ASSERT_TRUE(file.good()) << name;
    }
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(folder.Path(), Lorencia, store, log, nullptr).HasValue());

    constexpr std::string_view UnreadableModel = "un objeto del mundo no se puede leer y no se dibuja: ";
    std::vector<std::string> requests;
    for (std::size_t index = 0; index < sink.lines.size(); ++index)
    {
        const std::size_t message = sink.lines[index].find(UnreadableModel);
        if (sink.levels[index] == LogLevel::Warning && message != std::string::npos)
            requests.push_back(sink.lines[index].substr(message + UnreadableModel.size()));
    }
    const std::vector<std::string> Expected{"Data\\Object1\\Bird01.bmd",      "Data\\Object1\\Fish01.bmd",
                                            "Data\\Object1\\StoneWall01.bmd", "Data\\Object1\\SteelWall01.bmd",
                                            "Data\\Object1\\Cannon01.bmd",    "Data\\Object1\\Bridge01.bmd"};
    EXPECT_EQ(requests, Expected);
}

// Sin ningún modelo (sin la carpeta Object56, o vacía) el login va sin objetos, y lo dice en el log.
TEST(WorldModelsTest, WithoutAnyModelThereIsAWarning)
{
    Client::Tests::TemporaryFolder empty{"world-models-empty"};
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    WorldModels models;
    ASSERT_TRUE(models.Load(empty.Path(), Login, store, log, nullptr).HasValue());
    EXPECT_EQ(models.Count(), 0u);
    bool warned = false;
    for (std::size_t k = 0; k < sink.lines.size(); ++k)
    {
        warned = warned || (sink.levels[k] == LogLevel::Warning && sink.lines[k].find("Object56") != std::string::npos);
    }
    EXPECT_TRUE(warned);
}
} // namespace
