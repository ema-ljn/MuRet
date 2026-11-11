#include "Scenes/GameHanging.h"

#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "World/Sprites.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// DrawGameHanging (0x50db57..0x50e9fa y 0x509340) sobre un cuerpo sintético: el hueso k del cuerpo y de
// cada modelo que cuelga está en (0, 0, k) sin giro, así cada punto deja ver de qué hueso sale. Los
// esperados salen del ASM de c32bbd5f, sin ejecutar el original.
namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::GameHangingPass;

constexpr std::uint16_t Player = 0x20a;
constexpr std::uint16_t Sword = 0x212;
constexpr std::uint16_t BladeSword = 0x21d; // el destello a −150 de 0x50e921
constexpr std::uint16_t Sword224 = 0x224;
constexpr std::uint16_t Sword223 = 0x223;
constexpr std::uint16_t Bolts = 0xa19;
constexpr std::uint16_t Staff08 = 0xc19;
constexpr std::uint16_t Helper02 = 0x1c13;
constexpr std::uint16_t MuSign = 0x12a;
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t Shiny = 0x5fd;
constexpr std::size_t BoneCount = 48;
constexpr float PlayerSecondActionSpeed = 0.75f;

// El rand() de la CRT con la semilla 1 después de `count` llamadas.
std::uint32_t AfterRandoms(std::size_t count)
{
    Client::Render::Models::CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

// [ebp+0xc] de 0x50ba30 con el primer rand() de la semilla 1 (41): (41 % 30 + 70) · 0,01 en la x87.
float FirstLum()
{
    return static_cast<float>(static_cast<double>(81.0f) * static_cast<double>(0.01f));
}

class HangingModels final : public Client::Scenes::IModelLibrary
{
public:
    explicit HangingModels(std::set<std::uint16_t> items) : m_items(std::move(items))
    {
        using namespace Client::Render::Models;
        SkeletonBone bone;
        bone.parent = -1;
        // La acción 1 tiene dos keys: lo que cuelga en ella puede avanzar.
        bone.actions = {{BoneKey{}}, {BoneKey{}, BoneKey{}}};
        m_skeleton.bones.assign(BoneCount, bone);
        for (std::size_t index = 0; index < BoneCount; ++index)
        {
            for (auto& keys : m_skeleton.bones[index].actions)
            {
                for (auto& key : keys)
                    key.position = {0.0f, 0.0f, static_cast<float>(index)};
            }
        }
        m_skeleton.actions = {
            ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false},
            ActionInfo{.repeats = true, .speed = PlayerSecondActionSpeed, .keyCount = 2, .lockPositions = false}};
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "item.jpg";
        m_model.meshes = {mesh};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return m_items.contains(model) ? &m_model : nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Player || m_items.contains(model) ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return m_texture;
    }

private:
    std::set<std::uint16_t> m_items;
    Client::Render::Models::Skeleton m_skeleton;
    Client::Data::Models::Model m_model;
    std::array<Client::Render::TextureSlot, 1> m_texture{Client::Render::TextureSlot{0x300}};
};

class GameHangingTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_batch.Begin(Client::Render::Pass3D{});
        for (std::size_t index = 0; index < BoneCount; ++index)
        {
            Client::Render::Models::BoneTransform bone(1.0f);
            bone[3] = {0.0f, 0.0f, static_cast<float>(index)};
            m_body.push_back(bone);
        }
        m_actor.type = Player;
        m_actor.word376 = 0xffff;
        m_actor.scale = 1.0f;
        m_actor.alpha = 1.0f;
        m_actor.position = {150.0f, 150.0f, 10.0f};
        m_actor.drawnLight = {0.5f, 0.5f, 0.5f};
        m_actor.wings.model = 0xffff;
        m_actor.pet.model = 0xffff;
        for (auto& hand : m_actor.hands)
            hand.model = 0xffff;
        m_actor.hands[0].bone = 0x21;
        m_actor.hands[1].bone = 0x2a;
    }

    GameHangingPass Pass(const Client::Scenes::IModelLibrary& models)
    {
        return {.batch = m_batch,
                .models = models,
                .body = m_body,
                .random = m_random,
                .carry = &m_carry,
                .sprites = &m_sprites,
                .logicSteps = 1};
    }

    std::vector<Client::World::Sprite> LiveSprites()
    {
        std::vector<Client::World::Sprite> live;
        for (const auto& sprite : m_sprites.Sprites())
        {
            if (sprite.alive)
                live.push_back(sprite);
        }
        return live;
    }

    ActorState m_actor;
    std::vector<Client::Render::Models::BoneTransform> m_body;
    Client::Render::ModelBatch m_batch;
    Client::Render::Models::CrtRandom m_random{1};
    Client::Scenes::FaunaDrawCarry m_carry;
    Client::World::FrameSprites m_sprites;
};

