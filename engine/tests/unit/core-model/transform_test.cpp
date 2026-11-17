#include <instantee/core_model/transform.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::core;

  {
    const Mat4 i = Mat4::identity();
    const Vec3 p = transform_point(i, Vec3{3, 4, 5});
    expect(nearly_equal(p, Vec3{3, 4, 5}), "identity preserves point");
  }

  {
    const Mat4 t = translation(Vec3{10, -2, 1});
    expect(nearly_equal(transform_point(t, Vec3{1, 2, 3}), Vec3{11, 0, 4}),
           "translation reference");
  }

  {
    const Mat4 s = scaling(Vec3{2, 3, 4});
    expect(nearly_equal(transform_point(s, Vec3{1, 1, 1}), Vec3{2, 3, 4}),
           "scale reference");
  }

  {
    // 90°: +X maps toward +Y (canvas down).
    const Mat4 r = rotation_z(std::numbers::pi_v<double> / 2.0);
    expect(nearly_equal(transform_point(r, Vec3{1, 0, 0}), Vec3{0, 1, 0}),
           "rotation_z +X -> +Y");
    expect(nearly_equal(transform_vector(r, Vec3{0, 1, 0}), Vec3{-1, 0, 0}),
           "rotation_z +Y -> -X");
  }

  {
    // mul(A,B) applies B first: translate then rotate about origin.
    const Mat4 r = rotation_z(std::numbers::pi_v<double> / 2.0);
    const Mat4 t = translation(Vec3{1, 0, 0});
    const Mat4 rt = mul(r, t);
    expect(nearly_equal(transform_point(rt, Vec3{0, 0, 0}), Vec3{0, 1, 0}),
           "R*T applies T then R");
  }

  {
    LocalTransform local;
    local.translate = {100, 50, 0};
    local.scale = {2, 2, 1};
    local.rotation_z_rad = std::numbers::pi_v<double> / 2.0;
    local.pivot = {10, 0, 0};
    const Mat4 m = local_matrix(local);
    // Pivot stays fixed in parent space before translation... after full local
    // matrix, pivot maps to translate + pivot (rotation/scale about pivot).
    expect(nearly_equal(transform_point(m, local.pivot),
                        Vec3{local.translate.x + local.pivot.x,
                             local.translate.y + local.pivot.y,
                             local.translate.z + local.pivot.z}),
           "pivot point maps to translate+pivot");
    // Point at pivot+(1,0): scale*2 -> (2,0), rotate 90° -> (0,2), then +T+P.
    expect(nearly_equal(transform_point(m, Vec3{11, 0, 0}), Vec3{110, 52, 0}),
           "TRS about pivot reference");
  }

  {
    const Mat4 parent = translation(Vec3{5, 0, 0});
    LocalTransform child;
    child.translate = {0, 7, 0};
    const Mat4 world = combine_parent_local(parent, local_matrix(child));
    expect(nearly_equal(transform_point(world, Vec3{0, 0, 0}), Vec3{5, 7, 0}),
           "parent*local hierarchy");
  }

  {
    // Associativity of mul on sample matrices.
    const Mat4 a = translation(Vec3{1, 2, 3});
    const Mat4 b = scaling(Vec3{2, 1, 0.5});
    const Mat4 c = rotation_z(0.3);
    expect(nearly_equal(mul(a, mul(b, c)), mul(mul(a, b), c)),
           "mat4 mul associativity");
  }

  {
    const Mat3 a = mat3_from_mat4(rotation_z(0.5));
    const Mat3 b = mat3_from_mat4(scaling(Vec3{2, 3, 1}));
    const Mat3 ab = mul(a, b);
    expect(nearly_equal(ab(0, 0),
                        mul(rotation_z(0.5), scaling(Vec3{2, 3, 1}))(0, 0)),
           "mat3 matches mat4 upper-left");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
