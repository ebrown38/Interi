// SPDX-License-Identifier: MIT
#include <interi/scene/Primitives.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace interi::scene
{

namespace
{

constexpr std::array<std::string_view, kPrimitiveShapeCount> kShapeNames = {
    "Box", "Sphere", "Cylinder", "Cone", "Capsule", "Plane", "Wedge", "Stairs", "Torus",
};

constexpr std::string_view kBoxSlots[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
constexpr std::string_view kSurfaceSlots[] = {"Surface"};
constexpr std::string_view kCylinderSlots[] = {"Side", "Top", "Bottom"};
constexpr std::string_view kConeSlots[] = {"Side", "Base"};
constexpr std::string_view kWedgeSlots[] = {"Slope", "Sides", "Back", "Bottom"};
constexpr std::string_view kStairsSlots[] = {"Steps", "Sides", "Back", "Bottom"};

constexpr std::uint32_t kMaxDivisions = 1024;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

/// Builds a mesh from welded positions. Triangles are oriented so their winding agrees with the corner normals.
class Builder
{
public:
    std::uint32_t Add(const Vec3& position)
    {
        m_mesh.positions.push_back(position);
        return static_cast<std::uint32_t>(m_mesh.positions.size() - 1);
    }

    void Triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c, Vec3 na, Vec3 nb, Vec3 nc, std::uint32_t slot)
    {
        const std::vector<Vec3>& p = m_mesh.positions;
        if (Dot(Cross(p[b] - p[a], p[c] - p[a]), na + nb + nc) < 0.0f)
        {
            std::swap(b, c);
            std::swap(nb, nc);
        }
        m_mesh.indices.insert(m_mesh.indices.end(), {a, b, c});
        m_mesh.normals.insert(m_mesh.normals.end(), {na, nb, nc});
        m_mesh.slots.push_back(slot);
    }

    void Flat(std::uint32_t a, std::uint32_t b, std::uint32_t c, const Vec3& normal, std::uint32_t slot)
    {
        Triangle(a, b, c, normal, normal, normal, slot);
    }

    /// A planar quad a-b-c-d (in order around its edge).
    void Quad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, const Vec3& normal,
              std::uint32_t slot)
    {
        Flat(a, b, c, normal, slot);
        Flat(a, c, d, normal, slot);
    }

    [[nodiscard]] const Vec3& Position(std::uint32_t index) const { return m_mesh.positions[index]; }
    [[nodiscard]] TriangleMesh Take() { return std::move(m_mesh); }

private:
    TriangleMesh m_mesh;
};

[[nodiscard]] Vec3 OnCircle(float angle, float radius, float y)
{
    return {radius * std::cos(angle), y, -radius * std::sin(angle)};
}

[[nodiscard]] float Angle(std::uint32_t index, std::uint32_t count)
{
    return kTwoPi * static_cast<float>(index % count) / static_cast<float>(count);
}

TriangleMesh MakeBox(const Vec3& size)
{
    Builder builder;
    const Vec3 h = size * 0.5f;
    std::array<std::uint32_t, 8> corner{};
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        corner[i] = builder.Add({(i & 1u) != 0 ? h.x : -h.x, (i & 2u) != 0 ? h.y : -h.y, (i & 4u) != 0 ? h.z : -h.z});
    }
    // Corner index bits: 1 = +x, 2 = +y, 4 = +z.
    builder.Quad(corner[1], corner[3], corner[7], corner[5], {1.0f, 0.0f, 0.0f}, 0);
    builder.Quad(corner[0], corner[4], corner[6], corner[2], {-1.0f, 0.0f, 0.0f}, 1);
    builder.Quad(corner[2], corner[6], corner[7], corner[3], {0.0f, 1.0f, 0.0f}, 2);
    builder.Quad(corner[0], corner[1], corner[5], corner[4], {0.0f, -1.0f, 0.0f}, 3);
    builder.Quad(corner[4], corner[5], corner[7], corner[6], {0.0f, 0.0f, 1.0f}, 4);
    builder.Quad(corner[0], corner[2], corner[3], corner[1], {0.0f, 0.0f, -1.0f}, 5);
    return builder.Take();
}

