#include "Core/Expected.h"
#include "Render/Pass3D.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <initializer_list>
#include <map>
#include <ostream>
#include <sstream>
#include <string>

namespace
{
using Fields = std::map<std::string, std::string>;
template <typename T> using Result = Client::Core::Expected<T, const char*>;
using Client::Core::Unexpected;

constexpr const char* CaptureSignature = "match camera=00635ae0 inverse=005fcc30";
constexpr std::uint32_t LorenciaMap = 0;
constexpr std::uint32_t GameScene = 5;
constexpr std::uint32_t LowByteMask = 0xff;

struct CapturedCamera
{
    Client::World::GameCamera state;
    Client::World::GameCameraInput input;
    glm::vec3 angles;
    glm::vec3 position;
};

struct ScalarField
{
    const char* key;
    float& value;
};

void RemoveCarriageReturn(std::string& line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}

Result<Fields> ReadFields(const char* path)
{
    std::ifstream capture(path);
    if (!capture)
        return Unexpected("no se pudo abrir la captura");
    std::string line;
    if (!std::getline(capture, line))
        return Unexpected("captura vacía");
    RemoveCarriageReturn(line);
    if (line != CaptureSignature)
        return Unexpected("la captura no confirmó ambas firmas");
    Fields fields;
    while (std::getline(capture, line))
    {
        RemoveCarriageReturn(line);
        const auto separator = line.find('=');
        if (separator == std::string::npos ||
            !fields.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
            return Unexpected("campo inválido o duplicado");
    }
    if (!capture.eof())
        return Unexpected("lectura incompleta de la captura");
    return fields;
}

Result<std::uint32_t> ParseWord(const std::string& text)
{
    // Conserva los prefijos, signos, espacios y rango unsigned long que aceptaba stoul.
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtoul(text.c_str(), &end, 16);
    if (end == text.c_str() || errno == ERANGE)
        return Unexpected("stoul");
    if (end != text.c_str() + text.size() || value > UINT32_MAX)
        return Unexpected("bits inválidos");
    return static_cast<std::uint32_t>(value);
}

Result<std::uint32_t> ReadWord(const Fields& fields, const char* key)
{
    const auto found = fields.find(key);
    if (found == fields.end())
        return Unexpected("map::at");
    return ParseWord(found->second);
}

Result<void> ReadScalars(const Fields& fields, std::initializer_list<ScalarField> scalars)
{
    for (const auto& scalar : scalars)
    {
        const auto word = ReadWord(fields, scalar.key);
        if (!word.HasValue())
            return Unexpected(word.Error());
        scalar.value = std::bit_cast<float>(word.Value());
    }
    return {};
}

Result<glm::vec3> ReadVector(const Fields& fields, const char* key)
{
    const auto found = fields.find(key);
    if (found == fields.end())
        return Unexpected("map::at");
    std::istringstream components(found->second);
    glm::vec3 result{};
    for (int k = 0; k < 3; ++k)
    {
        std::string component;
        if (!std::getline(components, component, ','))
            return Unexpected("vector incompleto");
        const auto word = ParseWord(component);
        if (!word.HasValue())
            return Unexpected(word.Error());
        result[k] = std::bit_cast<float>(word.Value());
    }
    std::string extra;
    if (std::getline(components, extra, ','))
        return Unexpected("vector con componentes de más");
    return result;
}

Result<Client::World::GameCamera> ReadState(const Fields& fields, float yaw)
{
    Client::World::GameCamera state;
    state.yaw = yaw;
    const auto distanceAndZoom = ReadScalars(fields, {{"distance", state.distance}, {"zoom", state.zoom}});
    if (!distanceAndZoom.HasValue())
        return Unexpected(distanceAndZoom.Error());
    const auto level = ReadWord(fields, "level");
    if (!level.HasValue())
        return Unexpected(level.Error());
    if (level.Value() > UINT16_MAX)
        return Unexpected("nivel fuera de WORD");
    state.level = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(level.Value()));
    const auto remaining = ReadScalars(
        fields,
        {{"fixedHeight", state.fixedHeight}, {"farPlane", state.farPlane}, {"targetDistance", state.targetDistance}});
    if (!remaining.HasValue())
        return Unexpected(remaining.Error());
    return state;
}

Result<std::uint8_t> ReadCellAttribute(const Fields& fields)
{
    const char* key = fields.contains("visualCellAttribute") ? "visualCellAttribute" : "cellAttribute";
    if (!fields.contains(key))
        return std::uint8_t{0};
    const auto wall = ReadWord(fields, key);
    if (!wall.HasValue())
        return Unexpected(wall.Error());
    if (wall.Value() > UINT16_MAX)
        return Unexpected("atributo fuera de WORD");
    return static_cast<std::uint8_t>(wall.Value() & LowByteMask);
}

Result<void> ValidateLocation(const Fields& fields)
{
    const auto map = ReadWord(fields, "map");
    if (!map.HasValue())
        return Unexpected(map.Error());
    if (map.Value() != LorenciaMap)
        return Unexpected("no es una captura de Lorencia en juego");
    const auto scene = ReadWord(fields, "scene");
    if (!scene.HasValue())
        return Unexpected(scene.Error());
    if (scene.Value() != GameScene)
        return Unexpected("no es una captura de Lorencia en juego");
    return {};
}

Result<Client::World::GameCameraInput> ReadInput(const Fields& fields)
{
    Client::World::GameCameraInput input;
    const auto hero = ReadVector(fields, "heroPosition");
    if (!hero.HasValue())
        return Unexpected(hero.Error());
    input.hero = hero.Value();
    const auto scalars = ReadScalars(fields, {{"shake", input.shake}, {"worldTime", input.worldTime}});
    if (!scalars.HasValue())
        return Unexpected(scalars.Error());
    const auto effectTarget = ReadVector(fields, "effectTarget");
    if (!effectTarget.HasValue())
        return Unexpected(effectTarget.Error());
    input.effectTarget = effectTarget.Value();
    const auto location = ValidateLocation(fields);
    if (!location.HasValue())
        return Unexpected(location.Error());
    input.map = LorenciaMap;
    input.scene = Client::World::CullingScene::Game;
    const auto wall = ReadCellAttribute(fields);
    if (!wall.HasValue())
        return Unexpected(wall.Error());
    input.cellAttribute = wall.Value();
    return input;
}

Result<CapturedCamera> ReadCapturedCamera(const Fields& fields)
{
    const auto angles = ReadVector(fields, "angles");
    if (!angles.HasValue())
        return Unexpected(angles.Error());
    const auto state = ReadState(fields, angles.Value().z);
    if (!state.HasValue())
        return Unexpected(state.Error());
    const auto input = ReadInput(fields);
    if (!input.HasValue())
        return Unexpected(input.Error());
    const auto position = ReadVector(fields, "position");
    if (!position.HasValue())
        return Unexpected(position.Error());
    return CapturedCamera{state.Value(), input.Value(), angles.Value(), position.Value()};
}

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

class CheckWriter
{
public:
    explicit CheckWriter(std::ostream& output) : m_output(output) {}

