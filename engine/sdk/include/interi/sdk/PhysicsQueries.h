// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The `service.PhysicsQueries` contract: scene queries every Physics Engine provides, so a physics-agnostic module
// depends on this header and the contract id rather than on a particular engine. The selected engine provides the
// contract as a hook of signature `PhysicsQueriesService` (it returns the engine's IPhysicsQueries; null while the
// engine has no world), e.g.
//   auto find = hooks.Find<sdk::PhysicsQueriesService>(sdk::kPhysicsQueriesContract);
//   if (const sdk::IPhysicsQueries* queries = find()) { queries->Raycast(...); }
namespace interi::sdk
{

inline constexpr std::string_view kPhysicsQueriesContract = "service.PhysicsQueries";

/// Bit set of the engine's collision layers a query considers (bit i = the engine's layer i).
using PhysicsLayerMask = std::uint32_t;
inline constexpr PhysicsLayerMask kAllPhysicsLayers = ~PhysicsLayerMask{0};

struct PhysicsHit
{
    /// The engine's id for what was hit: for an ECS-synced body the entity's handle bits, 0 for none.
    std::uint64_t object = 0;
    Vec3 point;         ///< World space, on the hit object's surface.
    Vec3 normal;        ///< Unit surface normal at `point`.
    float fraction = 0; ///< Of the ray or sweep: hit position = start + fraction * displacement.
};

/// Queries may run on any thread and in parallel with each other, but not while the engine steps.
class IPhysicsQueries
{
public:
    IPhysicsQueries() = default;
    IPhysicsQueries(const IPhysicsQueries&) = delete;
    IPhysicsQueries& operator=(const IPhysicsQueries&) = delete;
    IPhysicsQueries(IPhysicsQueries&&) = delete;
    IPhysicsQueries& operator=(IPhysicsQueries&&) = delete;
    virtual ~IPhysicsQueries() = default;

    /// Nearest hit by the segment from `origin` to `origin + displacement`.
    [[nodiscard]] virtual std::optional<PhysicsHit> Raycast(const Vec3& origin, const Vec3& displacement,
                                                            PhysicsLayerMask layers) const = 0;
    /// Nearest hit by a sphere swept from `start` to `start + displacement`.
    [[nodiscard]] virtual std::optional<PhysicsHit>
    SphereCast(const Vec3& start, float radius, const Vec3& displacement, PhysicsLayerMask layers) const = 0;
    /// Everything the sphere intersects, once each, in an order fixed by the engine. Replaces `out`.
    virtual void OverlapSphere(const Vec3& center, float radius, std::vector<PhysicsHit>& out,
                               PhysicsLayerMask layers) const = 0;
};

/// The signature of the `service.PhysicsQueries` hook.
using PhysicsQueriesService = const IPhysicsQueries*();

} // namespace interi::sdk
