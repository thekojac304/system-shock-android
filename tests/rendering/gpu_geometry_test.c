/* SPDX-License-Identifier: GPL-3.0-or-later
 * Build: cc -std=c11 -Wall -Wextra -Werror -O2 gpu_geometry_test.c -o geometry-test
 * Pure production helper: no game assets, renderer, GL context or device API.
 */
#include "../../SourceSnapshot/src/MacSrc/GpuGeometry.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Every boundary start and both windings must give the same decision. */
static void check_rotations(int n, const int32_t (*xy)[2], bool expected) {
    int32_t reordered[32][2];
    int32_t xyz[32][3];
    unsigned char clipped[32];
    for (int reverse = 0; reverse < 2; ++reverse)
        for (int start = 0; start < n; ++start) {
            for (int i = 0; i < n; ++i) {
                int index = (start + (reverse ? n - i : i)) % n;
                reordered[i][0] = xy[index][0];
                reordered[i][1] = xy[index][1];
                xyz[i][0] = xy[index][0];
                xyz[i][1] = xy[index][1];
                xyz[i][2] = 65536;
                clipped[i] = (unsigned char)(i % 2);
            }
            assert(gw_polygon_canonicalize_clipped_positions(
                n, (const int32_t (*)[3])xyz, clipped, 1024, 768, reordered) == 0);
            assert(gw_polygon_is_convex(n, (const int32_t (*)[2])reordered) == expected);
        }
}

