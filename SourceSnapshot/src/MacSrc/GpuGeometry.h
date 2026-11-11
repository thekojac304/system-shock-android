/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_GEOMETRY_H
#define SHOCK_GPU_GEOMETRY_H

#include <stdbool.h>
#include <stdint.h>

/* Clipping may emit the same view-space vertex twice: an ordinary point and
 * an intersection. The ordinary projector uses one multiply/divide; the clip
 * projector truncates x/z or y/z before multiplying by half the viewport.
 * Their screen coordinates can therefore differ despite EXACT equal xyz.
 *
 * Normalize only clipped points that have an unambiguous ordinary counterpart.
 * Keep all vertices/attributes, and change only the caller's GPU screen copy.
 * The allowed delta is derived from those two projection formulas: no more
 * than ceil(viewport_dimension/2)+1 raw 16.16 units per coordinate (under .008
 * pixel at 1024x768). Larger differences and clipped-only groups are untouched.
 * The convexity guard below remains unchanged and validates the final outline.
 * Returns changed-point count, or -1 for invalid arguments.
 */
static inline int gw_polygon_canonicalize_clipped_positions(
    int n, const int32_t (*xyz)[3], const unsigned char *clipped,
    int width, int height, int32_t (*xy)[2]) {
    if (!xyz || !clipped || !xy || n < 3 || n > 32 || width <= 0 || height <= 0)
        return -1;
    int64_t limit_x = width / 2 + width % 2 + 1;
    int64_t limit_y = height / 2 + height % 2 + 1;
    int changed = 0;
    for (int i = 0; i < n; ++i) {
        if (!clipped[i]) continue;
        int representative = -1;
        bool ambiguous = false;
        for (int j = 0; j < n; ++j) {
            if (clipped[j] || xyz[i][0] != xyz[j][0] ||
                xyz[i][1] != xyz[j][1] || xyz[i][2] != xyz[j][2]) continue;
            if (representative < 0) representative = j;
            else if (xy[j][0] != xy[representative][0] ||
                     xy[j][1] != xy[representative][1]) ambiguous = true;
        }
        if (representative < 0 || ambiguous) continue;
        int64_t dx = (int64_t)xy[i][0] - xy[representative][0];
        int64_t dy = (int64_t)xy[i][1] - xy[representative][1];
        if (dx < -limit_x || dx > limit_x || dy < -limit_y || dy > limit_y)
            continue;
        if (dx || dy) {
            xy[i][0] = xy[representative][0];
            xy[i][1] = xy[representative][1];
            ++changed;
        }
    }
    return changed;
}

/* Input coordinates are the engine's projected signed 16.16 values. Triangle
 * fans are safe only when every vertex lies on the same side of every directed
 * boundary edge. Testing just consecutive turns would admit some star polygons.
 *
 * A two-raw-unit edge-distance tolerance admits projection/clipping roundoff:
 * |cross| <= 2 * (|dx| + |dy|). Two raw units are 1/32768 of a screen pixel;
 * they are NOT two pixels. This bound applies to each edge/vertex comparison,
 * not to arbitrary independent perturbations of a complete collinear chain.
 * Entirely collinear/repeated-point polygons are harmless zero-area fans.
 *
 * Cast before subtracting. An int32 difference can exceed INT32_MAX, and a
 * determinant can exceed INT64_MAX; double covers the complete input range.
 */
static inline bool gw_polygon_is_convex(int n, const int32_t (*xy)[2]) {
    if (!xy || n < 3 || n > 32) return false;
    int winding = 0;
    for (int i = 0; i < n; ++i) {
        int next = (i + 1) % n;
        double dx = (double)xy[next][0] - (double)xy[i][0];
        double dy = (double)xy[next][1] - (double)xy[i][1];
        double tolerance = 2.0 * ((dx < 0.0 ? -dx : dx) +
                                  (dy < 0.0 ? -dy : dy));
        for (int j = 0; j < n; ++j) {
            double px = (double)xy[j][0] - (double)xy[i][0];
            double py = (double)xy[j][1] - (double)xy[i][1];
            double cross = dx * py - dy * px;
            int sign = (cross > tolerance) - (cross < -tolerance);
            if (sign && winding && sign != winding) return false;
            if (sign) winding = sign;
        }
    }
    return true;
}

/* Model topology uses exact predicates, not the terrain roundoff tolerance.
 * Android's arm64 Clang supports this integer type. Coordinate differences can
 * need 33 bits, a determinant 66 bits, and the 32-vertex area sum 71 bits.
 * Widen BEFORE subtracting/multiplying so every int32 input is representable.
 */
typedef __int128 GwGeometryWide;

static inline GwGeometryWide gw_model_cross(const int32_t a[2],
                                            const int32_t b[2],
                                            const int32_t c[2]) {
    return ((GwGeometryWide)b[0] - a[0]) * ((GwGeometryWide)c[1] - a[1]) -
           ((GwGeometryWide)b[1] - a[1]) * ((GwGeometryWide)c[0] - a[0]);
}

static inline bool gw_model_between(const int32_t a[2], const int32_t b[2],
                                     const int32_t p[2]) {
    return ((a[0] <= p[0] && p[0] <= b[0]) || (b[0] <= p[0] && p[0] <= a[0])) &&
           ((a[1] <= p[1] && p[1] <= b[1]) || (b[1] <= p[1] && p[1] <= a[1]));
}