    void Compare(const std::string& name, std::uint32_t actual, std::uint32_t observed)
    {
        const bool same = actual == observed;
        m_matches = m_matches && same;
        if (!m_first)
            m_output << ",\n";
        m_first = false;
        m_output << "    \"" << name << "\": {\"observed\": \"" << std::hex << std::setfill('0') << std::setw(8)
                 << observed << "\", \"computed\": \"" << std::setw(8) << actual
                 << "\", \"matches\": " << (same ? "true" : "false") << "}";
    }

    [[nodiscard]] bool Matches() const
    {
        return m_matches;
    }

private:
    std::ostream& m_output;
    bool m_matches = true;
    bool m_first = true;
};

Result<void> WriteCameraChecks(CheckWriter& writer, const CapturedCamera& observed,
                               const Client::Render::Camera3D& computed, const Fields& fields)
{
    for (int k = 0; k < 3; ++k)
    {
        writer.Compare("camera.position." + std::to_string(k), Bits(computed.position[k]), Bits(observed.position[k]));
        writer.Compare("camera.angles." + std::to_string(k), Bits(computed.angles[k]), Bits(observed.angles[k]));
    }
    const auto fieldOfView = ReadWord(fields, "fieldOfView");
    if (!fieldOfView.HasValue())
        return Unexpected(fieldOfView.Error());
    writer.Compare("camera.fov", Bits(computed.fieldOfView), fieldOfView.Value());
    const auto nearPlane = ReadWord(fields, "nearPlane");
    if (!nearPlane.HasValue())
        return Unexpected(nearPlane.Error());
    writer.Compare("camera.near", Bits(computed.nearPlane), nearPlane.Value());
    writer.Compare("camera.far", Bits(computed.farPlane), Bits(observed.state.farPlane));
    return {};
}

void WriteStateChecks(CheckWriter& writer, const Client::World::GameCamera& observed,
                      const Client::World::GameCamera& computed)
{
    writer.Compare("state.yaw", Bits(computed.yaw), Bits(observed.yaw));
    writer.Compare("state.distance", Bits(computed.distance), Bits(observed.distance));
    writer.Compare("state.zoom", Bits(computed.zoom), Bits(observed.zoom));
    writer.Compare("state.level", static_cast<std::uint16_t>(computed.level),
                   static_cast<std::uint16_t>(observed.level));
    writer.Compare("state.fixedHeight", Bits(computed.fixedHeight), Bits(observed.fixedHeight));
    writer.Compare("state.farPlane", Bits(computed.farPlane), Bits(observed.farPlane));
    writer.Compare("state.targetDistance", Bits(computed.targetDistance), Bits(observed.targetDistance));
}

void WriteLimits(std::ostream& output, bool matches)
{
    output << "\n  },\n  \"matches\": " << (matches ? "true" : "false") << ",\n"
           << "  \"limits\": [\n"
           << "    \"Captura pasiva secuencial, no atomica; reposo indicado por operador, sin serie temporal.\",\n"
           << "    \"Nueve salidas camara; siete checks de estado reutilizan entradas observadas y no son "
              "predicciones independientes.\",\n"
           << "    \"Sin atributo observado se supone 0x40/0x80 apagados. Si existe, se usa visualCellAttribute o "
              "cellAttribute; celda visual/logica puede diferir.\",\n"
           << "    \"Lorencia mapa 0: no prueba el balanceo FSIN del mapa 5.\"\n"
           << "  ]\n}\n";
}

Result<bool> WriteComparison(const char* path, const Fields& fields, const CapturedCamera& observed,
                             const Client::Render::Camera3D& camera, const Client::World::GameCamera& state)
{
    std::ofstream output(path);
    if (!output)
        return Unexpected("no se pudo abrir el JSON");
    output << "{\n  \"implementation\": \"libClientLib.a: StepGameCamera\",\n  \"checks\": {\n";
    CheckWriter writer(output);
    const auto cameraChecks = WriteCameraChecks(writer, observed, camera, fields);
    if (!cameraChecks.HasValue())
        return Unexpected(cameraChecks.Error());
    WriteStateChecks(writer, observed.state, state);
    WriteLimits(output, writer.Matches());
    output.flush();
    if (!output)
        return Unexpected("fallo al escribir el JSON");
    return writer.Matches();
}

// Verificación offline: usa la implementación enlazada, no repite su cuenta.
Result<bool> CompareCapture(const char* capturePath, const char* outputPath)
{
    const auto fields = ReadFields(capturePath);
    if (!fields.HasValue())
        return Unexpected(fields.Error());
    const auto observed = ReadCapturedCamera(fields.Value());
    if (!observed.HasValue())
        return Unexpected(observed.Error());
    auto state = observed.Value().state;
    const auto camera = Client::World::StepGameCamera(state, observed.Value().input);
    return WriteComparison(outputPath, fields.Value(), observed.Value(), camera, state);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "error: uso: compare_lorencia CAPTURA JSON\n";
        return 1;
    }
    const auto result = CompareCapture(argv[1], argv[2]);
    if (!result.HasValue())
    {
        std::cerr << "error: " << result.Error() << '\n';
        return 1;
    }
    std::cout << "matches=" << (result.Value() ? "true" : "false") << " cameraChecks=9 seededStateChecks=7\n";
    return result.Value() ? 0 : 1;
}
