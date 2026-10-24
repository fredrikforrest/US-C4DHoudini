// GeoMath: host-neutral geometry helpers shared by the Cinema 4D plugin, the
// probe tool and the unit tests. No HAPI calls in here.
//
// Coordinate systems:
//   Houdini  : Y-up, right-handed, 1 unit = 1 metre, polygons wound clockwise
//              when seen from the side the normal points to.
//   Cinema 4D: Y-up, LEFT-handed, 1 unit = 1 centimetre by default, polygons
//              also clockwise seen from the front (CalcFaceNormal = cross(b-a, c-a)).
// Going from one to the other is a reflection (we negate Z, which is what
// Cinema's own FBX/Alembic importers do so caches line up with live output)
// plus a uniform scale. The reflection flips the orientation of every
// polygon, but Houdini's clockwise rule and Cinema 4D's normal formula are
// already opposite conventions, so the two cancel: vertex order is kept as
// delivered. (Houdini tri (0,0,0)(1,0,0)(0,0,1) faces +Y; after negating Z,
// CalcFaceNormal gives +Y too. tests/test_geomath.cpp checks this.) The
// Unity plugin, also Y-up left-handed, likewise reflects without reversing;
// Maya (right-handed) must reverse instead. reverseWinding stays available as
// an option for HDAs authored with flipped normals.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace hapi {

struct Transform
{
    float position[3] = {0, 0, 0};
    float quaternion[4] = {0, 0, 0, 1};   // x, y, z, w (HAPI order)
    float scale[3] = {1, 1, 1};
    float shear[3] = {0, 0, 0};
};

// 4x4, row-major, ROW-vector convention: p' = p * M, translation in m[3][0..2].
// This is both Houdini's and Cinema 4D's convention (C4D Matrix rows = v1,v2,v3,off).
struct Matrix4
{
    double m[4][4];

    static Matrix4 identity()
    {
        Matrix4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) r.m[i][j] = (i == j) ? 1.0 : 0.0;
        return r;
    }

    Matrix4 operator*(const Matrix4& o) const
    {
        Matrix4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }

    void transformPoint(const double in[3], double out[3]) const
    {
        for (int j = 0; j < 3; ++j)
            out[j] = in[0] * m[0][j] + in[1] * m[1][j] + in[2] * m[2][j] + m[3][j];
    }
};

namespace geo {

struct ConvertOptions
{
    double scale = 100.0;        // metres -> centimetres
    bool flipZ = true;           // right-handed -> left-handed
    bool reverseWinding = false; // normally not needed, see header comment
};

// HAPI_Transform (SRT order, unit quaternion) -> matrix, still in Houdini space.
inline Matrix4 toMatrix(const Transform& t)
{
    const double x = t.quaternion[0], y = t.quaternion[1], z = t.quaternion[2], w = t.quaternion[3];
    const double xx = x * x, yy = y * y, zz = z * z;
    const double xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;

    // Rotation for row vectors: rows are the rotated basis vectors.
    Matrix4 r = Matrix4::identity();
    r.m[0][0] = 1 - 2 * (yy + zz); r.m[0][1] = 2 * (xy + wz);     r.m[0][2] = 2 * (xz - wy);
    r.m[1][0] = 2 * (xy - wz);     r.m[1][1] = 1 - 2 * (xx + zz); r.m[1][2] = 2 * (yz + wx);
    r.m[2][0] = 2 * (xz + wy);     r.m[2][1] = 2 * (yz - wx);     r.m[2][2] = 1 - 2 * (xx + yy);

    Matrix4 s = Matrix4::identity();
    s.m[0][0] = t.scale[0]; s.m[1][1] = t.scale[1]; s.m[2][2] = t.scale[2];
    // Shear is rarely produced by HDAs; HAPI's shear is (xy, xz, yz).
    s.m[1][0] = t.shear[0]; s.m[2][0] = t.shear[1]; s.m[2][1] = t.shear[2];

    Matrix4 tr = Matrix4::identity();
    tr.m[3][0] = t.position[0]; tr.m[3][1] = t.position[1]; tr.m[3][2] = t.position[2];

    return s * r * tr;   // p' = p*S*R*T
}

inline void toHostPoint(const float* in, const ConvertOptions& o, double out[3])
{
    out[0] = in[0] * o.scale;
    out[1] = in[1] * o.scale;
    out[2] = (o.flipZ ? -in[2] : in[2]) * o.scale;
}

inline void toHoudiniPoint(const double* in, const ConvertOptions& o, float out[3])
{
    out[0] = float(in[0] / o.scale);
    out[1] = float(in[1] / o.scale);
    out[2] = float((o.flipZ ? -in[2] : in[2]) / o.scale);
}

// Direction vectors (normals): reflection only, no scale.
inline void toHostVector(const float* in, const ConvertOptions& o, double out[3])
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = o.flipZ ? -in[2] : in[2];
}

// Change of basis for a reflection F = diag(1,1,-1): M' = F * M * F, then
// scale the translation. F is its own inverse.
inline Matrix4 toHostMatrix(const Matrix4& h, const ConvertOptions& o)
{
    Matrix4 r = h;
    if (o.flipZ)
    {
        // Negate row 2 and column 2 (the element [2][2] is negated twice).
        for (int j = 0; j < 4; ++j) r.m[2][j] = -r.m[2][j];
        for (int i = 0; i < 4; ++i) r.m[i][2] = -r.m[i][2];
    }
    for (int j = 0; j < 3; ++j) r.m[3][j] *= o.scale;
    return r;
}

inline Matrix4 toHoudiniMatrix(const Matrix4& host, const ConvertOptions& o)
{
    ConvertOptions inv = o;
    inv.scale = 1.0 / o.scale;
    return toHostMatrix(host, inv);
}

// Reverse the vertex order of each face in place. Works on the vertex list
// and on any per-vertex attribute laid out in the same order (tupleSize).
template <typename T>
inline void reverseWinding(std::vector<T>& perVertex, const std::vector<int>& faceCounts, int tupleSize = 1)
{
    size_t start = 0;
    for (int fc : faceCounts)
    {
        if (fc < 0) continue;
        size_t a = start, b = start + size_t(fc) - 1;
        while (a < b)
        {
            for (int k = 0; k < tupleSize; ++k)
            {
                T tmp = perVertex[a * tupleSize + k];
                perVertex[a * tupleSize + k] = perVertex[b * tupleSize + k];
                perVertex[b * tupleSize + k] = tmp;
            }
            ++a; --b;
        }
        start += size_t(fc);
    }
}

inline bool nearlyEqual(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

} // namespace geo
} // namespace hapi
