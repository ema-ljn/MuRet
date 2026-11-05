#include "World/WorldObjects.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Scenes/WorldModels.h"
#include "World/WorldTerrain.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"
#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Client::World::CreateWorldObjects, StepWorldObject y DrawOf con el EncTerrain56.obj del laboratorio,
// contra lo que hizo el código real del main.exe con cada uno de sus 276 registros
// (tests/vectors/world55, 06 § El mundo del login): bit a bit.
namespace
{
using Client::Tests::WorldObjectVector;
using Client::Tests::WorldObjectVectorCall;
using Client::Tests::WorldObjectVectors;
using Client::Tests::WorldObjectVectorState;
using Client::World::ObjectDraw;
using Client::World::ObjectDrawKind;
using Client::World::WorldObject;

constexpr std::uint8_t Login = 55;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// Cada campo del objeto contra el del binario, por sus bits.
void ExpectSameState(const WorldObject& object, const WorldObjectVectorState& expected, const std::string& where)
{
    EXPECT_EQ(Bits(object.transform.scale), Bits(expected.scale)) << where << ": la escala";
    for (int k = 0; k < 3; ++k)
    {
        EXPECT_EQ(Bits(object.transform.position[k]), Bits(expected.position[k])) << where << ": la posición " << k;
        EXPECT_EQ(Bits(object.transform.angles[k]), Bits(expected.angles[k])) << where << ": el ángulo " << k;
        EXPECT_EQ(Bits(object.light[k]), Bits(expected.light[k])) << where << ": la luz " << k;
    }
    EXPECT_EQ(object.state.hiddenMesh, expected.hiddenMesh) << where << ": la malla oculta";
    EXPECT_EQ(object.state.blendMesh, expected.blendMesh) << where << ": el blendMesh";
    EXPECT_EQ(Bits(object.state.light), Bits(expected.blendMeshLight)) << where << ": la luz del blendMesh";
    EXPECT_EQ(Bits(object.state.uv.x), Bits(expected.uv.x)) << where << ": la U";
    EXPECT_EQ(Bits(object.state.uv.y), Bits(expected.uv.y)) << where << ": la V";
    EXPECT_EQ(Bits(object.speed), Bits(expected.speed)) << where << ": la velocidad";
    EXPECT_EQ(object.vertexLight, expected.vertexLight) << where << ": la luz por vértice";
    EXPECT_EQ(Bits(object.alpha), Bits(expected.alpha)) << where << ": el alpha";
    EXPECT_EQ(Bits(object.targetAlpha), Bits(expected.targetAlpha)) << where << ": la meta del alpha";
    EXPECT_EQ(Bits(object.animation.frame), Bits(expected.frame)) << where << ": el frame";
    EXPECT_EQ(object.animation.action, expected.action) << where << ": la acción";
}

class GoldenWorldObjects : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        const auto error =
            Client::Tests::ReadWorldObjectVectors(Client::Tests::World55VectorsDir() / "objects.txt", m_vectors);
        ASSERT_FALSE(error.has_value()) << *error;
        auto records = Client::World::LoadWorldObjects(m_dataRoot, Login);
        ASSERT_TRUE(records.HasValue()) << records.Error();
        m_records = std::move(records).Value();
        Client::Core::Log log(m_sink, Client::Core::LogLevel::Debug);
        const auto loaded = m_models.Load(m_dataRoot, Login, m_store, log, nullptr);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    }

    // Los objetos como los crea MuRet en el login, y los tres pasos del binario sobre cada uno.
    std::vector<WorldObject> Created() const
    {
        return Client::World::CreateWorldObjects(m_records, Login, Client::World::ObjectScene::Login);
    }

    void StepOnce(WorldObject& object) const
    {
        object.seen = true;
        Client::World::StepWorldObject(object, Login, m_models.SkeletonOf(static_cast<std::uint16_t>(object.type)));
    }

    WorldObjectVectors m_vectors;
    std::vector<Client::Data::Terrain::TerrainObject> m_records;
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Scenes::WorldModels m_models;
};