TriangleMesh MakePlane(const Vec3& size)
{
    Builder builder;
    const float x = size.x * 0.5f;
    const float z = size.z * 0.5f;
    const std::uint32_t a = builder.Add({-x, 0.0f, -z});
    const std::uint32_t b = builder.Add({x, 0.0f, -z});
    const std::uint32_t c = builder.Add({x, 0.0f, z});
    const std::uint32_t d = builder.Add({-x, 0.0f, z});
    builder.Quad(a, b, c, d, {0.0f, 1.0f, 0.0f}, 0);
    return builder.Take();
}

/// Rings of `segments` vertices (ring r at heights[r] with radii[r]) between optional single-vertex poles, joined
/// by bands with smooth normals from `normal(position, ring)`. Pole vertices are added when the first or last
/// radius is zero.
template <typename NormalFn>
void Revolve(Builder& builder, std::span<const float> heights, std::span<const float> radii, std::uint32_t segments,
             std::uint32_t slot, NormalFn&& normal)
{
    std::vector<std::vector<std::uint32_t>> rings;
    for (std::size_t r = 0; r < heights.size(); ++r)
    {
        std::vector<std::uint32_t> ring;
        if (radii[r] == 0.0f)
        {
            ring.assign(segments, builder.Add({0.0f, heights[r], 0.0f}));
        }
        else
        {
            for (std::uint32_t s = 0; s < segments; ++s)
            {
                ring.push_back(builder.Add(OnCircle(Angle(s, segments), radii[r], heights[r])));
            }
        }
        rings.push_back(std::move(ring));
    }
    for (std::size_t r = 0; r + 1 < rings.size(); ++r)
    {
        const bool topPole = radii[r] == 0.0f;
        const bool bottomPole = radii[r + 1] == 0.0f;
        for (std::uint32_t s = 0; s < segments; ++s)
        {
            const std::uint32_t next = (s + 1) % segments;
            const std::uint32_t a = rings[r][s];
            const std::uint32_t b = rings[r][next];
            const std::uint32_t c = rings[r + 1][next];
            const std::uint32_t d = rings[r + 1][s];
            const float mid = (Angle(s, segments) + Angle(s, segments) + kTwoPi / static_cast<float>(segments)) * 0.5f;
            const auto at = [&](std::uint32_t vertex, std::size_t ring, float poleAngle)
            {
                const Vec3& p = builder.Position(vertex);
                return normal(radii[ring] == 0.0f ? OnCircle(poleAngle, 1e-3f, p.y) : p, ring);
            };
            if (!topPole)
            {
                builder.Triangle(a, d, b, at(a, r, mid), at(d, r + 1, mid), at(b, r, mid), slot);
            }
            if (!bottomPole)
            {
                builder.Triangle(b, d, c, at(b, r, mid), at(d, r + 1, mid), at(c, r + 1, mid), slot);
            }
        }
    }
}

/// A flat disc closing a ring of the mesh (the ring's vertices are re-found by position order: `first` is the
/// index of its first vertex, the ring is contiguous).
void Cap(Builder& builder, std::uint32_t first, std::uint32_t segments, float y, float facing, std::uint32_t slot)
{
    const std::uint32_t centre = builder.Add({0.0f, y, 0.0f});
    const Vec3 normal{0.0f, facing, 0.0f};
    for (std::uint32_t s = 0; s < segments; ++s)
    {
        builder.Flat(centre, first + s, first + (s + 1) % segments, normal, slot);
    }
}

TriangleMesh MakeSphere(float radius, std::uint32_t segments, std::uint32_t rings)
{
    std::vector<float> heights;
    std::vector<float> radii;
    for (std::uint32_t r = 0; r <= rings; ++r)
    {
        const float polar = std::numbers::pi_v<float> * static_cast<float>(r) / static_cast<float>(rings);
        heights.push_back(r == 0 ? radius : r == rings ? -radius : radius * std::cos(polar));
        radii.push_back(r == 0 || r == rings ? 0.0f : radius * std::sin(polar));
    }
    Builder builder;
    Revolve(builder, heights, radii, segments, 0, [](const Vec3& p, std::size_t) { return Normalize(p); });
    return builder.Take();
}

