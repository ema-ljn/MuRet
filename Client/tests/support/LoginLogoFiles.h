#pragma once

#include "Data/Models/Model.h"
#include "Data/Textures/TextureSet.h"
#include "FrontEnd/LoginModels.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>

// A synthetic Logo03.bmd in a test Data\, like the lab's: two one-triangle meshes, logo.jpg and logo2.jpg (texture
// 1's, the one of the 0xa2's blendMesh), one bone and an action of one key, with its two textures.
namespace Client::Tests
{
// On bone 0; `texture` is its index.
inline Data::Models::Mesh TriangleMesh(std::int16_t texture, std::string name)
{
    Data::Models::Mesh mesh;
    mesh.texture = texture;
    mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
    mesh.normals = {{0, {0, 0, 1}, 0}};
    mesh.texCoords = {{0, 0}};
    mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
    mesh.textureName = std::move(name);
    return mesh;
}

// Logo\Logo03.bmd in `root`, and its two textures, except the second with `withoutSecondTexture`.
inline void WriteLoginLogo(const std::filesystem::path& root, bool withoutSecondTexture = false)
{
    Data::Models::Model logo;
    logo.version = 0x0a;
    logo.name = "logo";
    logo.meshes = {TriangleMesh(0, "logo.jpg"), TriangleMesh(1, "logo2.jpg")};
    Data::Models::Action action;
    action.keyCount = 1;
    logo.actions = {action};
    Data::Models::Bone bone;
    bone.name = "root";
    bone.actions = {Data::Models::BoneKeys{{{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 0.0f}}}};
    logo.bones = {bone};
    WriteSceneFile(root, "Logo/Logo03.bmd", PlainBmd(BmdContent(logo), logo.version));
    const std::array<Data::Textures::TextureSetEntry, 2> textures{{
        {"Logo\\logo.jpg", 0x455, FrontEnd::LoginLogoSampling},
        {"Logo\\logo2.jpg", 0x456, FrontEnd::LoginLogoSampling},
    }};
    WriteTextureSet(root, std::span(textures).first(withoutSecondTexture ? 1 : 2));
}
} // namespace Client::Tests
