// Static meshes (.sm v9/v10) and .rs shader files. Spec: docs/formats/sm.md; reference: tools/sm.py
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace obf {

struct SmMaterial {
    std::string name;
    uint32_t prim = 4, stride = 32, alpha = 0;
    std::vector<float> vertices;  // stride / 4 floats each: pos, normal, uv[, lightmap uv]
    std::vector<uint16_t> indices;
    size_t vertex_count() const { return vertices.size() / (stride / 4); }
};

struct CollisionMesh {
    std::vector<float> vertices;    // xyz
    std::vector<uint16_t> faces;    // a, b, c per triangle
    std::vector<uint16_t> material; // per face
};

struct SmMesh {
    uint32_t version = 0;
    float bbox[6] = {};
    std::vector<CollisionMesh> collision;
    std::vector<std::vector<SmMaterial>> lods;
};

// Collision block (also used inside .tm); the spatial tree after the faces is ignored.
CollisionMesh collision_parse(const uint8_t* data, size_t size);

SmMesh sm_parse(const std::vector<uint8_t>& data);  // throws std::runtime_error
std::vector<uint32_t> sm_triangles(const SmMaterial& m);  // lists and strips -> triangle list

// .rs: material name (lower) -> key -> value ("shader" holds the subshader type).
std::map<std::string, std::map<std::string, std::string>> rs_parse(const std::string& text);

}  // namespace obf