TriangleMesh MakeCylinder(float radius, float height, std::uint32_t segments)
{
    const float h = height * 0.5f;
    const float heights[] = {h, -h};
    const float radii[] = {radius, radius};
    Builder builder;
    Revolve(builder, heights, radii, segments, 0,
            [](const Vec3& p, std::size_t) { return Normalize(Vec3{p.x, 0.0f, p.z}); });
    Cap(builder, 0, segments, h, 1.0f, 1);
    Cap(builder, segments, segments, -h, -1.0f, 2);
    return builder.Take();
}

TriangleMesh MakeCone(float radius, float height, std::uint32_t segments)
{
    const float h = height * 0.5f;
    const float heights[] = {h, -h};
    const float radii[] = {0.0f, radius};
    Builder builder;
    // The side's normal leans up by the slope: (radial * height, radius) normalised.
    Revolve(builder, heights, radii, segments, 0,
            [&](const Vec3& p, std::size_t)
            {
                const Vec3 radial = Normalize(Vec3{p.x, 0.0f, p.z});
                return Normalize(Vec3{radial.x * height, radius, radial.z * height});
            });
    Cap(builder, 1, segments, -h, -1.0f, 1); // the apex is vertex 0
    return builder.Take();
}

TriangleMesh MakeCapsule(float radius, float height, std::uint32_t segments, std::uint32_t rings)
{
    const float c = height * 0.5f - radius; // half the straight part
    std::vector<float> heights;
    std::vector<float> radii;
    for (std::uint32_t r = 0; r <= rings; ++r) // top hemisphere: pole to equator
    {
        const float polar = std::numbers::pi_v<float> * 0.5f * static_cast<float>(r) / static_cast<float>(rings);
        heights.push_back(c + (r == 0 ? radius : r == rings ? 0.0f : radius * std::cos(polar)));
        radii.push_back(r == 0 ? 0.0f : r == rings ? radius : radius * std::sin(polar));
    }
    for (std::uint32_t r = rings; r <= 2 * rings; ++r) // bottom hemisphere: equator to pole
    {
        const float polar = std::numbers::pi_v<float> * 0.5f * static_cast<float>(r) / static_cast<float>(rings);
        heights.push_back(-c + (r == rings ? 0.0f : r == 2 * rings ? -radius : radius * std::cos(polar)));
        radii.push_back(r == 2 * rings ? 0.0f : r == rings ? radius : radius * std::sin(polar));
    }
    Builder builder;
    Revolve(builder, heights, radii, segments, 0,
            [&](const Vec3& p, std::size_t ring)
            {
                const float centre = ring <= rings ? c : -c;
                return Normalize(Vec3{p.x, p.y - centre, p.z});
            });
    return builder.Take();
}

TriangleMesh MakeTorus(float radius, float tube, std::uint32_t segments, std::uint32_t rings)
{
    Builder builder;
    // Vertex (s, t): s around the ring, t around the tube.
    for (std::uint32_t s = 0; s < segments; ++s)
    {
        for (std::uint32_t t = 0; t < rings; ++t)
        {
            const float around = Angle(t, rings);
            const float distance = radius + tube * std::cos(around);
            (void)builder.Add(OnCircle(Angle(s, segments), distance, tube * std::sin(around)));
        }
    }
    const auto index = [&](std::uint32_t s, std::uint32_t t) { return (s % segments) * rings + t % rings; };
    const auto normal = [&](std::uint32_t vertex)
    {
        const Vec3& p = builder.Position(vertex);
        const Vec3 centre = Normalize(Vec3{p.x, 0.0f, p.z}) * radius;
        return Normalize(p - centre);
    };
    for (std::uint32_t s = 0; s < segments; ++s)
    {
        for (std::uint32_t t = 0; t < rings; ++t)
        {
            const std::uint32_t a = index(s, t);
            const std::uint32_t b = index(s + 1, t);
            const std::uint32_t c = index(s + 1, t + 1);
            const std::uint32_t d = index(s, t + 1);
            builder.Triangle(a, b, c, normal(a), normal(b), normal(c), 0);
            builder.Triangle(a, c, d, normal(a), normal(c), normal(d), 0);
        }
    }
    return builder.Take();
}

