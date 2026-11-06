#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

// Verificación offline: usa la implementación enlazada, no repite su cuenta.
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("uso: compare_lorencia CAPTURA JSON");
        std::ifstream capture(argv[1]);
        if (!capture)
            throw std::runtime_error("no se pudo abrir la captura");
        std::map<std::string, std::string> fields;
        std::string line;
        if (!std::getline(capture, line))
            throw std::runtime_error("captura vacía");
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line != "match camera=00635ae0 inverse=005fcc30")
            throw std::runtime_error("la captura no confirmó ambas firmas");
        while (std::getline(capture, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto separator = line.find('=');
            if (separator == std::string::npos ||
                !fields.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
                throw std::runtime_error("campo inválido o duplicado");
        }
        if (!capture.eof())
            throw std::runtime_error("lectura incompleta de la captura");
        const auto word = [](const std::string& text)
        {
            std::size_t consumed = 0;
            const auto value = std::stoul(text, &consumed, 16);
            if (consumed != text.size() || value > UINT32_MAX)
                throw std::runtime_error("bits inválidos");
            return static_cast<std::uint32_t>(value);
        };
        const auto scalar = [&](const std::string& key) { return std::bit_cast<float>(word(fields.at(key))); };
        const auto vector = [&](const std::string& key)
        {
            std::istringstream components(fields.at(key));
            glm::vec3 result{};
            for (int k = 0; k < 3; ++k)
            {
                std::string component;
                if (!std::getline(components, component, ','))
                    throw std::runtime_error("vector incompleto");
                result[k] = std::bit_cast<float>(word(component));
            }
            std::string extra;
            if (std::getline(components, extra, ','))
                throw std::runtime_error("vector con componentes de más");
            return result;
        };
        Client::World::GameCamera state;
        const glm::vec3 observedAngles = vector("angles");
        state.yaw = observedAngles.z;
        state.distance = scalar("distance");
        state.zoom = scalar("zoom");
        const auto level = word(fields.at("level"));
        if (level > UINT16_MAX)
            throw std::runtime_error("nivel fuera de WORD");
        state.level = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(level));
        state.fixedHeight = scalar("fixedHeight");
        state.farPlane = scalar("farPlane");
        state.targetDistance = scalar("targetDistance");
        Client::World::GameCameraInput input;
        input.hero = vector("heroPosition");
        input.shake = scalar("shake");
        input.worldTime = scalar("worldTime");
        input.effectTarget = vector("effectTarget");
        if (word(fields.at("map")) != 0 || word(fields.at("scene")) != 5)
            throw std::runtime_error("no es una captura de Lorencia en juego");
        input.map = 0;
        input.scene = Client::World::CullingScene::Game;
        const bool hasWall = fields.contains("visualCellAttribute") || fields.contains("cellAttribute");
        const std::string wallKey = fields.contains("visualCellAttribute") ? "visualCellAttribute" : "cellAttribute";
        const auto wall = hasWall ? word(fields.at(wallKey)) : 0u;
        if (wall > UINT16_MAX)
            throw std::runtime_error("atributo fuera de WORD");
        input.cellAttribute = static_cast<std::uint8_t>(wall & 0xffu);
        const auto computed = Client::World::StepGameCamera(state, input);
        const glm::vec3 observedPosition = vector("position");
        std::ofstream output(argv[2]);
        if (!output)
            throw std::runtime_error("no se pudo abrir el JSON");
        bool matches = true;
        bool first = true;
        output << "{\n  \"implementation\": \"libClientLib.a: StepGameCamera\",\n  \"checks\": {\n";
        const auto compare = [&](const std::string& name, std::uint32_t actual, std::uint32_t observed)
        {
            const bool same = actual == observed;
            matches = matches && same;
            if (!first)
                output << ",\n";
            first = false;
            output << "    \"" << name << "\": {\"observed\": \"" << std::hex << std::setfill('0') << std::setw(8)
                   << observed << "\", \"computed\": \"" << std::setw(8) << actual
                   << "\", \"matches\": " << (same ? "true" : "false") << "}";
        };
        for (int k = 0; k < 3; ++k)
        {
            compare("camera.position." + std::to_string(k), std::bit_cast<std::uint32_t>(computed.position[k]),
                    std::bit_cast<std::uint32_t>(observedPosition[k]));
            compare("camera.angles." + std::to_string(k), std::bit_cast<std::uint32_t>(computed.angles[k]),
                    std::bit_cast<std::uint32_t>(observedAngles[k]));
        }
        compare("camera.fov", std::bit_cast<std::uint32_t>(computed.fieldOfView), word(fields.at("fieldOfView")));
        compare("camera.near", std::bit_cast<std::uint32_t>(computed.nearPlane), word(fields.at("nearPlane")));
        compare("camera.far", std::bit_cast<std::uint32_t>(computed.farPlane), word(fields.at("farPlane")));
        compare("state.yaw", std::bit_cast<std::uint32_t>(state.yaw), std::bit_cast<std::uint32_t>(observedAngles.z));
        compare("state.distance", std::bit_cast<std::uint32_t>(state.distance), word(fields.at("distance")));
        compare("state.zoom", std::bit_cast<std::uint32_t>(state.zoom), word(fields.at("zoom")));
        compare("state.level", static_cast<std::uint16_t>(state.level), level);
        compare("state.fixedHeight", std::bit_cast<std::uint32_t>(state.fixedHeight), word(fields.at("fixedHeight")));
        compare("state.farPlane", std::bit_cast<std::uint32_t>(state.farPlane), word(fields.at("farPlane")));
        compare("state.targetDistance", std::bit_cast<std::uint32_t>(state.targetDistance),
                word(fields.at("targetDistance")));
        output << "\n  },\n  \"matches\": " << (matches ? "true" : "false") << ",\n"
               << "  \"limits\": [\n"
               << "    \"Captura pasiva secuencial, no atomica; reposo indicado por operador, sin serie temporal.\",\n"
               << "    \"Nueve salidas camara; siete checks de estado reutilizan entradas observadas y no son "
                  "predicciones independientes.\",\n"
               << "    \"Sin atributo observado se supone 0x40/0x80 apagados. Si existe, se usa visualCellAttribute o "
                  "cellAttribute; celda visual/logica puede diferir.\",\n"
               << "    \"Lorencia mapa 0: no prueba el balanceo FSIN del mapa 5.\"\n"
               << "  ]\n}\n";
        output.flush();
        if (!output)
            throw std::runtime_error("fallo al escribir el JSON");
        std::cout << "matches=" << (matches ? "true" : "false") << " cameraChecks=9 seededStateChecks=7\n";
        return matches ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