static void measured_duplicate_projection(void) {
    /* Unmodified numeric fixture from build6-device.log: frame100, level1,
     * terrain tile18,19, viewport1024x768, camera18.1114807,18.254715,1.72702026.
     * The first point is PF_CLIPPNT (flags2f); second is ordinary (flags0f).
     * Equal view-space xyz was projected by two different fixed-point formulas,
     * producing a 387-raw-unit reversed edge. Keep the strict guard unchanged. */
    const int32_t measured_xy[4][2] = {
        {17349632, 0}, {17349245, 0}, {0, 28653696}, {0, 0}
    };
    const int32_t measured_xyz[4][3] = {
        {-18839, 39008, 39008}, {-18839, 39008, 39008},
        {-30188, -4184, 30188}, {-43726, 43726, 43726}
    };
    const unsigned char measured_clipped[4] = {1, 0, 1, 1};
    for (int reverse = 0; reverse < 2; ++reverse)
        for (int start = 0; start < 4; ++start) {
            int32_t xy[4][2], xyz[4][3];
            unsigned char clipped[4];
            for (int i = 0; i < 4; ++i) {
                int index = (start + (reverse ? 4 - i : i)) % 4;
                memcpy(xy[i], measured_xy[index], sizeof(xy[i]));
                memcpy(xyz[i], measured_xyz[index], sizeof(xyz[i]));
                clipped[i] = measured_clipped[index];
            }
            assert(!gw_polygon_is_convex(4, (const int32_t (*)[2])xy));
            assert(gw_polygon_canonicalize_clipped_positions(
                4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 1);
            assert(gw_polygon_is_convex(4, (const int32_t (*)[2])xy));
            for (int i = 0; i < 4; ++i) {
                int index = (start + (reverse ? 4 - i : i)) % 4;
                const int32_t *expected = measured_xy[index == 0 ? 1 : index];
                assert(!memcmp(xy[i], expected, sizeof(xy[i])));
                assert(!memcmp(xyz[i], measured_xyz[index], sizeof(xyz[i])));
            }
            assert(gw_polygon_canonicalize_clipped_positions(
                4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
        }

    int32_t xy[4][2], xyz[4][3];
    unsigned char clipped[4];
    memcpy(xy, measured_xy, sizeof(xy));
    memcpy(xyz, measured_xyz, sizeof(xyz));
    memcpy(clipped, measured_clipped, sizeof(clipped));
    ++xyz[0][2]; /* Close in world space is insufficient: exact equality only. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    assert(!memcmp(xy, measured_xy, sizeof(xy)));
    memcpy(xyz, measured_xyz, sizeof(xyz));
    clipped[1] = 1; /* No ordinary counterpart: retain conservative fallback. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    clipped[1] = 0;
    xy[0][0] = xy[1][0] + 514; /* Greater than the viewport-derived X bound. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    assert(!gw_polygon_is_convex(4, (const int32_t (*)[2])xy));
    memcpy(xy, measured_xy, sizeof(xy));
    xy[0][1] = 386; /* Greater than the viewport-derived Y bound. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    memcpy(xy, measured_xy, sizeof(xy));
    memcpy(xyz[3], xyz[1], sizeof(xyz[3]));
    clipped[3] = 0; /* Conflicting ordinary projections are not normalized. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    memcpy(xyz, measured_xyz, sizeof(xyz));
    clipped[3] = 1;
    xy[0][0] = INT32_MIN;
    xy[1][0] = INT32_MAX; /* Difference must not overflow signed int32. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 0, 768, xy) == -1);
    assert(gw_polygon_canonicalize_clipped_positions(
        4, NULL, clipped, 1024, 768, xy) == -1);
}

/* Independent shoelace/edge-accounting oracle: triangles must cover exactly
 * the signed boundary area, use every original vertex, and cancel every
 * interior directed edge. This checks the result, not the ear selection order.
 */
static void check_model_mesh(int n, const int32_t (*xy)[2],
                             const unsigned char (*triangles)[3]) {
    int edges[32][32] = {{0}}, used[32] = {0};
    __int128 polygon_area = 0, triangle_area = 0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        polygon_area += (__int128)xy[i][0] * xy[j][1] - (__int128)xy[j][0] * xy[i][1];
    }
    assert(polygon_area != 0);
    for (int i = 0; i < n - 2; ++i) {
        __int128 area = 0;
        for (int j = 0; j < 3; ++j) {
            int a = triangles[i][j], b = triangles[i][(j + 1) % 3];
            assert(a < n && b < n && a != b);
            ++edges[a][b];
            used[a] = 1;
            area += (__int128)xy[a][0] * xy[b][1] - (__int128)xy[b][0] * xy[a][1];
        }
        assert(polygon_area > 0 ? area > 0 : area < 0);
        triangle_area += area;
    }
    assert(triangle_area == polygon_area);
    for (int a = 0; a < n; ++a) {
        assert(used[a]);
        for (int b = 0; b < n; ++b) {
            int boundary = ((a + 1) % n == b) - ((b + 1) % n == a);
            assert(edges[a][b] - edges[b][a] == boundary);
            assert(edges[a][b] <= 1);
        }
    }
}

static void check_model_rotations(int n, const int32_t (*xy)[2], bool expected) {
    int32_t reordered[32][2], before[32][2];
    struct {
        unsigned char left[8], triangles[30][3], right[8];
    } output, untouched;
    unsigned char repeated[30][3];
    for (int reverse = 0; reverse < 2; ++reverse)
        for (int start = 0; start < n; ++start) {
            memset(reordered, 0, sizeof(reordered));
            for (int i = 0; i < n; ++i) {
                int index = (start + (reverse ? n - i : i)) % n;
                memcpy(reordered[i], xy[index], sizeof(reordered[i]));
            }
            memcpy(before, reordered, sizeof(before));
            memset(&output, 0xa5, sizeof(output));
            untouched = output;
            int count = gw_model_triangulate(n, (const int32_t (*)[2])reordered, output.triangles);
            assert(!memcmp(before, reordered, sizeof(before)));
            assert(count == (expected ? n - 2 : 0));
            if (!expected) {
                assert(!memcmp(&output, &untouched, sizeof(output)));
                continue;
            }
            assert(!memcmp(output.left, untouched.left, sizeof(output.left)));
            assert(!memcmp(output.right, untouched.right, sizeof(output.right)));
            for (int i = count; i < 30; ++i)
                assert(!memcmp(output.triangles[i], untouched.triangles[i], 3));
            check_model_mesh(n, (const int32_t (*)[2])reordered,
                             (const unsigned char (*)[3])output.triangles);
            assert(gw_model_triangulate(n, (const int32_t (*)[2])reordered, repeated) == count);
            assert(!memcmp(output.triangles, repeated, (size_t)count * 3));
        }
}

static void model_triangulation(void) {
    /* build8-gpu-timing.log, 14:49:42, object440. The projected outline is
     * mildly concave, but its existing diagonal0->2 is valid and must remain. */
    const int32_t object440[4][2] = {
        {33496751, 19795574}, {29749620, 18553895},
        {29205010, 18277898}, {34610693, 20166201}
    };
    unsigned char triangles[30][3];
    const unsigned char old_fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
    assert(!gw_polygon_is_convex(4, object440));
    assert(gw_model_triangulate(4, object440, triangles) == 2);
    assert(!memcmp(triangles, old_fan, sizeof(old_fan)));
    check_model_rotations(4, object440, true);

    /* This reflex corner instead requires diagonal1->3. */
    const int32_t alternate[4][2] = {{4 * 65536, 0}, {65536, 65536},
                                    {0, 4 * 65536}, {0, 0}};
    assert(gw_model_triangulate(4, alternate, triangles) == 2);
    int alternate_diagonal = 0;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j) {
            int a = triangles[i][j], b = triangles[i][(j + 1) % 3];
            assert(!((a == 0 && b == 2) || (a == 2 && b == 0)));
            if ((a == 1 && b == 3) || (a == 3 && b == 1)) ++alternate_diagonal;
        }
    assert(alternate_diagonal == 2);
    check_model_rotations(4, alternate, true);

    const int32_t notch[8][2] = {{0, 0}, {6, 0}, {6, 6}, {4, 6},
                                {4, 2}, {2, 2}, {2, 6}, {0, 6}};
    const int32_t collinear_edge[5][2] = {{0, 0}, {2, 0}, {4, 0}, {4, 4}, {0, 4}};
    const int32_t triangle[3][2] = {{0, 0}, {4, 0}, {2, 3}};
    const int32_t quad[4][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    check_model_rotations(8, notch, true);
    check_model_rotations(5, collinear_edge, true);
    check_model_rotations(3, triangle, true);
    check_model_rotations(4, quad, true);
    assert(gw_model_triangulate(4, quad, triangles) == 2);
    assert(!memcmp(triangles, old_fan, sizeof(old_fan)));

    const int32_t bowtie[4][2] = {{0, 0}, {4, 4}, {0, 4}, {4, 0}};
    const int32_t star[5][2] = {{0, 100}, {-59, -81}, {95, 31}, {-95, 31}, {59, -81}};
    const int32_t touching[5][2] = {{0, 0}, {4, 0}, {4, 4}, {2, 0}, {0, 4}};
    const int32_t backtracking[5][2] = {{0, 0}, {4, 0}, {2, 0}, {4, 4}, {0, 4}};
    const int32_t repeated[5][2] = {{0, 0}, {4, 0}, {4, 0}, {4, 4}, {0, 4}};
    const int32_t repeated_remote[5][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 0}, {0, 4}};
    const int32_t collinear[3][2] = {{0, 0}, {1, 1}, {2, 2}};
    const int32_t single_point[3][2] = {{0, 0}, {0, 0}, {0, 0}};
    check_model_rotations(4, bowtie, false);
    check_model_rotations(5, star, false);
    check_model_rotations(5, touching, false);
    check_model_rotations(5, backtracking, false);
    check_model_rotations(5, repeated, false);
    check_model_rotations(5, repeated_remote, false);
    check_model_rotations(3, collinear, false);
    check_model_rotations(3, single_point, false);

    /* Medical moving-floor fixture: qa0-floor-descent2.log frame18989.
     * Its nearly edge-on projected boundary crosses. Neither the unchanged
     * terrain guard nor the model helper may accept it by widening tolerance. */
    const int32_t terrain_crossing[6][2] = {
        {18452480, 0}, {25291134, 3708878}, {49296057, 16710147},
        {67108864, 26350848}, {67108864, 26351616}, {18459136, 0}
    };
    check_rotations(6, terrain_crossing, false);
    check_model_rotations(6, terrain_crossing, false);

    const int32_t extreme_quad[4][2] = {{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN},
                                       {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MAX}};
    const int32_t extreme_concave[5][2] = {{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN},
                                          {0, 0}, {INT32_MAX, INT32_MAX},
                                          {INT32_MIN, INT32_MAX}};
    /* Exact determinant -1 after cancellation of approximately 2^64 products:
     * double or a signed64 determinant cannot safely classify this triangle. */
    const int32_t extreme_thin[3][2] = {{INT32_MIN, INT32_MIN},
                                       {INT32_MAX, INT32_MAX - 1},
                                       {INT32_MAX - 1, INT32_MAX - 2}};
    check_model_rotations(4, extreme_quad, true);
    check_model_rotations(5, extreme_concave, true);
    check_model_rotations(3, extreme_thin, true);
    int32_t maximum[32][2] = {{0, 0}, {29 * 65536, 0}, {29 * 65536, 10 * 65536}};
    for (int i = 3; i < 31; ++i) {
        int x = 31 - i;
        maximum[i][0] = x * 65536;
        maximum[i][1] = (x % 2 ? 10 : 5) * 65536;
    }
    maximum[31][0] = 0; maximum[31][1] = 10 * 65536;
    check_model_rotations(32, (const int32_t (*)[2])maximum, true);

    unsigned char unchanged[30][3];
    memset(triangles, 0xa5, sizeof(triangles));
    memcpy(unchanged, triangles, sizeof(triangles));
    assert(!gw_model_triangulate(3, NULL, triangles));
    assert(!gw_model_triangulate(3, triangle, NULL));
    const int invalid_counts[] = {-1, 0, 2, 33, INT_MAX};
    for (unsigned i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]); ++i)
        assert(!gw_model_triangulate(invalid_counts[i], triangle, triangles));
    assert(!memcmp(triangles, unchanged, sizeof(triangles)));
    puts("PASS: model triangulation: measured object440 keeps fan, alternate diagonal, concave 8/32 vertices, both windings/all starts, preserved vertices, exact area/edge coverage, atomic rejection of crossings/touching/duplicates/zero area, int32 extremes and determinant cancellation");
}