/// A triangular prism: full bottom, a vertical back at -z, and a slope from the front bottom edge (+z) to the top
/// back edge.
TriangleMesh MakeWedge(const Vec3& size)
{
    Builder builder;
    const Vec3 h = size * 0.5f;
    std::array<std::uint32_t, 6> v{};
    for (std::uint32_t side = 0; side < 2; ++side)
    {
        const float x = side == 0 ? -h.x : h.x;
        v[side * 3 + 0] = builder.Add({x, -h.y, h.z});  // front bottom
        v[side * 3 + 1] = builder.Add({x, -h.y, -h.z}); // back bottom
        v[side * 3 + 2] = builder.Add({x, h.y, -h.z});  // back top
    }
    builder.Quad(v[0], v[3], v[5], v[2], Normalize(Vec3{0.0f, size.z, size.y}), 0);
    builder.Flat(v[0], v[1], v[2], {-1.0f, 0.0f, 0.0f}, 1);
    builder.Flat(v[3], v[4], v[5], {1.0f, 0.0f, 0.0f}, 1);
    builder.Quad(v[1], v[4], v[5], v[2], {0.0f, 0.0f, -1.0f}, 2);
    builder.Quad(v[0], v[3], v[4], v[1], {0.0f, -1.0f, 0.0f}, 3);
    return builder.Take();
}

/// `steps` steps rising towards -z: step i spans depth [i, i + 1] * size.z / steps from the front (+z) and its tread
/// is (i + 1) * size.y / steps above the bottom. The side profile is cut into one column per step, each a fan from
/// its top corner, so the sides share every edge with the treads, risers, back and bottom.
TriangleMesh MakeStairs(const Vec3& size, std::uint32_t steps)
{
    Builder builder;
    const Vec3 h = size * 0.5f;
    const auto z = [&](std::uint32_t i) { return h.z - size.z * static_cast<float>(i) / static_cast<float>(steps); };
    const auto y = [&](std::uint32_t i) { return -h.y + size.y * static_cast<float>(i) / static_cast<float>(steps); };

    // Per side: bottom[i] = (z(i), bottom) for i in 0..steps, low[i] = (z(i), y(i)) for i in 1..steps-1 (the riser
    // foot), high[i] = (z(i), y(i + 1)) for i in 0..steps-1 (the tread front), top = (z(steps), top).
    struct Side
    {
        std::vector<std::uint32_t> bottom, low, high;
        std::uint32_t top = 0;
    };
    std::array<Side, 2> sides;
    for (std::uint32_t s = 0; s < 2; ++s)
    {
        const float x = s == 0 ? -h.x : h.x;
        Side& side = sides[s];
        for (std::uint32_t i = 0; i <= steps; ++i)
        {
            side.bottom.push_back(builder.Add({x, -h.y, z(i)}));
        }
        side.low.push_back(side.bottom[0]);
        for (std::uint32_t i = 1; i < steps; ++i)
        {
            side.low.push_back(builder.Add({x, y(i), z(i)}));
        }
        for (std::uint32_t i = 0; i < steps; ++i)
        {
            side.high.push_back(builder.Add({x, y(i + 1), z(i)}));
        }
        side.top = builder.Add({x, h.y, z(steps)});
    }
    const Vec3 up{0.0f, 1.0f, 0.0f};
    const Vec3 front{0.0f, 0.0f, 1.0f};
    for (std::uint32_t s = 0; s < 2; ++s)
    {
        const Side& side = sides[s];
        const Vec3 normal{s == 0 ? -1.0f : 1.0f, 0.0f, 0.0f};
        for (std::uint32_t i = 0; i < steps; ++i)
        {
            // Column i: bottom[i], bottom[i + 1], its top corner (the next step's tread front, or the top), high[i]
            // and, past the first, low[i].
            const std::uint32_t corner = i + 1 < steps ? side.low[i + 1] : side.top;
            builder.Flat(corner, side.high[i], i == 0 ? side.bottom[0] : side.low[i], normal, 1);
            if (i > 0)
            {
                builder.Flat(corner, side.low[i], side.bottom[i], normal, 1);
            }
            builder.Flat(corner, side.bottom[i], side.bottom[i + 1], normal, 1);
        }
    }
    const Side& l = sides[0];
    const Side& r = sides[1];
    for (std::uint32_t i = 0; i < steps; ++i)
    {
        builder.Quad(l.low[i], r.low[i], r.high[i], l.high[i], front, 0); // riser
        const std::uint32_t lBack = i + 1 < steps ? l.low[i + 1] : l.top;
        const std::uint32_t rBack = i + 1 < steps ? r.low[i + 1] : r.top;
        builder.Quad(l.high[i], r.high[i], rBack, lBack, up, 0); // tread
        builder.Quad(l.bottom[i], l.bottom[i + 1], r.bottom[i + 1], r.bottom[i], {0.0f, -1.0f, 0.0f}, 3);
    }
    builder.Quad(l.bottom[steps], r.bottom[steps], r.top, l.top, {0.0f, 0.0f, -1.0f}, 2);
    return builder.Take();
}