// Sin nada que cuelgue quedan el rand() de 0x50db57 y, fuera de la espalda, el cierre de +0x38e antes de
// cada mano. Sin buffer ni sprites no hace falta nada.
TEST_F(GameHangingTest, WithNothingHangingOnlyTheLightAndTheTwoHandBytesSpendRandoms)
{
    const HangingModels models({});
    GameHangingPass pass = Pass(models);
    pass.carry = nullptr;
    pass.sprites = nullptr;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
}

// 0x50ddde..0x50de60 con '4' en +0x38e (0x509926): el arma de la espalda usa la parte de las alas, con la
// velocidad de la acción 1 del jugador, y 0x509340 le deja la key anterior en 2 y el frame en 3,2.
// Al volver, 0x50de5a repone solo el frame de las alas: la key y la acción anteriores quedan (Q-C296).
TEST_F(GameHangingTest, TheBackWeaponLeavesTheWingsWithTheirFrameButItsPreviousKey)
{
    const HangingModels models({Sword});
    m_actor.safeZone = 1;
    m_actor.hands[0].model = Sword;
    m_actor.byte38e = 0x34;
    m_actor.animation.action = 0x30;
    m_actor.animation.frame = 6.0f;
    m_actor.wings.animation = {.action = 0, .frame = 7.5f, .previousAction = 9, .previousKey = 5};

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.wings.bone, 0x2f);
    EXPECT_EQ(m_actor.wings.speed, PlayerSecondActionSpeed);
    EXPECT_EQ(m_actor.wings.animation.frame, 7.5f);
    EXPECT_EQ(m_actor.wings.animation.previousKey, 2);
    EXPECT_EQ(m_actor.wings.animation.previousAction, 9);
    // A la espalda no corre el lazo de las manos: el de 0x50db57, +0x38e en 0x509340 y la luz de 0x509a92.
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(130.0f, 155.0f, 97.0f));
}

// 0x50dcda..0x50dd70 fuera de la espalda: las saetas de la izquierda van al hueso de las alas con el lugar
// de los arcos (70, 0, 90 y −10, 5, 10), y el lazo de las manos no las vuelve a dibujar.
TEST_F(GameHangingTest, BoltsInTheLeftHandHangOnTheBackOutsideTheSafeZone)
{
    const HangingModels models({Bolts});
    m_actor.hands[1].model = Bolts;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(140.0f, 155.0f, 67.0f));
    // 0x50db57, +0x38e y la luz en 0x509340, y +0x38e antes de cada mano.
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// 0x50e896..0x50e9ea: el destello de 0x21d a −150 del hueso de la mano, con la luz del nivel 7
// (0,5, 0,4 y 0,3 de [ebp+0xc]). Los rojos (+0x375 desde 6) y +0x1d6 en 4 no lo llevan.
TEST_F(GameHangingTest, TheHandSwordFlareUsesTheLevelLight)
{
    const HangingModels models({BladeSword});
    m_actor.hands[0].model = BladeSword;
    m_actor.hands[0].level = 7;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    // Con el nivel 7, 0x60b3d0 dibuja la malla y su pasada del brillo.
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(150.0f, 150.0f, 43.0f));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 1u);
    const float lum = FirstLum();
    EXPECT_EQ(sprites[0].texture, Flare);
    EXPECT_EQ(sprites[0].position, glm::vec3(150.0f, 0.0f, 43.0f));
    EXPECT_EQ(sprites[0].scale, 1.4f);
    EXPECT_EQ(sprites[0].light, glm::vec3(lum * 0.5f, lum * 0.4f, lum * 0.3f));
    // 0x50db57, +0x38e de la mano y de 0x509340, la luz de 0x509a92 y +0x38e de la izquierda.
    EXPECT_EQ(m_random.State(), AfterRandoms(5));

    for (const bool red : {true, false})
    {
        SCOPED_TRACE(red);
        m_sprites.Clear();
        m_actor.pk = static_cast<std::uint8_t>(red ? 6 : 0);
        m_actor.level = static_cast<std::uint16_t>(red ? 0 : 4);
        ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, Pass(models)).HasValue());
        EXPECT_TRUE(LiveSprites().empty());
    }
}

