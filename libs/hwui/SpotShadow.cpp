/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "OpenGLRenderer"

#define SHADOW_SHRINK_SCALE 0.1f

#include <math.h>
#include <stdlib.h>
#include <utils/Log.h>

#include "ShadowTessellator.h"
#include "SpotShadow.h"
#include "Vertex.h"

namespace android {
namespace uirenderer {

/**
 * Calculate the intersection of a ray with a polygon.
 * It assumes the ray originates inside the polygon.
 *
 * @param poly The polygon, which is represented in a Vector2 array.
 * @param polyLength The length of caster's polygon in terms of number of
 *                   vertices.
 * @param point the start of the ray
 * @param dx the x vector of the ray
 * @param dy the y vector of the ray
 * @return the distance along the ray if it intersects with the polygon FP_NAN if otherwise
 */
float SpotShadow::rayIntersectPoly(const Vector2* poly, int polyLength,
        const Vector2& point, float dx, float dy) {
    double px = point.x;
    double py = point.y;
    int p1 = polyLength - 1;
    for (int p2 = 0; p2 < polyLength; p2++) {
        double p1x = poly[p1].x;
        double p1y = poly[p1].y;
        double p2x = poly[p2].x;
        double p2y = poly[p2].y;
        // The math below is derived from solving this formula, basically the
        // intersection point should stay on both the ray and the edge of (p1, p2).
        // solve([p1x+t*(p2x-p1x)=dx*t2+px,p1y+t*(p2y-p1y)=dy*t2+py],[t,t2]);
        double div = (dx * (p1y - p2y) + dy * p2x - dy * p1x);
        if (div != 0) {
            double t = (dx * (p1y - py) + dy * px - dy * p1x) / (div);
            if (t >= 0 && t <= 1) {
                double t2 = (p1x * (py - p2y) + p2x * (p1y - py) +
                        px * (p2y - p1y)) / div;
                if (t2 > 0) {
                    return (float)t2;
                }
            }
        }
        p1 = p2;
    }
    return FP_NAN;
}

/**
 * Sort points by their X coordinates
 *
 * @param points the points as a Vector2 array.
 * @param pointsLength the number of vertices of the polygon.
 */
void SpotShadow::xsort(Vector2* points, int pointsLength) {
    quicksortX(points, 0, pointsLength - 1);
}

/**
 * compute the convex hull of a collection of Points
 *
 * @param points the points as a Vector2 array.
 * @param pointsLength the number of vertices of the polygon.
 * @param retPoly pre allocated array of floats to put the vertices
 * @return the number of points in the polygon 0 if no intersection
 */
int SpotShadow::hull(Vector2* points, int pointsLength, Vector2* retPoly) {
    xsort(points, pointsLength);
    int n = pointsLength;
    Vector2 lUpper[n];
    lUpper[0] = points[0];
    lUpper[1] = points[1];

    int lUpperSize = 2;

    for (int i = 2; i < n; i++) {
        lUpper[lUpperSize] = points[i];
        lUpperSize++;

        while (lUpperSize > 2 && !ccw(
                lUpper[lUpperSize - 3].x, lUpper[lUpperSize - 3].y,
                lUpper[lUpperSize - 2].x, lUpper[lUpperSize - 2].y,
                lUpper[lUpperSize - 1].x, lUpper[lUpperSize - 1].y)) {
            // Remove the middle point of the three last
            lUpper[lUpperSize - 2].x = lUpper[lUpperSize - 1].x;
            lUpper[lUpperSize - 2].y = lUpper[lUpperSize - 1].y;
            lUpperSize--;
        }
    }

    Vector2 lLower[n];
    lLower[0] = points[n - 1];
    lLower[1] = points[n - 2];

    int lLowerSize = 2;

    for (int i = n - 3; i >= 0; i--) {
        lLower[lLowerSize] = points[i];
        lLowerSize++;

        while (lLowerSize > 2 && !ccw(
                lLower[lLowerSize - 3].x, lLower[lLowerSize - 3].y,
                lLower[lLowerSize - 2].x, lLower[lLowerSize - 2].y,
                lLower[lLowerSize - 1].x, lLower[lLowerSize - 1].y)) {
            // Remove the middle point of the three last
            lLower[lLowerSize - 2] = lLower[lLowerSize - 1];
            lLowerSize--;
        }
    }
    int count = 0;

    for (int i = 0; i < lUpperSize; i++) {
        retPoly[count] = lUpper[i];
        count++;
    }

    for (int i = 1; i < lLowerSize - 1; i++) {
        retPoly[count] = lLower[i];
        count++;
    }
    // TODO: Add test harness which verify that all the points are inside the hull.
    return count;
}

/**
 * Test whether the 3 points form a counter clockwise turn.
 *
 * @param ax the x coordinate of point a
 * @param ay the y coordinate of point a
 * @param bx the x coordinate of point b
 * @param by the y coordinate of point b
 * @param cx the x coordinate of point c
 * @param cy the y coordinate of point c
 * @return true if a right hand turn
 */
bool SpotShadow::ccw(double ax, double ay, double bx, double by,
        double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax) > EPSILON;
}

/**
 * Calculates the intersection of poly1 with poly2 and put in poly2.
 *
 *
 * @param poly1 The 1st polygon, as a Vector2 array.
 * @param poly1Length The number of vertices of 1st polygon.
 * @param poly2 The 2nd and output polygon, as a Vector2 array.
 * @param poly2Length The number of vertices of 2nd polygon.
 * @return number of vertices in output polygon as poly2.
 */
int SpotShadow::intersection(Vector2* poly1, int poly1Length,
        Vector2* poly2, int poly2Length) {
    makeClockwise(poly1, poly1Length);
    makeClockwise(poly2, poly2Length);

    Vector2 poly[poly1Length * poly2Length + 2];
    int count = 0;
    int pcount = 0;

    // If one vertex from one polygon sits inside another polygon, add it and
    // count them.
    for (int i = 0; i < poly1Length; i++) {
        if (testPointInsidePolygon(poly1[i], poly2, poly2Length)) {
            poly[count] = poly1[i];
            count++;
            pcount++;

        }
    }

    int insidePoly2 = pcount;
    for (int i = 0; i < poly2Length; i++) {
        if (testPointInsidePolygon(poly2[i], poly1, poly1Length)) {
            poly[count] = poly2[i];
            count++;
        }
    }

    int insidePoly1 = count - insidePoly2;
    // If all vertices from poly1 are inside poly2, then just return poly1.
    if (insidePoly2 == poly1Length) {
        memcpy(poly2, poly1, poly1Length * sizeof(Vector2));
        return poly1Length;
    }

    // If all vertices from poly2 are inside poly1, then just return poly2.
    if (insidePoly1 == poly2Length) {
        return poly2Length;
    }

    // Since neither polygon fully contain the other one, we need to add all the
    // intersection points.
    Vector2 intersection;
    for (int i = 0; i < poly2Length; i++) {
        for (int j = 0; j < poly1Length; j++) {
            int poly2LineStart = i;
            int poly2LineEnd = ((i + 1) % poly2Length);
            int poly1LineStart = j;
            int poly1LineEnd = ((j + 1) % poly1Length);
            bool found = lineIntersection(
                    poly2[poly2LineStart].x, poly2[poly2LineStart].y,
                    poly2[poly2LineEnd].x, poly2[poly2LineEnd].y,
                    poly1[poly1LineStart].x, poly1[poly1LineStart].y,
                    poly1[poly1LineEnd].x, poly1[poly1LineEnd].y,
                    intersection);
            if (found) {
                poly[count].x = intersection.x;
                poly[count].y = intersection.y;
                count++;
            } else {
                Vector2 delta = poly2[i] - poly1[j];
                if (delta.lengthSquared() < EPSILON) {
                    poly[count] = poly2[i];
                    count++;
                }
            }
        }
    }

    if (count == 0) {
        return 0;
    }

    // Sort the result polygon around the center.
    Vector2 center(0.0f, 0.0f);
    for (int i = 0; i < count; i++) {
        center += poly[i];
    }
    center /= count;
    sort(poly, count, center);

#if DEBUG_SHADOW
    // Since poly2 is overwritten as the result, we need to save a copy to do
    // our verification.
    Vector2 oldPoly2[poly2Length];
    int oldPoly2Length = poly2Length;
    memcpy(oldPoly2, poly2, sizeof(Vector2) * poly2Length);
#endif

    // Filter the result out from poly and put it into poly2.
    poly2[0] = poly[0];
    int lastOutputIndex = 0;
    for (int i = 1; i < count; i++) {
        Vector2 delta = poly[i] - poly2[lastOutputIndex];
        if (delta.lengthSquared() >= EPSILON) {
            poly2[++lastOutputIndex] = poly[i];
        } else {
            // If the vertices are too close, pick the inner one, because the
            // inner one is more likely to be an intersection point.
            Vector2 delta1 = poly[i] - center;
            Vector2 delta2 = poly2[lastOutputIndex] - center;
            if (delta1.lengthSquared() < delta2.lengthSquared()) {
                poly2[lastOutputIndex] = poly[i];
            }
        }
    }
    int resultLength = lastOutputIndex + 1;

#if DEBUG_SHADOW
    testConvex(poly2, resultLength, "intersection");
    testConvex(poly1, poly1Length, "input poly1");
    testConvex(oldPoly2, oldPoly2Length, "input poly2");

    testIntersection(poly1, poly1Length, oldPoly2, oldPoly2Length, poly2, resultLength);
#endif

    return resultLength;
}

/**
 * Sort points about a center point
 *
 * @param poly The in and out polyogon as a Vector2 array.
 * @param polyLength The number of vertices of the polygon.
 * @param center the center ctr[0] = x , ctr[1] = y to sort around.
 */
void SpotShadow::sort(Vector2* poly, int polyLength, const Vector2& center) {
    quicksortCirc(poly, 0, polyLength - 1, center);
}

/**
 * Calculate the angle between and x and a y coordinate.
 * The atan2 range from -PI to PI, if we want to sort the vertices as clockwise,
 * we just negate the return angle.
 */
float SpotShadow::angle(const Vector2& point, const Vector2& center) {
    return -(float)atan2(point.y - center.y, point.x - center.x);
}

/**
 * Swap points pointed to by i and j
 */
void SpotShadow::swap(Vector2* points, int i, int j) {
    Vector2 temp = points[i];
    points[i] = points[j];
    points[j] = temp;
}

/**
 * quick sort implementation about the center.
 */
void SpotShadow::quicksortCirc(Vector2* points, int low, int high,
        const Vector2& center) {
    int i = low, j = high;
    int p = low + (high - low) / 2;
    float pivot = angle(points[p], center);
    while (i <= j) {
        while (angle(points[i], center) < pivot) {
            i++;
        }
        while (angle(points[j], center) > pivot) {
            j--;
        }

        if (i <= j) {
            swap(points, i, j);
            i++;
            j--;
        }
    }
    if (low < j) quicksortCirc(points, low, j, center);
    if (i < high) quicksortCirc(points, i, high, center);
}

/**
 * Sort points by x axis
 *
 * @param points points to sort
 * @param low start index
 * @param high end index
 */
void SpotShadow::quicksortX(Vector2* points, int low, int high) {
    int i = low, j = high;
    int p = low + (high - low) / 2;
    float pivot = points[p].x;
    while (i <= j) {
        while (points[i].x < pivot) {
            i++;
        }
        while (points[j].x > pivot) {
            j--;
        }

        if (i <= j) {
            swap(points, i, j);
            i++;
            j--;
        }
    }
    if (low < j) quicksortX(points, low, j);
    if (i < high) quicksortX(points, i, high);
}

/**
 * Test whether a point is inside the polygon.
 *
 * @param testPoint the point to test
 * @param poly the polygon
 * @return true if the testPoint is inside the poly.
 */
bool SpotShadow::testPointInsidePolygon(const Vector2 testPoint,
        const Vector2* poly, int len) {
    bool c = false;
    double testx = testPoint.x;
    double testy = testPoint.y;
    for (int i = 0, j = len - 1; i < len; j = i++) {
        double startX = poly[j].x;
        double startY = poly[j].y;
        double endX = poly[i].x;
        double endY = poly[i].y;

        if (((endY > testy) != (startY > testy)) &&
            (testx < (startX - endX) * (testy - endY)
             / (startY - endY) + endX)) {
            c = !c;
        }
    }
    return c;
}

/**
 * Make the polygon turn clockwise.
 *
 * @param polygon the polygon as a Vector2 array.
 * @param len the number of points of the polygon
 */
void SpotShadow::makeClockwise(Vector2* polygon, int len) {
    if (polygon == 0  || len == 0) {
        return;
    }
    if (!isClockwise(polygon, len)) {
        reverse(polygon, len);
    }
}

/**
 * Test whether the polygon is order in clockwise.
 *
 * @param polygon the polygon as a Vector2 array
 * @param len the number of points of the polygon
 */
bool SpotShadow::isClockwise(Vector2* polygon, int len) {
    double sum = 0;
    double p1x = polygon[len - 1].x;
    double p1y = polygon[len - 1].y;
    for (int i = 0; i < len; i++) {

        double p2x = polygon[i].x;
        double p2y = polygon[i].y;
        sum += p1x * p2y - p2x * p1y;
        p1x = p2x;
        p1y = p2y;
    }
    return sum < 0;
}

/**
 * Reverse the polygon
 *
 * @param polygon the polygon as a Vector2 array
 * @param len the number of points of the polygon
 */
void SpotShadow::reverse(Vector2* polygon, int len) {
    int n = len / 2;
    for (int i = 0; i < n; i++) {
        Vector2 tmp = polygon[i];
        int k = len - 1 - i;
        polygon[i] = polygon[k];
        polygon[k] = tmp;
    }
}

/**
 * Intersects two lines in parametric form. This function is called in a tight
 * loop, and we need double precision to get things right.
 *
 * @param x1 the x coordinate point 1 of line 1
 * @param y1 the y coordinate point 1 of line 1
 * @param x2 the x coordinate point 2 of line 1
 * @param y2 the y coordinate point 2 of line 1
 * @param x3 the x coordinate point 1 of line 2
 * @param y3 the y coordinate point 1 of line 2
 * @param x4 the x coordinate point 2 of line 2
 * @param y4 the y coordinate point 2 of line 2
 * @param ret the x,y location of the intersection
 * @return true if it found an intersection
 */
inline bool SpotShadow::lineIntersection(double x1, double y1, double x2, double y2,
        double x3, double y3, double x4, double y4, Vector2& ret) {
    double d = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (d == 0.0) return false;

    double dx = (x1 * y2 - y1 * x2);
    double dy = (x3 * y4 - y3 * x4);
    double x = (dx * (x3 - x4) - (x1 - x2) * dy) / d;
    double y = (dx * (y3 - y4) - (y1 - y2) * dy) / d;

    // The intersection should be in the middle of the point 1 and point 2,
    // likewise point 3 and point 4.
    if (((x - x1) * (x - x2) > EPSILON)
        || ((x - x3) * (x - x4) > EPSILON)
        || ((y - y1) * (y - y2) > EPSILON)
        || ((y - y3) * (y - y4) > EPSILON)) {
        // Not interesected
        return false;
    }
    ret.x = x;
    ret.y = y;
    return true;

}

/**
 * Compute a horizontal circular polygon about point (x , y , height) of radius
 * (size)
 *
 * @param points number of the points of the output polygon.
 * @param lightCenter the center of the light.
 * @param size the light size.
 * @param ret result polygon.
 */
void SpotShadow::computeLightPolygon(int points, const Vector3& lightCenter,
        float size, Vector3* ret) {
    // TODO: Caching all the sin / cos values and store them in a look up table.
    for (int i = 0; i < points; i++) {
        double angle = 2 * i * M_PI / points;
        ret[i].x = sinf(angle) * size + lightCenter.x;
        ret[i].y = cosf(angle) * size + lightCenter.y;
        ret[i].z = lightCenter.z;
    }
}

/**
* Generate the shadow from a spot light.
*
* @param poly x,y,z vertexes of a convex polygon that occludes the light source
* @param polyLength number of vertexes of the occluding polygon
* @param lightCenter the center of the light
* @param lightSize the radius of the light source
* @param lightVertexCount the vertex counter for the light polygon
* @param shadowTriangleStrip return an (x,y,alpha) triangle strip representing the shadow. Return
*                            empty strip if error.
*
*/
void SpotShadow::createSpotShadow(const Vector3* poly, int polyLength,
        const Vector3& lightCenter, float lightSize, int lightVertexCount,
        VertexBuffer& retStrips) {
    Vector3 light[lightVertexCount * 3];
    computeLightPolygon(lightVertexCount, lightCenter, lightSize, light);
    computeSpotShadow(light, lightVertexCount, lightCenter, poly, polyLength,
            retStrips);
}

/**
 * Generate the shadow spot light of shape lightPoly and a object poly
 *
 * @param lightPoly x,y,z vertex of a convex polygon that is the light source
 * @param lightPolyLength number of vertexes of the light source polygon
 * @param poly x,y,z vertexes of a convex polygon that occludes the light source
 * @param polyLength number of vertexes of the occluding polygon
 * @param shadowTriangleStrip return an (x,y,alpha) triangle strip representing the shadow. Return
 *                            empty strip if error.
 */
void SpotShadow::computeSpotShadow(const Vector3* lightPoly, int lightPolyLength,
        const Vector3& lightCenter, const Vector3* poly, int polyLength,
        VertexBuffer& shadowTriangleStrip) {
    // Point clouds for all the shadowed vertices
    Vector2 shadowRegion[lightPolyLength * polyLength];
    // Shadow polygon from one point light.
    Vector2 outline[polyLength];
    Vector2 umbraMem[polyLength * lightPolyLength];
    Vector2* umbra = umbraMem;

    int umbraLength = 0;

    // Validate input, receiver is always at z = 0 plane.
    bool inputPolyPositionValid = true;
    for (int i = 0; i < polyLength; i++) {
        if (poly[i].z <= 0.00001) {
            inputPolyPositionValid = false;
            ALOGE("polygon below the surface");
            break;
        }
        if (poly[i].z >= lightPoly[0].z) {
            inputPolyPositionValid = false;
            ALOGE("polygon above the light");
            break;
        }
    }

    // If the caster's position is invalid, don't draw anything.
    if (!inputPolyPositionValid) {
        return;
    }

    // Calculate the umbra polygon based on intersections of all outlines
    int k = 0;
    for (int j = 0; j < lightPolyLength; j++) {
        int m = 0;
        for (int i = 0; i < polyLength; i++) {
            float t = lightPoly[j].z - poly[i].z;
            if (t == 0) {
                return;
            }
            t = lightPoly[j].z / t;
            float x = lightPoly[j].x - t * (lightPoly[j].x - poly[i].x);
            float y = lightPoly[j].y - t * (lightPoly[j].y - poly[i].y);

            Vector2 newPoint = Vector2(x, y);
            shadowRegion[k] = newPoint;
            outline[m] = newPoint;

            k++;
            m++;
        }

        // For the first light polygon's vertex, use the outline as the umbra.
        // Later on, use the intersection of the outline and existing umbra.
        if (umbraLength == 0) {
            for (int i = 0; i < polyLength; i++) {
                umbra[i] = outline[i];
            }
            umbraLength = polyLength;
        } else {
            int col = ((j * 255) / lightPolyLength);
            umbraLength = intersection(outline, polyLength, umbra, umbraLength);
            if (umbraLength == 0) {
                break;
            }
        }
    }

    // Generate the penumbra area using the hull of all shadow regions.
    int shadowRegionLength = k;
    Vector2 penumbra[k];
    int penumbraLength = hull(shadowRegion, shadowRegionLength, penumbra);

    Vector2 fakeUmbra[polyLength];
    if (umbraLength < 3) {
        // If there is no real umbra, make a fake one.
        for (int i = 0; i < polyLength; i++) {
            float t = lightCenter.z - poly[i].z;
            if (t == 0) {
                return;
            }
            t = lightCenter.z / t;
            float x = lightCenter.x - t * (lightCenter.x - poly[i].x);
            float y = lightCenter.y - t * (lightCenter.y - poly[i].y);

            fakeUmbra[i].x = x;
            fakeUmbra[i].y = y;
        }

        // Shrink the centroid's shadow by 10%.
        // TODO: Study the magic number of 10%.
        Vector2 shadowCentroid =
                ShadowTessellator::centroid2d(fakeUmbra, polyLength);
        for (int i = 0; i < polyLength; i++) {
            fakeUmbra[i] = shadowCentroid * (1.0f - SHADOW_SHRINK_SCALE) +
                    fakeUmbra[i] * SHADOW_SHRINK_SCALE;
        }
#if DEBUG_SHADOW
        ALOGD("No real umbra make a fake one, centroid2d =  %f , %f",
                shadowCentroid.x, shadowCentroid.y);
#endif
        // Set the fake umbra, whose size is the same as the original polygon.
        umbra = fakeUmbra;
        umbraLength = polyLength;
    }

    generateTriangleStrip(penumbra, penumbraLength, umbra, umbraLength,
            shadowTriangleStrip);
}

/**
 * Generate a triangle strip given two convex polygons
 *
 * @param penumbra The outer polygon x,y vertexes
 * @param penumbraLength The number of vertexes in the outer polygon
 * @param umbra The inner outer polygon x,y vertexes
 * @param umbraLength The number of vertexes in the inner polygon
 * @param shadowTriangleStrip return an (x,y,alpha) triangle strip representing the shadow. Return
 *                            empty strip if error.
**/
void SpotShadow::generateTriangleStrip(const Vector2* penumbra, int penumbraLength,
        const Vector2* umbra, int umbraLength, VertexBuffer& shadowTriangleStrip) {
    const int rays = SHADOW_RAY_COUNT;
    const int layers = SHADOW_LAYER_COUNT;

    int size = rays * (layers + 1);
    float step = M_PI * 2 / rays;
    // Centroid of the umbra.
    Vector2 centroid = ShadowTessellator::centroid2d(umbra, umbraLength);
#if DEBUG_SHADOW
    ALOGD("centroid2d =  %f , %f", centroid.x, centroid.y);
#endif
    // Intersection to the penumbra.
    float penumbraDistPerRay[rays];
    // Intersection to the umbra.
    float umbraDistPerRay[rays];

    for (int i = 0; i < rays; i++) {
        // TODO: Setup a lookup table for all the sin/cos.
        float dx = sinf(step * i);
        float dy = cosf(step * i);
        umbraDistPerRay[i] = rayIntersectPoly(umbra, umbraLength, centroid,
                dx, dy);
        if (isnan(umbraDistPerRay[i])) {
            ALOGE("rayIntersectPoly returns NAN");
            return;
        }
        penumbraDistPerRay[i] = rayIntersectPoly(penumbra, penumbraLength,
                centroid, dx, dy);
        if (isnan(umbraDistPerRay[i])) {
            ALOGE("rayIntersectPoly returns NAN");
            return;
        }
    }

    int stripSize = getStripSize(rays, layers);
    AlphaVertex* shadowVertices = shadowTriangleStrip.alloc<AlphaVertex>(stripSize);
    int currentIndex = 0;

    // Calculate the vertices (x, y, alpha) in the shadow area.
    for (int layerIndex = 0; layerIndex <= layers; layerIndex++) {
        for (int rayIndex = 0; rayIndex < rays; rayIndex++) {
            float dx = sinf(step * rayIndex);
            float dy = cosf(step * rayIndex);
                float layerRatio = layerIndex / (float) layers;
                float deltaDist = layerRatio *
                        (umbraDistPerRay[rayIndex] - penumbraDistPerRay[rayIndex]);
                float currentDist = penumbraDistPerRay[rayIndex] + deltaDist;
                float op = calculateOpacity(layerRatio);
                AlphaVertex::set(&shadowVertices[currentIndex++],
                        dx * currentDist + centroid.x, dy * currentDist + centroid.y, op);
        }
    }
    // The centroid is in the umbra area, so the opacity is considered as 1.0.
    AlphaVertex::set(&shadowVertices[currentIndex++], centroid.x, centroid.y, 1.0);
#if DEBUG_SHADOW
    if (currentIndex != SHADOW_VERTEX_COUNT) {
        ALOGE("number of vertex generated for spot shadow is wrong!");
    }
    for (int i = 0; i < currentIndex; i++) {
        ALOGD("spot shadow value: i %d, (x:%f, y:%f, a:%f)", i, shadowVertices[i].x,
                shadowVertices[i].y, shadowVertices[i].alpha);
    }
#endif
}

/**
 * This is only for experimental purpose.
 * After intersections are calculated, we could smooth the polygon if needed.
 * So far, we don't think it is more appealing yet.
 *
 * @param level The level of smoothness.
 * @param rays The total number of rays.
 * @param rayDist (In and Out) The distance for each ray.
 *
 */
void SpotShadow::smoothPolygon(int level, int rays, float* rayDist) {
    for (int k = 0; k < level; k++) {
        for (int i = 0; i < rays; i++) {
            float p1 = rayDist[(rays - 1 + i) % rays];
            float p2 = rayDist[i];
            float p3 = rayDist[(i + 1) % rays];
            rayDist[i] = (p1 + p2 * 2 + p3) / 4;
        }
    }
}

/**
 * Calculate the opacity according to the distance. Ideally, the opacity is 1.0
 * in the umbra area, and fall off to 0.0 till the edge of penumbra area.
 *
 * @param layerRatio The distance ratio of current sample between umbra and penumbra area.
 *                   Penumbra edge is 0 and umbra edge is 1.
 * @return The opacity according to the distance between umbra and penumbra.
 */
float SpotShadow::calculateOpacity(float layerRatio) {
    return (layerRatio * layerRatio + layerRatio) / 2.0;
}

/**
 * Calculate the number of vertex we will create given a number of rays and layers
 *
 * @param rays number of points around the polygons you want
 * @param layers number of layers of triangle strips you need
 * @return number of vertex (multiply by 3 for number of floats)
 */
int SpotShadow::getStripSize(int rays, int layers) {
    return  (2 + rays + ((layers) * 2 * (rays + 1)));
}

#if DEBUG_SHADOW

#define TEST_POINT_NUMBER 128

/**
 * Calculate the bounds for generating random test points.
 */
void SpotShadow::updateBound(const Vector2 inVector, Vector2& lowerBound,
        Vector2& upperBound ) {
    if (inVector.x < lowerBound.x) {
        lowerBound.x = inVector.x;
    }

    if (inVector.y < lowerBound.y) {
        lowerBound.y = inVector.y;
    }

    if (inVector.x > upperBound.x) {
        upperBound.x = inVector.x;
    }

    if (inVector.y > upperBound.y) {
        upperBound.y = inVector.y;
    }
}

/**
 * For debug purpose, when things go wrong, dump the whole polygon data.
 */
static void dumpPolygon(const Vector2* poly, int polyLength, const char* polyName) {
    for (int i = 0; i < polyLength; i++) {
        ALOGD("polygon %s i %d x %f y %f", polyName, i, poly[i].x, poly[i].y);
    }
}

/**
 * Test whether the polygon is convex.
 */
bool SpotShadow::testConvex(const Vector2* polygon, int polygonLength,
        const char* name) {
    bool isConvex = true;
    for (int i = 0; i < polygonLength; i++) {
        Vector2 start = polygon[i];
        Vector2 middle = polygon[(i + 1) % polygonLength];
        Vector2 end = polygon[(i + 2) % polygonLength];

        double delta = (double(middle.x) - start.x) * (double(end.y) - start.y) -
                (double(middle.y) - start.y) * (double(end.x) - start.x);
        bool isCCWOrCoLinear = (delta >= EPSILON);

        if (isCCWOrCoLinear) {
            ALOGE("(Error Type 2): polygon (%s) is not a convex b/c start (x %f, y %f),"
                    "middle (x %f, y %f) and end (x %f, y %f) , delta is %f !!!",
                    name, start.x, start.y, middle.x, middle.y, end.x, end.y, delta);
            isConvex = false;
            break;
        }
    }
    return isConvex;
}

/**
 * Test whether or not the polygon (intersection) is within the 2 input polygons.
 * Using Marte Carlo method, we generate a random point, and if it is inside the
 * intersection, then it must be inside both source polygons.
 */
void SpotShadow::testIntersection(const Vector2* poly1, int poly1Length,
        const Vector2* poly2, int poly2Length,
        const Vector2* intersection, int intersectionLength) {
    // Find the min and max of x and y.
    Vector2 lowerBound(FLT_MAX, FLT_MAX);
    Vector2 upperBound(-FLT_MAX, -FLT_MAX);
    for (int i = 0; i < poly1Length; i++) {
        updateBound(poly1[i], lowerBound, upperBound);
    }
    for (int i = 0; i < poly2Length; i++) {
        updateBound(poly2[i], lowerBound, upperBound);
    }

    bool dumpPoly = false;
    for (int k = 0; k < TEST_POINT_NUMBER; k++) {
        // Generate a random point between minX, minY and maxX, maxY.
        double randomX = rand() / double(RAND_MAX);
        double randomY = rand() / double(RAND_MAX);

        Vector2 testPoint;
        testPoint.x = lowerBound.x + randomX * (upperBound.x - lowerBound.x);
        testPoint.y = lowerBound.y + randomY * (upperBound.y - lowerBound.y);

        // If the random point is in both poly 1 and 2, then it must be intersection.
        if (testPointInsidePolygon(testPoint, intersection, intersectionLength)) {
            if (!testPointInsidePolygon(testPoint, poly1, poly1Length)) {
                dumpPoly = true;
                ALOGE("(Error Type 1): one point (%f, %f) in the intersection is"
                      " not in the poly1",
                        testPoint.x, testPoint.y);
            }

            if (!testPointInsidePolygon(testPoint, poly2, poly2Length)) {
                dumpPoly = true;
                ALOGE("(Error Type 1): one point (%f, %f) in the intersection is"
                      " not in the poly2",
                        testPoint.x, testPoint.y);
            }
        }
    }

    if (dumpPoly) {
        dumpPolygon(intersection, intersectionLength, "intersection");
        for (int i = 1; i < intersectionLength; i++) {
            Vector2 delta = intersection[i] - intersection[i - 1];
            ALOGD("Intersetion i, %d Vs i-1 is delta %f", i, delta.lengthSquared());
        }

        dumpPolygon(poly1, poly1Length, "poly 1");
        dumpPolygon(poly2, poly2Length, "poly 2");
    }
}
#endif

}; // namespace uirenderer
}; // namespace android