Result<void> CheckPositive(float value, std::string_view name)
{
    if (!std::isfinite(value) || value <= 0.0f)
    {
        return Invalid(std::format("primitive: {} must be positive", name));
    }
    return {};
}

Result<void> CheckCount(std::uint32_t value, std::uint32_t minimum, std::string_view name)
{
    if (value < minimum || value > kMaxDivisions)
    {
        return Invalid(std::format("primitive: {} must be in [{}, {}]", name, minimum, kMaxDivisions));
    }
    return {};
}

} // namespace

std::string_view ToString(PrimitiveShape shape) noexcept
{
    return kShapeNames[static_cast<std::size_t>(shape)];
}

std::optional<PrimitiveShape> ParsePrimitiveShape(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kShapeNames, name);
    if (found == kShapeNames.end())
    {
        return std::nullopt;
    }
    return static_cast<PrimitiveShape>(found - kShapeNames.begin());
}

std::vector<std::string> PrimitiveShapeNames()
{
    return {kShapeNames.begin(), kShapeNames.end()};
}

std::span<const std::string_view> PrimitiveSlots(PrimitiveShape shape) noexcept
{
    switch (shape)
    {
    case PrimitiveShape::Box:
        return kBoxSlots;
    case PrimitiveShape::Cylinder:
        return kCylinderSlots;
    case PrimitiveShape::Cone:
        return kConeSlots;
    case PrimitiveShape::Wedge:
        return kWedgeSlots;
    case PrimitiveShape::Stairs:
        return kStairsSlots;
    case PrimitiveShape::Sphere:
    case PrimitiveShape::Capsule:
    case PrimitiveShape::Plane:
    case PrimitiveShape::Torus:
        break;
    }
    return kSurfaceSlots;
}

Vec3 PrimitiveHalfExtents(const PrimitiveParams& params) noexcept
{
    switch (params.shape)
    {
    case PrimitiveShape::Box:
    case PrimitiveShape::Wedge:
    case PrimitiveShape::Stairs:
        return params.size * 0.5f;
    case PrimitiveShape::Plane:
        return {params.size.x * 0.5f, 0.0f, params.size.z * 0.5f};
    case PrimitiveShape::Sphere:
        return Vec3(params.radius);
    case PrimitiveShape::Cylinder:
    case PrimitiveShape::Cone:
    case PrimitiveShape::Capsule:
        return {params.radius, params.height * 0.5f, params.radius};
    case PrimitiveShape::Torus:
        return {params.radius + params.tube, params.tube, params.radius + params.tube};
    }
    return Vec3(0.0f);
}

