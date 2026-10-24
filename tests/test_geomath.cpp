// Unit tests for the coordinate conversion that every mesh goes through.
#include "GeoMath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace hapi;
using namespace hapi::geo;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define CHECK_NEAR(a, b) CHECK(nearlyEqual((a), (b), 1e-5))

static void testPointRoundTrip()
{
    ConvertOptions o;
    float h[3] = {1.0f, 2.0f, 3.0f};
    double c[3];
    toHostPoint(h, o, c);
    CHECK_NEAR(c[0], 100.0); CHECK_NEAR(c[1], 200.0); CHECK_NEAR(c[2], -300.0);
    float back[3];
    toHoudiniPoint(c, o, back);
    CHECK_NEAR(back[0], 1.0); CHECK_NEAR(back[1], 2.0); CHECK_NEAR(back[2], 3.0);
}

static void testReverseWinding()
{
    std::vector<int> faceCounts = {4, 3};
    std::vector<int> verts = {0, 1, 2, 3, 4, 5, 6};
    reverseWinding(verts, faceCounts);
    int expected[] = {3, 2, 1, 0, 6, 5, 4};
    for (int i = 0; i < 7; ++i) CHECK(verts[size_t(i)] == expected[i]);

    // Per-vertex tuples travel with their vertex.
    std::vector<float> uv = {0,0,0, 1,0,0, 1,1,0, 0,1,0};
    std::vector<int> quad = {4};
    reverseWinding(uv, quad, 3);
    CHECK_NEAR(uv[0], 0); CHECK_NEAR(uv[1], 1);   // first is now old last (0,1)
    CHECK_NEAR(uv[9], 0); CHECK_NEAR(uv[10], 0);  // last is old first (0,0)
}

static void testQuaternionToMatrix()
{
    // 90 degrees about Y: x axis -> -z in a right-handed system.
    Transform t;
    const float s = std::sin(float(M_PI) / 4), c = std::cos(float(M_PI) / 4);
    t.quaternion[0] = 0; t.quaternion[1] = s; t.quaternion[2] = 0; t.quaternion[3] = c;
    t.position[0] = 1; t.position[1] = 2; t.position[2] = 3;
    t.scale[0] = t.scale[1] = t.scale[2] = 2;
    Matrix4 m = toMatrix(t);
    double in[3] = {1, 0, 0}, out[3];
    m.transformPoint(in, out);
    // scale 2 -> (2,0,0); rotate 90 about Y -> (0,0,-2); translate -> (1,2,1)
    CHECK_NEAR(out[0], 1.0); CHECK_NEAR(out[1], 2.0); CHECK_NEAR(out[2], 1.0);
}

static void testMatrixConversionMatchesPointConversion()
{
    // Converting a point with the converted matrix must equal converting
    // the transformed point: host(M)(host(p)) == host(M(p)).
    Transform t;
    const float s = std::sin(0.3f), c = std::cos(0.3f);
    t.quaternion[0] = s * 0.6f; t.quaternion[1] = s * 0.8f; t.quaternion[2] = 0; t.quaternion[3] = c;
    t.position[0] = 0.5f; t.position[1] = -1.0f; t.position[2] = 2.0f;
    t.scale[0] = 1; t.scale[1] = 2; t.scale[2] = 3;
    Matrix4 mh = toMatrix(t);
    ConvertOptions o;
    Matrix4 mc = toHostMatrix(mh, o);

    float p[3] = {0.25f, 0.5f, -0.75f};
    double ph[3] = {p[0], p[1], p[2]}, q[3];
    mh.transformPoint(ph, q);                 // Houdini: M(p)
    float qf[3] = {float(q[0]), float(q[1]), float(q[2])};
    double expected[3];
    toHostPoint(qf, o, expected);             // host(M(p))

    double pc[3], got[3];
    toHostPoint(p, o, pc);                    // host(p)
    mc.transformPoint(pc, got);               // host(M)(host(p))
    for (int i = 0; i < 3; ++i) CHECK_NEAR(got[i], expected[i]);

    // And back again.
    Matrix4 round = toHoudiniMatrix(mc, o);
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) CHECK_NEAR(round.m[i][j], mh.m[i][j]);
}

static void testReflectionKeepsFacing()
{
    // Houdini winds polygons clockwise (normal = -cross(b-a, c-a) in its
    // right-handed space). The triangle a=(0,0,0) b=(1,0,0) c=(0,0,1) has
    // -cross((1,0,0),(0,0,1)) = -(0,-1,0) = +Y, so it faces up in Houdini.
    // Cinema 4D computes CalcFaceNormal = cross(b-a, c-a) in its left-handed
    // space. After negating Z the same vertex order must still face +Y.
    ConvertOptions o; o.scale = 1.0;
    float hp[3][3] = {{0,0,0}, {1,0,0}, {0,0,1}};
    double cp[3][3];
    for (int i = 0; i < 3; ++i) toHostPoint(hp[i], o, cp[i]);
    std::vector<int> idx = {0, 1, 2};
    std::vector<int> fc = {3};
    if (o.reverseWinding) reverseWinding(idx, fc);
    const double* a = cp[idx[0]]; const double* b = cp[idx[1]]; const double* c = cp[idx[2]];
    double e1[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
    double e2[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
    double n[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
    CHECK(n[1] > 0);

    // And the opposite: reversing on top of the reflection would flip it.
    std::vector<int> rev = {0, 1, 2};
    reverseWinding(rev, fc);
    a = cp[rev[0]]; b = cp[rev[1]]; c = cp[rev[2]];
    double f1[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
    double f2[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
    CHECK(f1[2]*f2[0]-f1[0]*f2[2] < 0);
}

int main()
{
    testPointRoundTrip();
    testReverseWinding();
    testQuaternionToMatrix();
    testMatrixConversionMatchesPointConversion();
    testReflectionKeepsFacing();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("geomath: all tests passed\n");
    return 0;
}