int main(void) {
    measured_duplicate_projection();
    model_triangulation();
    const int32_t triangle[][2] = {{0, 0}, {8 * 65536, 0}, {3 * 65536, 6 * 65536}};
    const int32_t quad[][2] = {{-5 * 65536, -3 * 65536}, {7 * 65536, -2 * 65536},
                             {9 * 65536, 5 * 65536}, {-2 * 65536, 4 * 65536}};
    const int32_t clipped[][2] = {{0, 0}, {65536, 0}, {2 * 65536, 2},
                                 {2 * 65536, 65536}, {0, 65536}};
    const int32_t clipped_inward[][2] = {{0, 0}, {65536, 1}, {2 * 65536, 0},
                                        {2 * 65536, 65536}, {0, 65536}};
    const int32_t outside_edge_tolerance[][2] = {{0, 0}, {65536, 2}, {2 * 65536, 0},
                                               {2 * 65536, 65536}, {0, 65536}};
    const int32_t concave[][2] = {{0, 0}, {4 * 65536, 0}, {2 * 65536, 65536},
                                 {4 * 65536, 4 * 65536}, {0, 4 * 65536}};
    const int32_t bowtie[][2] = {{0, 0}, {4 * 65536, 4 * 65536},
                                {0, 4 * 65536}, {4 * 65536, 0}};
    /* A pentagram's consecutive turns agree, but its boundary is not convex. */
    const int32_t star[][2] = {{0, 100 * 65536}, {-59 * 65536, -81 * 65536},
                              {95 * 65536, 31 * 65536}, {-95 * 65536, 31 * 65536},
                              {59 * 65536, -81 * 65536}};
    const int32_t collinear[][2] = {{0, 0}, {65536, 65536}, {3 * 65536, 3 * 65536},
                                   {2 * 65536, 2 * 65536}, {-65536, -65536}};
    const int32_t same_point[][2] = {{17, -31}, {17, -31}, {17, -31}, {17, -31}};
    const int32_t repeated_vertex[][2] = {{0, 0}, {65536, 0}, {65536, 0},
                                         {65536, 65536}, {0, 65536}};
    const int32_t extremes[][2] = {{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN},
                                  {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MAX}};
    const int32_t extreme_concave[][2] = {{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN},
                                         {0, 0}, {INT32_MAX, INT32_MAX},
                                         {INT32_MIN, INT32_MAX}};
    const int32_t extreme_collinear[][2] = {{INT32_MIN, INT32_MIN}, {0, 0},
                                           {INT32_MAX, INT32_MAX}};
    check_rotations(3, triangle, true);
    check_rotations(4, quad, true);
    check_rotations(5, clipped, true);
    check_rotations(5, clipped_inward, true);
    check_rotations(5, outside_edge_tolerance, false);
    check_rotations(5, concave, false);
    check_rotations(4, bowtie, false);
    check_rotations(5, star, false);
    check_rotations(5, collinear, true);
    check_rotations(4, same_point, true);
    check_rotations(5, repeated_vertex, true);
    check_rotations(4, extremes, true);
    check_rotations(5, extreme_concave, false);
    check_rotations(3, extreme_collinear, true);
    assert(!gw_polygon_is_convex(3, NULL));
    assert(!gw_polygon_is_convex(-1, triangle));
    assert(!gw_polygon_is_convex(0, triangle));
    assert(!gw_polygon_is_convex(2, triangle));
    assert(!gw_polygon_is_convex(33, triangle));
    assert(!gw_polygon_is_convex(INT_MAX, triangle));
    int32_t maximum_vertices[32][2];
    for (int i = 0; i < 32; ++i) {
        int corner = i / 8;
        int t = (i % 8) * 65536;
        maximum_vertices[i][0] = corner == 0 ? t : corner == 1 ? 8 * 65536 :
                                corner == 2 ? 8 * 65536 - t : 0;
        maximum_vertices[i][1] = corner == 0 ? 0 : corner == 1 ? t :
                                corner == 2 ? 8 * 65536 : 8 * 65536 - t;
    }
    check_rotations(32, (const int32_t (*)[2])maximum_vertices, true);
    puts("PASS: production polygon guard: measured duplicate projection with bounded GPU-only canonicalization, both windings/all starts, 3..32 vertices, clipped tolerance, concavity, crossing/star topology, zero-area/repeated points, int32 extremes");
    return 0;
}