// 0x50f6d6..0x50f74b: Staff08 lleva Shiny02 en el hueso 27 y destellos del 28 al 37 de su propia pose,
// que 0x509340 dejó en el buffer global (0x5092d0): su raíz es el hueso 33 del cuerpo. 0x607b60 le saca
// un rand() más.
TEST_F(GameHangingTest, TheStaffSpritesComeFromItsPoseInTheGlobalBuffer)
{
    const HangingModels models({Staff08});
    m_actor.hands[0].model = Staff08;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 11u);
    const float lum = FirstLum();
    const glm::vec3 light{lum * 0.4f, lum * 0.6f, lum};
    for (std::size_t index = 0; index < sprites.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(sprites[index].texture, index == 0 ? Shiny : Flare);
        EXPECT_EQ(sprites[index].scale, index == 0 ? 2.0f : 1.5f);
        EXPECT_EQ(sprites[index].position, glm::vec3(150.0f, 150.0f, static_cast<float>(10 + 33 + 27 + index)));
        EXPECT_EQ(sprites[index].light, light);
    }
    EXPECT_EQ(m_random.State(), AfterRandoms(6));
}

// 0x50df80..0x50e05d: Helper02 cuelga del hueso 34 a (20, 0, 0) a media key por frame, y después
// 0x4ec110 con (20, 0, 15) en el mismo hueso lleva un destello rojo de 1,5.
TEST_F(GameHangingTest, Helper02HangsFromBone34WithItsRedFlare)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.pet.bone, 0x22);
    EXPECT_EQ(m_actor.pet.speed, 0.5f);
    ASSERT_EQ(m_batch.Vertices().size(), 3u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(170.0f, 150.0f, 44.0f));
    const auto sprites = LiveSprites();
    ASSERT_EQ(sprites.size(), 1u);
    EXPECT_EQ(sprites[0].texture, Flare);
    EXPECT_EQ(sprites[0].position, glm::vec3(170.0f, 150.0f, 59.0f));
    EXPECT_EQ(sprites[0].scale, 1.5f);
    EXPECT_EQ(sprites[0].light, glm::vec3(FirstLum() * 0.5f, 0.0f, 0.0f));
    EXPECT_EQ(m_random.State(), AfterRandoms(5));
}

// 0x50df22: en los mapas 18 a 23 no cuelgan ni las alas ni la mascota.
TEST_F(GameHangingTest, TheEventMapsSkipTheWingsAndThePet)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;
    GameHangingPass pass = Pass(models);
    pass.map = 20;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(LiveSprites().empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
}

// 0x413fe0, 0x4142f0 y 0x414470: con el CtlCode en 0x20, +0x360 pasa a 5 y la capa nueva dibuja MuSign en el
// hueso 20 del cuerpo, corrido (70, −5, 0) y girado 45° en z, sin rand(): el pase base (luz por vértice) y el
// cromo aditivo en (1; 0,5; 0). Su pose queda en el buffer global, y las alas se posan después.
TEST_F(GameHangingTest, AGameMasterCarriesTheSignOnBone20)
{
    const HangingModels models({MuSign});
    m_actor.controlCode = 0x20;

    const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_actor.byte360, 5);
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    // (70, −5, 0) desde el hueso 20 en (0, 0, 20), más el lugar (150, 150, 10).
    const glm::vec3 origin{220.0f, 145.0f, 30.0f};
    ASSERT_TRUE(m_carry.models.contains(MuSign));
    EXPECT_EQ(m_carry.models.at(MuSign).position, origin);
    EXPECT_EQ(m_carry.models.at(MuSign).angles, glm::vec3(0.0f, 0.0f, 45.0f));
    const auto vertices = m_batch.Vertices();
    ASSERT_EQ(vertices.size(), 6u);
    for (const std::size_t first : {std::size_t{0}, std::size_t{3}})
    {
        EXPECT_NEAR(vertices[first].position.x, origin.x, 1e-3f);
        EXPECT_NEAR(vertices[first].position.y, origin.y, 1e-3f);
        EXPECT_NEAR(vertices[first].position.z, origin.z, 1e-3f);
        // El (1, 0, 0) de la malla, girado 45° en z.
        EXPECT_NEAR(vertices[first + 1].position.x, origin.x + 0.70710677f, 1e-3f);
        EXPECT_NEAR(vertices[first + 1].position.y, origin.y + 0.70710677f, 1e-3f);
    }
    EXPECT_NEAR(vertices[3].color.red, 1.0f, 1e-6f);
    EXPECT_NEAR(vertices[3].color.green, 0.5f, 1e-6f);
    EXPECT_NEAR(vertices[3].color.blue, 0.0f, 1e-6f);
}