static inline bool gw_model_edges_intersect(const int32_t a[2], const int32_t b[2],
                                            const int32_t c[2], const int32_t d[2]) {
    GwGeometryWide ab_c = gw_model_cross(a, b, c), ab_d = gw_model_cross(a, b, d);
    GwGeometryWide cd_a = gw_model_cross(c, d, a), cd_b = gw_model_cross(c, d, b);
    if ((!ab_c && gw_model_between(a, b, c)) || (!ab_d && gw_model_between(a, b, d)) ||
        (!cd_a && gw_model_between(c, d, a)) || (!cd_b && gw_model_between(c, d, b)))
        return true;
    return ((ab_c > 0 && ab_d < 0) || (ab_c < 0 && ab_d > 0)) &&
           ((cd_a > 0 && cd_b < 0) || (cd_a < 0 && cd_b > 0));
}

/* For a simple boundary, a strictly convex corner with no other remaining
 * vertex in/on its triangle is an ear. Including triangle boundaries prevents
 * a diagonal from skipping a collinear vertex or its distinct UV/light value.
 */
static inline bool gw_model_is_ear(const int32_t (*xy)[2], int a, int b, int c,
                                   int winding, int count, const unsigned char *ring) {
    GwGeometryWide cross = gw_model_cross(xy[a], xy[b], xy[c]);
    if (winding > 0 ? cross <= 0 : cross >= 0) return false;
    for (int i = 0; i < count; ++i) {
        int p = ring[i];
        if (p == a || p == b || p == c) continue;
        GwGeometryWide ab = gw_model_cross(xy[a], xy[b], xy[p]);
        GwGeometryWide bc = gw_model_cross(xy[b], xy[c], xy[p]);
        GwGeometryWide ca = gw_model_cross(xy[c], xy[a], xy[p]);
        if (winding > 0 ? (ab >= 0 && bc >= 0 && ca >= 0) :
                          (ab <= 0 && bc <= 0 && ca <= 0)) return false;
    }
    return true;
}

/* Triangulate only simple, nonzero-area model boundaries (3..32 vertices).
 * Return n-2 triangles as indices into the ORIGINAL vertices, preserving every
 * attribute. Keep the existing fan if it is valid; otherwise use bounded ear
 * clipping. No allocation, new vertex, snapping, tolerance or game-state edit.
 * Repeated vertices, touching/crossing edges, backtracking and zero area fail.
 * Straight collinear boundary vertices may remain if nondegenerate triangles
 * can retain all of them. On ANY failure return 0 and leave output untouched.
 * Terrain must continue using gw_polygon_is_convex, never this model helper.
 */
static inline int gw_model_triangulate(int n, const int32_t (*xy)[2],
                                       unsigned char (*triangles)[3]) {
    if (!xy || !triangles || n < 3 || n > 32) return 0;
    for (int i = 0; i < n; ++i) {
        int next = (i + 1) % n, prev = (i + n - 1) % n;
        for (int j = i + 1; j < n; ++j) {
            if (xy[i][0] == xy[j][0] && xy[i][1] == xy[j][1]) return 0;
            if (j == next || (i == 0 && j == n - 1)) continue;
            if (gw_model_edges_intersect(xy[i], xy[next], xy[j], xy[(j + 1) % n]))
                return 0;
        }
        if (!gw_model_cross(xy[prev], xy[i], xy[next]) &&
            !gw_model_between(xy[prev], xy[next], xy[i])) return 0;
    }
    GwGeometryWide area = 0;
    for (int i = 1; i < n - 1; ++i) area += gw_model_cross(xy[0], xy[i], xy[i + 1]);
    if (!area) return 0;
    int winding = area > 0 ? 1 : -1;
    unsigned char result[30][3], ring[32];
    for (int i = 0; i < n; ++i) ring[i] = (unsigned char)i;

    /* Validate the old fan as successive ears, retaining its exact diagonal
     * choices where safe (including the measured mildly concave object440).
     * Removed fan tips are absent from the remaining boundary.
     */
    int produced = 0;
    for (int i = 1; i < n - 1; ++i) {
        if (!gw_model_is_ear(xy, 0, i, i + 1, winding, n - i, ring + i)) break;
        result[produced][0] = 0;
        result[produced][1] = (unsigned char)i;
        result[produced++][2] = (unsigned char)(i + 1);
    }
    if (produced != n - 2) {
        int count = n;
        produced = 0;
        while (count > 3) {
            bool found = false;
            for (int tip = 0; tip < count; ++tip) {
                int a = ring[(tip + count - 1) % count], b = ring[tip];
                int c = ring[(tip + 1) % count];
                if (!gw_model_is_ear(xy, a, b, c, winding, count, ring)) continue;
                result[produced][0] = (unsigned char)a;
                result[produced][1] = (unsigned char)b;
                result[produced++][2] = (unsigned char)c;
                for (int j = tip; j < count - 1; ++j) ring[j] = ring[j + 1];
                --count;
                found = true;
                break;
            }
            if (!found) return 0; /* No ear: preserve conservative fallback. */
        }
        if (!gw_model_is_ear(xy, ring[0], ring[1], ring[2], winding, 3, ring)) return 0;
        for (int j = 0; j < 3; ++j) result[produced][j] = ring[j];
        ++produced;
    }
    if (produced != n - 2) return 0;
    for (int i = 0; i < produced; ++i)
        for (int j = 0; j < 3; ++j) triangles[i][j] = result[i][j];
    return produced;
}

#endif