// 0x606660 con cada registro, en la escena 2: lo de siempre, el farol a 0,8 con luz 0,2 y los
// remolinos 86 y 90 en su lugar del cielo.
TEST_F(GoldenWorldObjects, EachTypeIsCreatedAsTheBinary)
{
    const std::vector<WorldObject> objects = Created();
    ASSERT_EQ(objects.size(), m_vectors.objects.size());
    for (std::size_t k = 0; k < objects.size(); ++k)
    {
        const WorldObjectVector& expected = m_vectors.objects[k];
        EXPECT_EQ(objects[k].type, expected.type) << k;
        EXPECT_FALSE(objects[k].seen) << k;
        ExpectSameState(objects[k], expected.created, "objeto " + std::to_string(k) + " al crearse");
    }
}

// Tres pasos de 0x604ab0 sobre cada uno, con +0x16c prendido: el alpha, la animación a 0,16 y los
// casos del mapa 0x37.
TEST_F(GoldenWorldObjects, ThreeStepsOfEachTypeMatchTheBinary)
{
    std::vector<WorldObject> objects = Created();
    ASSERT_EQ(objects.size(), m_vectors.objects.size());
    for (std::size_t k = 0; k < objects.size(); ++k)
    {
        for (std::size_t step = 0; step < 3; ++step)
        {
            StepOnce(objects[k]);
            ExpectSameState(objects[k], m_vectors.objects[k].steps[step],
                            "objeto " + std::to_string(k) + " (tipo " + std::to_string(objects[k].type) +
                                ") en el paso " + std::to_string(step + 1));
        }
    }
}

// Y lo que 0x602e90 le pasa a RenderBody o a RenderMesh después de los tres pasos, con la luz del
// terreno del generador: los marcadores no se dibujan, el remolino 90 va por su malla 0.
TEST_F(GoldenWorldObjects, EachTypeDrawsAsTheBinary)
{
    std::vector<WorldObject> objects = Created();
    ASSERT_EQ(objects.size(), m_vectors.objects.size());
    std::size_t bodies = 0;
    std::size_t meshes = 0;
    for (std::size_t k = 0; k < objects.size(); ++k)
    {
        for (int step = 0; step < 3; ++step)
        {
            StepOnce(objects[k]);
        }
        const WorldObjectVector& expected = m_vectors.objects[k];
        const ObjectDraw draw = Client::World::DrawOf(objects[k], m_vectors.terrainLight, Login);
        const std::string where = "objeto " + std::to_string(k) + " (tipo " + std::to_string(objects[k].type) + ")";
        if (expected.calls.empty())
        {
            EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden) << where;
            continue;
        }
        ASSERT_EQ(expected.calls.size(), 1u) << where;
        const WorldObjectVectorCall& call = expected.calls.front();
        if (call.name == "RenderBody")
        {
            ++bodies;
            ASSERT_EQ(draw.kind, ObjectDrawKind::Body) << where;
            EXPECT_EQ(call.first, 2) << where;
            EXPECT_EQ(draw.state.hiddenMesh, call.hiddenMesh) << where;
            EXPECT_EQ(draw.vertexLight, call.vertexLight) << where;
        }
        else
        {
            ++meshes;
            ASSERT_EQ(draw.kind, ObjectDrawKind::SubtractiveMesh) << where;
            EXPECT_EQ(call.first, 0) << where << ": la malla 0";
        }
        EXPECT_EQ(Bits(draw.alpha), Bits(call.alpha)) << where << ": el alpha";
        EXPECT_EQ(draw.state.blendMesh, call.blendMesh) << where << ": el blendMesh";
        EXPECT_EQ(Bits(draw.state.light), Bits(call.blendMeshLight)) << where << ": la luz del blendMesh";
        EXPECT_EQ(Bits(draw.state.uv.x), Bits(call.uv.x)) << where << ": la U";
        EXPECT_EQ(Bits(draw.state.uv.y), Bits(call.uv.y)) << where << ": la V";
        for (int c = 0; c < 3; ++c)
        {
            EXPECT_EQ(Bits(draw.color[c]), Bits(call.color[c])) << where << ": el color " << c;
        }
    }
    // Los 276 menos los 3 marcadores (dos 84 y el 89): 272 cuerpos y el remolino 90.
    EXPECT_EQ(bodies, 272u);
    EXPECT_EQ(meshes, 1u);
}
} // namespace