// La capa queda en el registro: con +0x360 en 5 y sin ser GM se sigue dibujando, y 0x4ec200 la avanza 0,5
// por frame del original (entre frames, como quedó). Sin capa y con el 5 de un GM anterior, la vuelve a crear
// (Q-C301).
TEST_F(GameHangingTest, TheSignStaysInTheRecordAndAdvancesOnlyOnOriginalFrames)
{
    const HangingModels models({MuSign});
    m_actor.byte360 = 5;
    m_actor.layer = Client::Scenes::GameMasterLayer{.animation = {.action = 1}};
    GameHangingPass pass = Pass(models);
    pass.originalFrame = false;

    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.frame, 0.0f);

    pass.originalFrame = true;
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 1);
    EXPECT_EQ(m_actor.layer->animation.frame, 0.5f);

    m_actor.layer.reset();
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_actor.byte360, 5);
}

// Un GM con la capa de otro tipo la suelta (0x414340 vuelve +0x360 a 0) y crea la 5.
TEST_F(GameHangingTest, AGameMasterReplacesALayerOfAnotherKind)
{
    const HangingModels models({MuSign});
    m_actor.controlCode = 8;
    m_actor.byte360 = 2;
    m_actor.layer = Client::Scenes::GameMasterLayer{.animation = {.action = 1, .frame = 0.75f}};

    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, Pass(models)).HasValue());

    EXPECT_EQ(m_actor.byte360, 5);
    ASSERT_TRUE(m_actor.layer.has_value());
    EXPECT_EQ(m_actor.layer->animation.action, 0);
    EXPECT_EQ(m_actor.layer->animation.frame, 0.0f);
}

// Lo que todavía corta, con su dirección y después de lo ya dibujado: el joint de 0x223 (0x50ec26), el
// de 0x224 después de su Shiny02 (0x50ee26), la capa de un GM por el nombre sin MuSign (0x414470), el arma
// del evento de Blood Castle (0x50de84), la capa en Chaos Castle (0x5ffe09), las capas del 6 al 13
// (0x4140d5) y lo que no es un jugador (0x50e060).
TEST_F(GameHangingTest, TheUnportedCasesStopWithTheirAddress)
{
    {
        const HangingModels models({Sword223});
        m_actor.hands[0].model = Sword223;
        const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50ec26"), std::string::npos) << result.Error();
        EXPECT_EQ(m_batch.Vertices().size(), 3u);
    }
    {
        const HangingModels models({Sword224});
        m_actor.hands[0].model = Sword224;
        m_sprites.Clear();
        const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50ee26"), std::string::npos) << result.Error();
        ASSERT_EQ(LiveSprites().size(), 1u);
        EXPECT_EQ(LiveSprites()[0].texture, Shiny);
    }
    {
        const HangingModels models({});
        m_actor.hands[0].model = 0xffff;
        constexpr std::string_view Name = "xwebzenx";
        for (std::size_t index = 0; index < Name.size(); ++index)
            m_actor.name[index] = static_cast<std::byte>(Name[index]);
        const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x414470"), std::string::npos) << result.Error();
        EXPECT_EQ(m_actor.byte360, 5);
        m_actor.name = {};
    }
    {
        const HangingModels models({MuSign});
        GameHangingPass pass = Pass(models);
        pass.map = 11;
        const auto result = Client::Scenes::DrawGameHanging(m_actor, pass);
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50de84"), std::string::npos) << result.Error();
        pass.map = 20;
        const auto chaos = Client::Scenes::DrawGameHanging(m_actor, pass);
        ASSERT_FALSE(chaos.HasValue());
        EXPECT_NE(chaos.Error().find("0x5ffe09"), std::string::npos) << chaos.Error();
        m_actor.layer.reset();
        m_actor.byte360 = 6;
        const auto other = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(other.HasValue());
        EXPECT_NE(other.Error().find("0x4140d5"), std::string::npos) << other.Error();
        m_actor.byte360 = 0;
    }
    {
        const HangingModels models({});
        m_actor.type = 0x20b;
        m_random = Client::Render::Models::CrtRandom{1};
        const auto result = Client::Scenes::DrawGameHanging(m_actor, Pass(models));
        ASSERT_FALSE(result.HasValue());
        EXPECT_NE(result.Error().find("0x50e060"), std::string::npos) << result.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(1));
    }
}

// Entre frames del original (0x509340 con la animación como quedó), lo que cuelga no avanza.
TEST_F(GameHangingTest, BetweenOriginalFramesTheHangingAnimationDoesNotAdvance)
{
    const HangingModels models({Helper02});
    m_actor.pet.model = Helper02;
    m_actor.pet.animation = {.action = 1, .frame = 0.25f, .previousAction = 0, .previousKey = 0};
    GameHangingPass pass = Pass(models);
    pass.originalFrame = false;

    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.pet.animation.frame, 0.25f);

    pass.originalFrame = true;
    ASSERT_TRUE(Client::Scenes::DrawGameHanging(m_actor, pass).HasValue());
    EXPECT_EQ(m_actor.pet.animation.frame, 0.75f);
}
} // namespace
