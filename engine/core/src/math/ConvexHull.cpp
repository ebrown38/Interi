// SPDX-License-Identifier: MIT
#include <interi/core/containers/HashMap.h>
#include <interi/core/math/ConvexHull.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>
#include <utility>

namespace interi
{

namespace
{
constexpr std::uint32_t kNone = 0xFFFF'FFFFu;
/// Clouds thinner than this many epsilons (measured from the plane of the first three simplex points) are rejected
/// as degenerate; see FindInitialSimplex.
constexpr double kMinThicknessInEpsilons = 1000.0;

struct DVec3
{
    double x;
    double y;
    double z;
};

DVec3 ToDouble(const Vec3& v)
{
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}
DVec3 Sub(const DVec3& a, const DVec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
DVec3 CrossD(const DVec3& a, const DVec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double DotD(const DVec3& a, const DVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double LengthD(const DVec3& v)
{
    return std::sqrt(DotD(v, v));
}
double Component(const DVec3& v, std::size_t axis)
{
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

/// Plane in double precision; a zero normal (degenerate triangle) gives distance 0 everywhere.
struct DPlane
{
    DVec3 normal{0, 0, 0};
    double d = 0.0;

    [[nodiscard]] double Distance(const DVec3& p) const { return DotD(normal, p) + d; }
};

DPlane PlaneThrough(const DVec3& a, const DVec3& b, const DVec3& c)
{
    DVec3 n = CrossD(Sub(b, a), Sub(c, a));
    const double length = LengthD(n);
    if (length == 0.0)
    {
        return {};
    }
    n = {n.x / length, n.y / length, n.z / length};
    return {.normal = n, .d = -DotD(n, a)};
}

std::uint64_t EdgeKey(std::uint32_t from, std::uint32_t to)
{
    return (static_cast<std::uint64_t>(from) << 32) | to;
}

/// Quickhull over a fixed point set. Produces triangles indexing the input points.
class QuickHull
{
public:
    /// Works on points translated by -centre (exact in double), so precision does not depend on where the cloud
    /// sits in the world.
    QuickHull(std::span<const Vec3> points, const DVec3& centre, double epsilon)
        : m_points(points.size())
        , m_epsilon(epsilon)
    {
        std::ranges::transform(points, m_points.begin(), [&](const Vec3& p) { return Sub(ToDouble(p), centre); });
    }

    /// Returns false if the points do not span 3D.
    bool Build()
    {
        std::array<std::uint32_t, 4> simplex{};
        if (!FindInitialSimplex(simplex))
        {
            return false;
        }
        CreateSimplexFaces(simplex);

        std::vector<std::uint32_t> unassigned;
        for (std::uint32_t i = 0; i < m_points.size(); ++i)
        {
            if (std::ranges::find(simplex, i) == simplex.end())
            {
                unassigned.push_back(i);
            }
        }
        AssignToFaces(unassigned, {0, 1, 2, 3});

        std::vector<std::uint32_t> work{0, 1, 2, 3};
        while (!work.empty())
        {
            const std::uint32_t faceIndex = work.back();
            work.pop_back();
            if (!m_faces[faceIndex].alive || m_faces[faceIndex].outside.empty())
            {
                continue;
            }
            for (const std::uint32_t created : AddPoint(faceIndex))
            {
                work.push_back(created);
            }
        }
        return true;
    }

    /// Alive faces as triangles over input point indices.
    [[nodiscard]] std::vector<std::array<std::uint32_t, 3>> Triangles() const
    {
        std::vector<std::array<std::uint32_t, 3>> triangles;
        for (const Face& face : m_faces)
        {
            if (face.alive)
            {
                triangles.push_back(face.vertices);
            }
        }
        return triangles;
    }

private:
    struct Face
    {
        std::array<std::uint32_t, 3> vertices;
        DPlane plane;
        std::vector<std::uint32_t> outside; ///< Points in front of this face, not yet on the hull.
        bool alive = true;
    };

    bool FindInitialSimplex(std::array<std::uint32_t, 4>& simplex) const
    {
        // Extreme points along each axis; the farthest-apart pair seeds the simplex.
        std::array<std::uint32_t, 3> minIndex{};
        std::array<std::uint32_t, 3> maxIndex{};
        for (std::uint32_t i = 0; i < m_points.size(); ++i)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                if (Component(m_points[i], axis) < Component(m_points[minIndex[axis]], axis))
                {
                    minIndex[axis] = i;
                }
                if (Component(m_points[i], axis) > Component(m_points[maxIndex[axis]], axis))
                {
                    maxIndex[axis] = i;
                }
            }
        }
        double best = 0.0;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const double distance = LengthD(Sub(m_points[maxIndex[axis]], m_points[minIndex[axis]]));
            if (distance > best)
            {
                best = distance;
                simplex[0] = minIndex[axis];
                simplex[1] = maxIndex[axis];
            }
        }
        if (best <= m_epsilon)
        {
            return false; // all points coincide
        }

        // Farthest from the line through the first two.
        const DVec3 a = m_points[simplex[0]];
        const DVec3 direction = Sub(m_points[simplex[1]], a);
        best = 0.0;
        for (std::uint32_t i = 0; i < m_points.size(); ++i)
        {
            const double distance = LengthD(CrossD(Sub(m_points[i], a), direction)) / LengthD(direction);
            if (distance > best)
            {
                best = distance;
                simplex[2] = i;
            }
        }
        if (best <= m_epsilon)
        {
            return false; // collinear
        }

        // Farthest from the plane of the first three.
        const DPlane plane = PlaneThrough(a, m_points[simplex[1]], m_points[simplex[2]]);
        best = 0.0;
        for (std::uint32_t i = 0; i < m_points.size(); ++i)
        {
            const double distance = std::abs(plane.Distance(m_points[i]));
            if (distance > best)
            {
                best = distance;
                simplex[3] = i;
            }
        }
        // Too flat: where two nearly parallel faces meet at a knife edge, a point within epsilon of both planes can
        // lie far outside the hull. Measured on random slabs, a thickness of ~1400 epsilon keeps every input point
        // within one epsilon of the hull while ~70 epsilon misses by hundreds of epsilon.
        return best > kMinThicknessInEpsilons * m_epsilon;
    }

    void CreateSimplexFaces(const std::array<std::uint32_t, 4>& s)
    {
        // Each face is oriented so the simplex vertex not on it lies behind it.
        const std::array<std::array<std::uint32_t, 4>, 4> faces{
            {{s[0], s[1], s[2], s[3]}, {s[0], s[1], s[3], s[2]}, {s[0], s[2], s[3], s[1]}, {s[1], s[2], s[3], s[0]}}};
        for (const auto& [a, b, c, opposite] : faces)
        {
            const DPlane plane = PlaneThrough(m_points[a], m_points[b], m_points[c]);
            if (plane.Distance(m_points[opposite]) > 0.0)
            {
                AddFace(a, c, b);
            }
            else
            {
                AddFace(a, b, c);
            }
        }
    }

    std::uint32_t AddFace(std::uint32_t a, std::uint32_t b, std::uint32_t c)
    {
        const auto index = static_cast<std::uint32_t>(m_faces.size());
        m_faces.push_back(Face{.vertices = {a, b, c},
                               .plane = PlaneThrough(m_points[a], m_points[b], m_points[c]),
                               .outside = {},
                               .alive = true});
        m_edges[EdgeKey(a, b)] = index;
        m_edges[EdgeKey(b, c)] = index;
        m_edges[EdgeKey(c, a)] = index;
        return index;
    }

    void RemoveFace(std::uint32_t index)
    {
        Face& face = m_faces[index];
        face.alive = false;
        for (std::size_t e = 0; e < 3; ++e)
        {
            m_edges.Erase(EdgeKey(face.vertices[e], face.vertices[(e + 1) % 3]));
        }
    }

    /// Gives each point to the candidate face it is farthest (more than epsilon) in front of; points behind all of
    /// them are inside the new hull and are dropped.
    void AssignToFaces(const std::vector<std::uint32_t>& points, const std::vector<std::uint32_t>& candidates)
    {
        for (const std::uint32_t point : points)
        {
            double bestDistance = m_epsilon;
            std::uint32_t bestFace = kNone;
            for (const std::uint32_t faceIndex : candidates)
            {
                const double distance = m_faces[faceIndex].plane.Distance(m_points[point]);
                if (distance > bestDistance)
                {
                    bestDistance = distance;
                    bestFace = faceIndex;
                }
            }
            if (bestFace != kNone)
            {
                m_faces[bestFace].outside.push_back(point);
            }
        }
    }

    /// Adds the farthest outside point of a face to the hull. Returns the new faces.
    std::vector<std::uint32_t> AddPoint(std::uint32_t startFace)
    {
        const Face& start = m_faces[startFace];
        const std::uint32_t eye = *std::ranges::max_element(start.outside, {}, [&](std::uint32_t p)
                                                            { return start.plane.Distance(m_points[p]); });
        const DVec3 eyePoint = m_points[eye];

        // Flood-fill the faces the eye can see; edges to faces it cannot see form the horizon.
        enum : std::uint8_t
        {
            Unknown,
            Visible,
            Hidden
        };
        std::vector<std::uint8_t> state(m_faces.size(), Unknown);
        std::vector<std::uint32_t> visible{startFace};
        std::vector<std::pair<std::uint32_t, std::uint32_t>> horizon;
        state[startFace] = Visible;
        for (std::size_t next = 0; next < visible.size(); ++next)
        {
            const std::array<std::uint32_t, 3> vertices = m_faces[visible[next]].vertices;
            for (std::size_t e = 0; e < 3; ++e)
            {
                const std::uint32_t from = vertices[e];
                const std::uint32_t to = vertices[(e + 1) % 3];
                const std::uint32_t* neighbour = m_edges.Find(EdgeKey(to, from));
                INTERI_ASSERT(neighbour != nullptr, "hull is not closed");
                if (state[*neighbour] == Unknown)
                {
                    // Exact sign, no tolerance: a tolerant test would let the eye stay "hidden" from a face it is
                    // slightly in front of, leaving a concavity that sliver faces amplify far from the eye.
                    // Coplanar results are merged afterwards instead.
                    const bool sees = m_faces[*neighbour].plane.Distance(eyePoint) > 0.0;
                    state[*neighbour] = sees ? Visible : Hidden;
                    if (sees)
                    {
                        visible.push_back(*neighbour);
                    }
                }
                if (state[*neighbour] == Hidden)
                {
                    horizon.emplace_back(from, to);
                }
            }
        }

        std::vector<std::uint32_t> orphans;
        for (const std::uint32_t faceIndex : visible)
        {
            for (const std::uint32_t point : m_faces[faceIndex].outside)
            {
                if (point != eye)
                {
                    orphans.push_back(point);
                }
            }
            m_faces[faceIndex].outside.clear();
            RemoveFace(faceIndex);
        }

        std::vector<std::uint32_t> created;
        created.reserve(horizon.size());
        for (const auto& [from, to] : horizon)
        {
            created.push_back(AddFace(from, to, eye));
        }
        AssignToFaces(orphans, created);
        return created;
    }

    std::vector<DVec3> m_points;
    double m_epsilon;
    std::vector<Face> m_faces;
    HashMap<std::uint64_t, std::uint32_t> m_edges; ///< Directed edge -> face that owns it.
};

/// Union-find root with path halving.
std::uint32_t FindRoot(std::vector<std::uint32_t>& parent, std::uint32_t i)
{
    while (parent[i] != i)
    {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

/// Merges triangles that share an edge and are coplanar within epsilon into faces.
std::vector<HullFace> MergeCoplanarTriangles(const std::vector<Vec3>& vertices,
                                             const std::vector<std::array<std::uint32_t, 3>>& triangles,
                                             const DVec3& centre, double epsilon)
{
    std::vector<DVec3> local(vertices.size());
    std::ranges::transform(vertices, local.begin(), [&](const Vec3& p) { return Sub(ToDouble(p), centre); });
    const auto triangleCount = static_cast<std::uint32_t>(triangles.size());
    std::vector<DPlane> planes(triangleCount);
    HashMap<std::uint64_t, std::uint32_t> edgeOwner;
    for (std::uint32_t t = 0; t < triangleCount; ++t)
    {
        const auto& tri = triangles[t];
        planes[t] = PlaneThrough(local[tri[0]], local[tri[1]], local[tri[2]]);
        for (std::size_t e = 0; e < 3; ++e)
        {
            edgeOwner.Insert(EdgeKey(tri[e], tri[(e + 1) % 3]), t);
        }
    }

    std::vector<std::uint32_t> parent(triangleCount);
    std::ranges::iota(parent, 0u);
    for (std::uint32_t t = 0; t < triangleCount; ++t)
    {
        const auto& tri = triangles[t];
        for (std::size_t e = 0; e < 3; ++e)
        {
            const std::uint32_t* neighbour = edgeOwner.Find(EdgeKey(tri[(e + 1) % 3], tri[e]));
            if (neighbour == nullptr || *neighbour < t)
            {
                continue;
            }
            // Coplanar if each triangle's vertices lie within epsilon of the other's plane.
            bool coplanar = DotD(planes[t].normal, planes[*neighbour].normal) > 0.0;
            for (const std::uint32_t v : triangles[*neighbour])
            {
                coplanar = coplanar && std::abs(planes[t].Distance(local[v])) <= epsilon;
            }
            for (const std::uint32_t v : tri)
            {
                coplanar = coplanar && std::abs(planes[*neighbour].Distance(local[v])) <= epsilon;
            }
            if (coplanar)
            {
                parent[FindRoot(parent, t)] = FindRoot(parent, *neighbour);
            }
        }
    }

    std::vector<HullFace> faces;
    std::vector<std::uint32_t> faceOfRoot(triangleCount, kNone);
    std::vector<DVec3> normalSums;
    for (std::uint32_t t = 0; t < triangleCount; ++t)
    {
        const std::uint32_t root = FindRoot(parent, t);
        if (faceOfRoot[root] == kNone)
        {
            faceOfRoot[root] = static_cast<std::uint32_t>(faces.size());
            faces.emplace_back();
            normalSums.push_back({0, 0, 0});
        }
        const std::uint32_t face = faceOfRoot[root];
        faces[face].triangles.push_back(t);
        // Area-weighted normal: large triangles dominate slivers.
        const auto& tri = triangles[t];
        const DVec3& a = local[tri[0]];
        const DVec3 n = CrossD(Sub(local[tri[1]], a), Sub(local[tri[2]], a));
        normalSums[face] = {normalSums[face].x + n.x, normalSums[face].y + n.y, normalSums[face].z + n.z};
    }
    for (std::size_t f = 0; f < faces.size(); ++f)
    {
        const DVec3& n = normalSums[f];
        const double length = LengthD(n);
        const Vec3 normal{static_cast<float>(n.x / length), static_cast<float>(n.y / length),
                          static_cast<float>(n.z / length)};
        const Vec3& onPlane = vertices[triangles[faces[f].triangles.front()][0]];
        faces[f].plane = Plane{.normal = normal, .d = -Dot(normal, onPlane)};
    }
    return faces;
}

/// Runs Quickhull and compacts the result to the vertices actually used.
bool BuildOnce(std::span<const Vec3> points, const DVec3& centre, double epsilon, ConvexHull& hull)
{
    QuickHull quickHull(points, centre, epsilon);
    if (!quickHull.Build())
    {
        return false;
    }
    std::vector<std::uint32_t> remap(points.size(), kNone);
    hull.vertices.clear();
    hull.triangles = quickHull.Triangles();
    for (auto& tri : hull.triangles)
    {
        for (std::uint32_t& v : tri)
        {
            if (remap[v] == kNone)
            {
                remap[v] = static_cast<std::uint32_t>(hull.vertices.size());
                hull.vertices.push_back(points[v]);
            }
            v = remap[v];
        }
    }
    hull.faces = MergeCoplanarTriangles(hull.vertices, hull.triangles, centre, epsilon);
    return true;
}
} // namespace

Result<ConvexHull> BuildConvexHull(std::span<const Vec3> points)
{
    if (points.size() < 4)
    {
        return MakeError(ErrorCode::Degenerate, "a 3D convex hull needs at least 4 points");
    }
    // Tolerance from the coordinate magnitudes (as in Quickhull implementations): float input carries this much
    // rounding, so smaller distances are indistinguishable from coplanar.
    // Tolerance as in Quickhull implementations (3 * machine epsilon * coordinate magnitude), measured on the
    // cloud centred on its bounding box so it scales with the cloud's extent, not its distance from the origin.
    const AABB bounds = AABB::FromPoints(points);
    const Vec3 centreF = bounds.Center();
    const DVec3 centre = ToDouble(centreF);
    const Vec3 halfExtent = bounds.Extents();
    const double epsilon =
        3.0 * static_cast<double>(FLT_EPSILON) *
        (static_cast<double>(halfExtent.x) + static_cast<double>(halfExtent.y) + static_cast<double>(halfExtent.z));

    ConvexHull hull;
    hull.epsilon = static_cast<float>(epsilon);
    if (!BuildOnce(points, centre, epsilon, hull))
    {
        return MakeError(ErrorCode::Degenerate, "points are coincident, collinear or coplanar");
    }

    // Quickhull can keep points that lie on a face or an edge of the final hull when they were picked before the
    // true corners. A corner touches at least 3 merged faces; rebuild from the corners alone until stable.
    for (int pass = 0; pass < 3; ++pass)
    {
        std::vector<std::vector<std::uint32_t>> facesOfVertex(hull.vertices.size());
        for (std::uint32_t f = 0; f < hull.faces.size(); ++f)
        {
            for (const std::uint32_t t : hull.faces[f].triangles)
            {
                for (const std::uint32_t v : hull.triangles[t])
                {
                    if (std::ranges::find(facesOfVertex[v], f) == facesOfVertex[v].end())
                    {
                        facesOfVertex[v].push_back(f);
                    }
                }
            }
        }
        std::vector<Vec3> corners;
        for (std::size_t v = 0; v < hull.vertices.size(); ++v)
        {
            if (facesOfVertex[v].size() >= 3)
            {
                corners.push_back(hull.vertices[v]);
            }
        }
        if (corners.size() == hull.vertices.size())
        {
            break;
        }
        ConvexHull rebuilt;
        rebuilt.epsilon = hull.epsilon;
        if (!BuildOnce(corners, centre, epsilon, rebuilt))
        {
            break; // keep the valid (if redundant) hull
        }
        hull = std::move(rebuilt);
    }
    return hull;
}

} // namespace interi