Result<TriangleMesh> GeneratePrimitive(const PrimitiveParams& params)
{
    const auto size = [&](bool needY) -> Result<void>
    {
        INTERI_TRY(CheckPositive(params.size.x, "size.x"));
        if (needY)
        {
            INTERI_TRY(CheckPositive(params.size.y, "size.y"));
        }
        return CheckPositive(params.size.z, "size.z");
    };
    switch (params.shape)
    {
    case PrimitiveShape::Box:
        INTERI_TRY(size(true));
        return MakeBox(params.size);
    case PrimitiveShape::Plane:
        INTERI_TRY(size(false));
        return MakePlane(params.size);
    case PrimitiveShape::Wedge:
        INTERI_TRY(size(true));
        return MakeWedge(params.size);
    case PrimitiveShape::Stairs:
        INTERI_TRY(size(true));
        INTERI_TRY(CheckCount(params.steps, 1, "steps"));
        return MakeStairs(params.size, params.steps);
    case PrimitiveShape::Sphere:
        INTERI_TRY(CheckPositive(params.radius, "radius"));
        INTERI_TRY(CheckCount(params.segments, 3, "segments"));
        INTERI_TRY(CheckCount(params.rings, 2, "rings"));
        return MakeSphere(params.radius, params.segments, params.rings);
    case PrimitiveShape::Cylinder:
    case PrimitiveShape::Cone:
        INTERI_TRY(CheckPositive(params.radius, "radius"));
        INTERI_TRY(CheckPositive(params.height, "height"));
        INTERI_TRY(CheckCount(params.segments, 3, "segments"));
        return params.shape == PrimitiveShape::Cone ? MakeCone(params.radius, params.height, params.segments)
                                                    : MakeCylinder(params.radius, params.height, params.segments);
    case PrimitiveShape::Capsule:
        INTERI_TRY(CheckPositive(params.radius, "radius"));
        INTERI_TRY(CheckPositive(params.height, "height"));
        INTERI_TRY(CheckCount(params.segments, 3, "segments"));
        INTERI_TRY(CheckCount(params.rings, 1, "rings"));
        if (!(params.height > 2.0f * params.radius))
        {
            return Invalid("primitive: a capsule's height must exceed its diameter");
        }
        return MakeCapsule(params.radius, params.height, params.segments, params.rings);
    case PrimitiveShape::Torus:
        INTERI_TRY(CheckPositive(params.radius, "radius"));
        INTERI_TRY(CheckPositive(params.tube, "tube"));
        INTERI_TRY(CheckCount(params.segments, 3, "segments"));
        INTERI_TRY(CheckCount(params.rings, 3, "rings"));
        if (!(params.tube < params.radius))
        {
            return Invalid("primitive: a torus's tube must be thinner than its radius");
        }
        return MakeTorus(params.radius, params.tube, params.segments, params.rings);
    }
    return Invalid("primitive: unknown shape");
}

Result<PrimitiveParams> ParamsOf(const PrimitiveComponent& component)
{
    const auto shape = ParsePrimitiveShape(component.shape.name);
    if (!shape)
    {
        return Invalid(std::format("primitive: unknown shape '{}'", component.shape.name));
    }
    return PrimitiveParams{.shape = *shape,
                           .size = component.size,
                           .radius = component.radius,
                           .height = component.height,
                           .tube = component.tube,
                           .segments = component.segments,
                           .rings = component.rings,
                           .steps = component.steps};
}

PrimitiveComponent MakePrimitiveComponent(const PrimitiveParams& params)
{
    PrimitiveComponent component;
    component.shape.name = std::string(ToString(params.shape));
    component.size = params.size;
    component.radius = params.radius;
    component.height = params.height;
    component.tube = params.tube;
    component.segments = params.segments;
    component.rings = params.rings;
    component.steps = params.steps;
    return component;
}

void RegisterPrimitiveReflection(values::ReflectionRegistry& registry)
{
    using values::Field;
    values::ValueMeta length;
    length.min = 0.001;
    length.step = 0.05;
    length.unit = "m";
    values::ValueMeta divisions;
    divisions.min = 1.0;
    divisions.max = static_cast<double>(kMaxDivisions);
    divisions.step = 1.0;
    registry.Register<PrimitiveComponent>({
        values::EnumField("shape", &PrimitiveComponent::shape, PrimitiveShapeNames()),
        Field("size", &PrimitiveComponent::size, length),
        Field("radius", &PrimitiveComponent::radius, length),
        Field("height", &PrimitiveComponent::height, length),
        Field("tube", &PrimitiveComponent::tube, length),
        Field("segments", &PrimitiveComponent::segments, divisions),
        Field("rings", &PrimitiveComponent::rings, divisions),
        Field("steps", &PrimitiveComponent::steps, divisions),
        Field("colors", &PrimitiveComponent::colors),
        Field("offset", &PrimitiveComponent::offset),
    });
}

} // namespace interi::scene
