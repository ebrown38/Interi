// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::destruction
{

struct DestructibleAsset;

/// A node of the stress graph: one chunk, loaded at its centroid.
struct StressNode
{
    Vec3 position;     ///< Centroid (m).
    float mass = 0.0f; ///< kg.
    bool anchored = false;
};

/// An elastic connection between two nodes through their shared contact face.
struct StressBond
{
    std::uint32_t nodeA = 0;
    std::uint32_t nodeB = 0;
    Vec3 centroid;                 ///< Centre of the contact face.
    Vec3 normal;                   ///< Unit, from A into B.
    float area = 0.0f;             ///< m^2 (> 0).
    float compressiveLimit = 0.0f; ///< N.
    float tensileLimit = 0.0f;     ///< N.
    float shearLimit = 0.0f;       ///< N.
};

/// Nodes and bonds of a destructible's support structure.
struct StressGraph
{
    std::vector<StressNode> nodes;
    std::vector<StressBond> bonds;
};

/// The stress graph of an asset: node i is chunk i (only leaves have bonds), bond i is asset bond i.
[[nodiscard]] StressGraph MakeStressGraph(const DestructibleAsset& asset);

struct StressSettings
{
    Vec3 gravity{0.0f, -9.81f, 0.0f};
    /// Young's modulus of every bond (Pa). Only stiffness ratios shape the force distribution, so one value suits
    /// all materials; it sets the (tiny, linearised) displacement scale.
    double youngsModulus = 30.0e9;
    double shearModulusRatio = 0.4; ///< G / E.
    /// A solve has converged when |f - K u| <= relativeTolerance * |f| over the free degrees of freedom.
    double relativeTolerance = 1.0e-6;
};

/// Internal forces of one bond (N, N·m), from node A's side of the face.
struct BondForces
{
    float axial = 0.0f;   ///< Along the normal: > 0 tension (B pulled away from A), < 0 compression.
    float shear = 0.0f;   ///< Magnitude of the in-face force.
    float bending = 0.0f; ///< Magnitude of the moment about in-face axes.
    float torsion = 0.0f; ///< Magnitude of the moment about the normal.
};

/// Face-equivalent loads compared against a bond's limits: bending adds 6M/sqrt(A) (a square face's extreme fibre
/// stress times its area) to both tension and compression; torsion adds about 4.8T/sqrt(A) to shear.
struct BondLoad
{
    float compression = 0.0f;
    float tension = 0.0f;
    float shear = 0.0f;
};

[[nodiscard]] BondLoad ToBondLoad(const BondForces& forces, float area) noexcept;

struct SolveResult
{
    std::uint32_t iterations = 0;
    bool converged = false;
    double relativeResidual = 0.0;
};

/// Static stress in a structure under gravity and external loads, modelled as a beam lattice: every node has a
/// small displacement and rotation (6 degrees of freedom), every unbroken bond is an elastic beam between the two
/// nodes through its face centroid (axial EA/L, shear GA/L, bending EI/L, torsion GJ/L for a square face of area A),
/// and anchored nodes are fixed. Solves K u = f with block-Jacobi preconditioned conjugate gradients.
///
/// Only nodes connected to an anchor through unbroken bonds take part (the others are islands, which fall); K is
/// then positive definite. Each Solve continues from the previous solution (warm start).
///
/// All work is split into fixed chunks of bonds and nodes and every sum is combined in a fixed order, so results are
/// bit-identical at any worker count.
class StressSolver
{
public:
    StressSolver(StressGraph graph, const StressSettings& settings = {});

    [[nodiscard]] const StressGraph& Graph() const noexcept { return m_graph; }
    [[nodiscard]] std::size_t NodeCount() const noexcept { return m_graph.nodes.size(); }
    [[nodiscard]] std::size_t BondCount() const noexcept { return m_graph.bonds.size(); }

    /// Removes a bond from the structure (idempotent). Nodes no longer connected to an anchor leave the solve.
    void BreakBond(std::uint32_t bond);
    [[nodiscard]] bool IsBroken(std::uint32_t bond) const noexcept { return m_broken[bond] != 0; }

    /// Takes a node out of the structure for good: breaks all its bonds (a destroyed or released chunk).
    void RemoveNode(std::uint32_t node);

    /// Whether the node is connected to an anchor (or is one) through unbroken bonds.
    [[nodiscard]] bool IsSupported(std::uint32_t node);

    /// Force applied at a node's centroid in addition to its weight (N); persists until changed.
    void SetExternalForce(std::uint32_t node, const Vec3& force);
    void ClearExternalForces();

    /// Runs up to `maxIterations` conjugate-gradient iterations from the current solution.
    SolveResult Solve(JobSystem& jobs, std::uint32_t maxIterations);

    /// Whether the last Solve converged and nothing changed since (bonds, loads).
    [[nodiscard]] bool IsConverged() const noexcept { return m_converged; }

    /// Forgets the solution (the next Solve starts from zero displacement).
    void ResetSolution();

    /// Forces in a bond for the current solution (zero for broken bonds).
    [[nodiscard]] BondForces Forces(std::uint32_t bond) const;

    /// Unbroken bonds whose load exceeds their limits scaled by `limitScale[bond]` (all 1 when empty), ascending.
    /// Replaces the contents of `out`.
    void FindOverloadedBonds(JobSystem& jobs, std::span<const float> limitScale, std::vector<std::uint32_t>& out) const;

    /// Displacement and rotation of a node (zero for anchored and unsupported nodes).
    [[nodiscard]] Vec3 Displacement(std::uint32_t node) const;
    [[nodiscard]] Vec3 Rotation(std::uint32_t node) const;

private:
    struct BondStiffness
    {
        double axial = 0.0;
        double shear = 0.0;
        double bending = 0.0;
        double torsion = 0.0;
    };

    void UpdateSupport();
    void UpdatePreconditioner(JobSystem& jobs);
    void ComputeBondForces(std::size_t bond, std::span<const double> x, std::span<double> out) const;
    void ApplyStiffness(JobSystem& jobs, std::span<const double> x, std::span<double> out);
    void Precondition(JobSystem& jobs, std::span<const double> r, std::span<double> z) const;
    [[nodiscard]] double DotProduct(JobSystem& jobs, std::span<const double> a, std::span<const double> b) const;

    StressGraph m_graph;
    StressSettings m_settings;
    std::vector<BondStiffness> m_stiffness;
    std::vector<std::uint8_t> m_broken;
    std::vector<std::uint8_t> m_removed;
    std::vector<Vec3> m_external;

    // Node -> bond adjacency (CSR, bonds ascending), built once.
    std::vector<std::uint32_t> m_adjacencyOffsets;
    std::vector<std::uint32_t> m_adjacency;

    // Supported nodes (connected to an anchor) and free ones (supported, not anchored); rebuilt when bonds break.
    bool m_topologyDirty = true;
    bool m_preconditionerDirty = true;
    std::vector<std::uint8_t> m_supported;
    std::vector<std::uint8_t> m_free;
    std::size_t m_freeCount = 0;
    std::vector<double> m_blockInverse; ///< 36 per node: inverse of K's diagonal 6x6 block.

    // 6 doubles per node: (u, theta).
    std::vector<double> m_solution;
    std::vector<double> m_load;
    std::vector<double> m_residual;
    std::vector<double> m_direction;
    std::vector<double> m_preconditioned;
    std::vector<double> m_product;
    std::vector<double> m_bondScratch; ///< 6 per bond: spring force and moment for the current matvec.
    bool m_converged = false;
};

} // namespace interi::destruction
