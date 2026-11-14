// Minimal 4x4 matrix math. Column-major, column vectors (OpenGL layout).
// World data stays in the game's left-handed space; only the view mirrors z.
#pragma once
#include <cmath>

namespace obf {

struct Mat4 {
    float m[16] = {};
    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 4; ++k) r.m[c * 4 + i] += m[k * 4 + i] * b.m[c * 4 + k];
        return r;
    }
};

inline Mat4 translate(float x, float y, float z) {
    Mat4 r = Mat4::identity();
    r.m[12] = x, r.m[13] = y, r.m[14] = z;
    return r;
}

inline Mat4 scale(float x, float y, float z) {
    Mat4 r = Mat4::identity();
    r.m[0] = x, r.m[5] = y, r.m[10] = z;
    return r;
}

// Refractor "yaw/pitch/roll" in degrees, Direct3D convention: roll (z), then pitch (x), then yaw (y).
inline Mat4 rotation_ypr(float yaw_deg, float pitch_deg, float roll_deg) {
    const float k = 3.14159265f / 180.0f;
    float cy = std::cos(yaw_deg * k), sy = std::sin(yaw_deg * k);
    float cp = std::cos(pitch_deg * k), sp = std::sin(pitch_deg * k);
    float cr = std::cos(roll_deg * k), sr = std::sin(roll_deg * k);
    Mat4 ry = Mat4::identity(), rx = Mat4::identity(), rz = Mat4::identity();
    // Left-handed rotations (D3DXMatrixRotation*), transposed to column vectors.
    ry.m[0] = cy, ry.m[8] = sy, ry.m[2] = -sy, ry.m[10] = cy;
    rx.m[5] = cp, rx.m[9] = -sp, rx.m[6] = sp, rx.m[10] = cp;
    rz.m[0] = cr, rz.m[4] = -sr, rz.m[1] = sr, rz.m[5] = cr;
    return ry * rx * rz;
}

inline Mat4 perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 r;
    float f = 1.0f / std::tan(fovy / 2);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}

// Camera in game coordinates; yaw 0 looks along +z, pitch > 0 looks up.
inline Mat4 view(float x, float y, float z, float yaw, float pitch) {
    float fx = std::sin(yaw) * std::cos(pitch), fy = std::sin(pitch), fz = std::cos(yaw) * std::cos(pitch);
    // Game data is left-handed (Direct3D); mirror z into OpenGL's right-handed space.
    fz = -fz;
    z = -z;
    float rx = -fz, rz = fx;  // right = forward x up
    float rl = std::sqrt(rx * rx + rz * rz);
    rx /= rl;
    rz /= rl;
    float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;  // up = right x forward
    Mat4 v = Mat4::identity();
    v.m[0] = rx, v.m[4] = 0, v.m[8] = rz;
    v.m[1] = ux, v.m[5] = uy, v.m[9] = uz;
    v.m[2] = -fx, v.m[6] = -fy, v.m[10] = -fz;
    v.m[12] = -(rx * x + rz * z);
    v.m[13] = -(ux * x + uy * y + uz * z);
    v.m[14] = fx * x + fy * y + fz * z;
    return v * scale(1, 1, -1);
}

}  // namespace obf
