// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/StressSolver.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include "Vec3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace interi::destruction
{

namespace
{

using detail::Vec3d;

constexpr std::size_t kDofs = 6;
constexpr std::size_t kBondGrain = 1024;
constexpr std::size_t kNodeGrain = 512;
constexpr double kMinArea = 1e-8;
constexpr double kMinLength = 1e-4;
/// Torsion constant of a square section, J = kTorsionConstant * A^2.
constexpr double kTorsionConstant = 0.1406;
/// Peak shear stress of a square section under torsion is T / (0.208 s^3); times the area s^2 gives ~4.81 T / s.
constexpr float kTorsionShearFactor = 1.0f / 0.208f;

using Block = std::array<double, kDofs * kDofs>;

Vec3d Load3(std::span<const double> v, std::size_t offset) noexcept
{
    return {v[offset], v[offset + 1], v[offset + 2]};
}

void Store3(std::span<double> v, std::size_t offset, const Vec3d& value) noexcept
{
    v[offset] = value.x;
    v[offset + 1] = value.y;
    v[offset + 2] = value.z;
}

/// Inverts a symmetric positive definite 6x6 matrix by Cholesky factorisation. Returns false if it is not.
bool InvertSpd(const Block& matrix, Block& inverse) noexcept
{
    Block lower{};
    for (std::size_t i = 0; i < kDofs; ++i)
    {
        for (std::size_t j = 0; j <= i; ++j)
        {
            double sum = matrix[i * kDofs + j];
            for (std::size_t k = 0; k < j; ++k)
            {
                sum -= lower[i * kDofs + k] * lower[j * kDofs + k];
            }
            if (i == j)
            {
                if (!(sum > 0.0))
                {
                    return false;
                }
                lower[i * kDofs + i] = std::sqrt(sum);
            }
            else
            {
                lower[i * kDofs + j] = sum / lower[j * kDofs + j];
            }
        }
    }
    // Solve L L^T x = e_c for each column c.
    for (std::size_t c = 0; c < kDofs; ++c)
    {
        std::array<double, kDofs> y{};
        for (std::size_t i = 0; i < kDofs; ++i)
        {
            double sum = i == c ? 1.0 : 0.0;
            for (std::size_t k = 0; k < i; ++k)
            {
                sum -= lower[i * kDofs + k] * y[k];
            }
            y[i] = sum / lower[i * kDofs + i];
        }
        for (std::size_t i = kDofs; i-- > 0;)
        {
            double sum = y[i];
            for (std::size_t k = i + 1; k < kDofs; ++k)
            {
                sum -= lower[k * kDofs + i] * inverse[k * kDofs + c];
            }
            inverse[i * kDofs + c] = sum / lower[i * kDofs + i];
        }
    }
    return true;
}

double Dot6(std::span<const double> a, std::span<const double> b, std::size_t node) noexcept
{
    const std::size_t o = node * kDofs;
    double sum = 0.0;
    for (std::size_t k = 0; k < kDofs; ++k)
    {
        sum += a[o + k] * b[o + k];
    }
    return sum;
}

} // namespace

StressGraph MakeStressGraph(const DestructibleAsset& asset)
{
    StressGraph graph;
    graph.nodes.reserve(asset.chunks.size());
    for (const AssetChunk& chunk : asset.chunks)
    {
        graph.nodes.push_back({.position = chunk.centroid, .mass = chunk.mass, .anchored = false});
    }
    for (const std::uint32_t anchor : asset.anchors)
    {
        graph.nodes[anchor].anchored = true;
    }
    graph.bonds.reserve(asset.bonds.size());
    for (const Bond& bond : asset.bonds)
    {
        graph.bonds.push_back({.nodeA = bond.chunkA,
                               .nodeB = bond.chunkB,
                               .centroid = bond.centroid,
                               .normal = bond.normal,
                               .area = bond.area,
                               .compressiveLimit = bond.compressiveLimit,
                               .tensileLimit = bond.tensileLimit,
                               .shearLimit = bond.shearLimit});
    }
    return graph;
}

BondLoad ToBondLoad(const BondForces& forces, float area) noexcept
{
    const float side = std::sqrt(std::max(area, static_cast<float>(kMinArea)));
    const float bending = 6.0f * forces.bending / side;
    return {.compression = std::max(-forces.axial, 0.0f) + bending,
            .tension = std::max(forces.axial, 0.0f) + bending,
            .shear = forces.shear + kTorsionShearFactor * forces.torsion / side};
}

StressSolver::StressSolver(StressGraph graph, const StressSettings& settings)
    : m_graph(std::move(graph))
    , m_settings(settings)
{
    const std::size_t nodeCount = m_graph.nodes.size();
    const std::size_t bondCount = m_graph.bonds.size();
    const double youngs = settings.youngsModulus;
    const double shearModulus = youngs * settings.shearModulusRatio;

    m_stiffness.resize(bondCount);
    m_adjacencyOffsets.assign(nodeCount + 1, 0);
    for (std::size_t i = 0; i < bondCount; ++i)
    {
        const StressBond& bond = m_graph.bonds[i];
        INTERI_ASSERT(bond.nodeA < nodeCount && bond.nodeB < nodeCount && bond.nodeA != bond.nodeB);
        const double area = std::max(static_cast<double>(bond.area), kMinArea);
        const Vec3d delta =
            Vec3d::From(m_graph.nodes[bond.nodeB].position) - Vec3d::From(m_graph.nodes[bond.nodeA].position);
        const double length = std::max(detail::Length(delta), kMinLength);
        const double inertia = area * area / 12.0;
        m_stiffness[i] = {.axial = youngs * area / length,
                          .shear = shearModulus * area / length,
                          .bending = youngs * inertia / length,
                          .torsion = shearModulus * kTorsionConstant * area * area / length};
        ++m_adjacencyOffsets[bond.nodeA + 1];
        ++m_adjacencyOffsets[bond.nodeB + 1];
    }
    for (std::size_t n = 0; n < nodeCount; ++n)
    {
        m_adjacencyOffsets[n + 1] += m_adjacencyOffsets[n];
    }
    m_adjacency.resize(m_adjacencyOffsets[nodeCount]);
    std::vector<std::uint32_t> fill(m_adjacencyOffsets.begin(), m_adjacencyOffsets.end() - 1);
    for (std::uint32_t i = 0; i < bondCount; ++i)
    {
        m_adjacency[fill[m_graph.bonds[i].nodeA]++] = i;
        m_adjacency[fill[m_graph.bonds[i].nodeB]++] = i;
    }

    m_broken.assign(bondCount, 0);
    m_removed.assign(nodeCount, 0);
    m_external.assign(nodeCount, Vec3{});
    m_supported.assign(nodeCount, 0);
    m_free.assign(nodeCount, 0);
    m_blockInverse.assign(nodeCount * kDofs * kDofs, 0.0);
    for (std::vector<double>* v : {&m_solution, &m_residual, &m_direction, &m_preconditioned, &m_product, &m_load})
    {
        v->assign(nodeCount * kDofs, 0.0);
    }
    m_bondScratch.assign(bondCount * kDofs, 0.0);
}

void StressSolver::BreakBond(std::uint32_t bond)
{
    INTERI_ASSERT(bond < m_broken.size());
    if (m_broken[bond] == 0)
    {
        m_broken[bond] = 1;
        m_topologyDirty = true;
        m_converged = false;
    }
}

void StressSolver::RemoveNode(std::uint32_t node)
{
    INTERI_ASSERT(node < m_removed.size());
    if (m_removed[node] == 0)
    {
        m_removed[node] = 1;
        m_topologyDirty = true;
        m_converged = false;
    }
    for (std::uint32_t k = m_adjacencyOffsets[node]; k < m_adjacencyOffsets[node + 1]; ++k)
    {
        BreakBond(m_adjacency[k]);
    }
}

bool StressSolver::IsSupported(std::uint32_t node)
{
    UpdateSupport();
    return m_supported[node] != 0;
}

void StressSolver::SetExternalForce(std::uint32_t node, const Vec3& force)
{
    if (m_external[node] != force)
    {
        m_external[node] = force;
        m_converged = false;
    }
}

void StressSolver::ClearExternalForces()
{
    std::ranges::fill(m_external, Vec3{});
    m_converged = false;
}

void StressSolver::ResetSolution()
{
    std::ranges::fill(m_solution, 0.0);
    m_converged = false;
}

void StressSolver::UpdateSupport()
{
    if (!m_topologyDirty)
    {
        return;
    }
    const std::size_t nodeCount = m_graph.nodes.size();
    std::ranges::fill(m_supported, std::uint8_t{0});
    std::vector<std::uint32_t> stack;
    for (std::uint32_t n = 0; n < nodeCount; ++n)
    {
        if (m_graph.nodes[n].anchored && m_removed[n] == 0)
        {
            m_supported[n] = 1;
            stack.push_back(n);
        }
    }
    while (!stack.empty())
    {
        const std::uint32_t node = stack.back();
        stack.pop_back();
        for (std::uint32_t k = m_adjacencyOffsets[node]; k < m_adjacencyOffsets[node + 1]; ++k)
        {
            const std::uint32_t bond = m_adjacency[k];
            if (m_broken[bond] != 0)
            {
                continue;
            }
            const StressBond& b = m_graph.bonds[bond];
            const std::uint32_t other = b.nodeA == node ? b.nodeB : b.nodeA;
            if (m_supported[other] == 0)
            {
                m_supported[other] = 1;
                stack.push_back(other);
            }
        }
    }
    m_freeCount = 0;
    for (std::uint32_t n = 0; n < nodeCount; ++n)
    {
        m_free[n] = m_supported[n] != 0 && !m_graph.nodes[n].anchored ? 1 : 0;
        m_freeCount += m_free[n];
    }
    m_topologyDirty = false;
    m_preconditionerDirty = true;
}

void StressSolver::UpdatePreconditioner(JobSystem& jobs)
{
    if (!m_preconditionerDirty)
    {
        return;
    }
    ParallelFor(jobs, 0, m_graph.nodes.size(), kNodeGrain,
                [&](std::size_t node)
                {
                    const std::span<double> inverse(m_blockInverse.data() + node * kDofs * kDofs, kDofs * kDofs);
                    std::ranges::fill(inverse, 0.0);
                    if (m_free[node] == 0)
                    {
                        return;
                    }
                    Block block{};
                    const Vec3d position = Vec3d::From(m_graph.nodes[node].position);
                    for (std::uint32_t k = m_adjacencyOffsets[node]; k < m_adjacencyOffsets[node + 1]; ++k)
                    {
                        const std::uint32_t bondIndex = m_adjacency[k];
                        if (m_broken[bondIndex] != 0)
                        {
                            continue;
                        }
                        const StressBond& bond = m_graph.bonds[bondIndex];
                        const BondStiffness& s = m_stiffness[bondIndex];
                        const Vec3d n = Vec3d::From(bond.normal);
                        const Vec3d arm = Vec3d::From(bond.centroid) - position;
                        const std::array<double, 3> nv{n.x, n.y, n.z};
                        // D = k_s I + (k_n - k_s) n n^T, R = k_b I + (k_t - k_b) n n^T.
                        std::array<double, 9> d{};
                        std::array<double, 9> r{};
                        for (std::size_t i = 0; i < 3; ++i)
                        {
                            for (std::size_t j = 0; j < 3; ++j)
                            {
                                const double nn = nv[i] * nv[j];
                                d[i * 3 + j] = (i == j ? s.shear : 0.0) + (s.axial - s.shear) * nn;
                                r[i * 3 + j] = (i == j ? s.bending : 0.0) + (s.torsion - s.bending) * nn;
                            }
                        }
                        // Jacobian of the face displacement w.r.t. (u, theta): [I, -[arm]x].
                        const std::array<double, 9> crossArm{0.0,    arm.z,  -arm.y, //
                                                             -arm.z, 0.0,    arm.x,  //
                                                             arm.y,  -arm.x, 0.0};
                        std::array<double, 18> jacobian{};
                        for (std::size_t i = 0; i < 3; ++i)
                        {
                            jacobian[i * 6 + i] = 1.0;
                            for (std::size_t j = 0; j < 3; ++j)
                            {
                                jacobian[i * 6 + 3 + j] = crossArm[i * 3 + j];
                            }
                        }
                        for (std::size_t i = 0; i < kDofs; ++i)
                        {
                            for (std::size_t j = 0; j < kDofs; ++j)
                            {
                                double sum = 0.0;
                                for (std::size_t a = 0; a < 3; ++a)
                                {
                                    for (std::size_t b = 0; b < 3; ++b)
                                    {
                                        sum += jacobian[a * 6 + i] * d[a * 3 + b] * jacobian[b * 6 + j];
                                    }
                                }
                                if (i >= 3 && j >= 3)
                                {
                                    sum += r[(i - 3) * 3 + (j - 3)];
                                }
                                block[i * kDofs + j] += sum;
                            }
                        }
                    }
                    Block result{};
                    const bool ok = InvertSpd(block, result);
                    INTERI_ASSERT(ok);
                    if (ok)
                    {
                        std::ranges::copy(result, inverse.begin());
                    }
                });
    m_preconditionerDirty = false;
}

void StressSolver::ComputeBondForces(std::size_t bondIndex, std::span<const double> x, std::span<double> out) const
{
    const StressBond& bond = m_graph.bonds[bondIndex];
    if (m_broken[bondIndex] != 0 || m_supported[bond.nodeA] == 0)
    {
        std::ranges::fill(out, 0.0);
        return;
    }
    const BondStiffness& s = m_stiffness[bondIndex];
    const Vec3d centroid = Vec3d::From(bond.centroid);
    const Vec3d armA = centroid - Vec3d::From(m_graph.nodes[bond.nodeA].position);
    const Vec3d armB = centroid - Vec3d::From(m_graph.nodes[bond.nodeB].position);
    const std::size_t a = std::size_t{bond.nodeA} * kDofs;
    const std::size_t b = std::size_t{bond.nodeB} * kDofs;
    const Vec3d thetaA = Load3(x, a + 3);
    const Vec3d thetaB = Load3(x, b + 3);
    const Vec3d d = (Load3(x, b) + detail::Cross(thetaB, armB)) - (Load3(x, a) + detail::Cross(thetaA, armA));
    const Vec3d r = thetaB - thetaA;
    const Vec3d n = Vec3d::From(bond.normal);
    const double dn = detail::Dot(n, d);
    const double rn = detail::Dot(n, r);
    const Vec3d force = n * (s.axial * dn) + (d - n * dn) * s.shear;
    const Vec3d moment = n * (s.torsion * rn) + (r - n * rn) * s.bending;
    Store3(out, 0, force);
    Store3(out, 3, moment);
}

void StressSolver::ApplyStiffness(JobSystem& jobs, std::span<const double> x, std::span<double> out)
{
    ParallelFor(jobs, 0, m_graph.bonds.size(), kBondGrain, [&](std::size_t bond)
                { ComputeBondForces(bond, x, std::span<double>(m_bondScratch.data() + bond * kDofs, kDofs)); });
    ParallelFor(jobs, 0, m_graph.nodes.size(), kNodeGrain,
                [&](std::size_t node)
                {
                    Vec3d linear;
                    Vec3d angular;
                    if (m_free[node] != 0)
                    {
                        const Vec3d position = Vec3d::From(m_graph.nodes[node].position);
                        for (std::uint32_t k = m_adjacencyOffsets[node]; k < m_adjacencyOffsets[node + 1]; ++k)
                        {
                            const std::uint32_t bondIndex = m_adjacency[k];
                            const StressBond& bond = m_graph.bonds[bondIndex];
                            const std::span<const double> scratch(m_bondScratch.data() + bondIndex * kDofs, kDofs);
                            const Vec3d force = Load3(scratch, 0);
                            const Vec3d moment = Load3(scratch, 3);
                            const Vec3d arm = Vec3d::From(bond.centroid) - position;
                            const Vec3d torque = moment + detail::Cross(arm, force);
                            if (bond.nodeB == node)
                            {
                                linear = linear + force;
                                angular = angular + torque;
                            }
                            else
                            {
                                linear = linear - force;
                                angular = angular - torque;
                            }
                        }
                    }
                    Store3(out, node * kDofs, linear);
                    Store3(out, node * kDofs + 3, angular);
                });
}

void StressSolver::Precondition(JobSystem& jobs, std::span<const double> r, std::span<double> z) const
{
    ParallelFor(jobs, 0, m_graph.nodes.size(), kNodeGrain,
                [&](std::size_t node)
                {
                    const double* inverse = m_blockInverse.data() + node * kDofs * kDofs;
                    const std::size_t o = node * kDofs;
                    for (std::size_t i = 0; i < kDofs; ++i)
                    {
                        double sum = 0.0;
                        for (std::size_t j = 0; j < kDofs; ++j)
                        {
                            sum += inverse[i * kDofs + j] * r[o + j];
                        }
                        z[o + i] = sum;
                    }
                });
}

double StressSolver::DotProduct(JobSystem& jobs, std::span<const double> a, std::span<const double> b) const
{
    return ParallelReduce(
        jobs, 0, m_graph.nodes.size(), kNodeGrain, 0.0, [&](std::size_t node) { return Dot6(a, b, node); },
        [](double x, double y) { return x + y; });
}

SolveResult StressSolver::Solve(JobSystem& jobs, std::uint32_t maxIterations)
{
    UpdateSupport();
    UpdatePreconditioner(jobs);
    const std::size_t nodeCount = m_graph.nodes.size();
    const Vec3d gravity = Vec3d::From(m_settings.gravity);

    // Load vector over free nodes; everything else is pinned to zero.
    ParallelFor(jobs, 0, nodeCount, kNodeGrain,
                [&](std::size_t node)
                {
                    const std::size_t o = node * kDofs;
                    if (m_free[node] == 0)
                    {
                        std::fill_n(m_load.begin() + static_cast<std::ptrdiff_t>(o), kDofs, 0.0);
                        std::fill_n(m_solution.begin() + static_cast<std::ptrdiff_t>(o), kDofs, 0.0);
                        return;
                    }
                    const Vec3d weight = gravity * static_cast<double>(m_graph.nodes[node].mass);
                    Store3(m_load, o, weight + Vec3d::From(m_external[node]));
                    Store3(m_load, o + 3, {});
                });

    SolveResult result;
    const double loadNorm = std::sqrt(DotProduct(jobs, m_load, m_load));
    if (m_freeCount == 0 || loadNorm == 0.0)
    {
        std::ranges::fill(m_solution, 0.0);
        result.converged = true;
        m_converged = true;
        return result;
    }
    const double tolerance = m_settings.relativeTolerance * loadNorm;

    // r = f - K u
    ApplyStiffness(jobs, m_solution, m_product);
    ParallelFor(jobs, 0, nodeCount * kDofs, kNodeGrain * kDofs,
                [&](std::size_t i) { m_residual[i] = m_load[i] - m_product[i]; });
    double residualNorm = std::sqrt(DotProduct(jobs, m_residual, m_residual));
    if (residualNorm <= tolerance)
    {
        result.converged = true;
        result.relativeResidual = residualNorm / loadNorm;
        m_converged = true;
        return result;
    }

    Precondition(jobs, m_residual, m_preconditioned);
    std::ranges::copy(m_preconditioned, m_direction.begin());
    double rz = DotProduct(jobs, m_residual, m_preconditioned);
    while (result.iterations < maxIterations)
    {
        ApplyStiffness(jobs, m_direction, m_product);
        const double curvature = DotProduct(jobs, m_direction, m_product);
        if (!(curvature > 0.0))
        {
            break;
        }
        const double alpha = rz / curvature;
        ParallelFor(jobs, 0, nodeCount * kDofs, kNodeGrain * kDofs,
                    [&](std::size_t i)
                    {
                        m_solution[i] += alpha * m_direction[i];
                        m_residual[i] -= alpha * m_product[i];
                    });
        ++result.iterations;
        residualNorm = std::sqrt(DotProduct(jobs, m_residual, m_residual));
        if (residualNorm <= tolerance)
        {
            result.converged = true;
            break;
        }
        Precondition(jobs, m_residual, m_preconditioned);
        const double rzNext = DotProduct(jobs, m_residual, m_preconditioned);
        const double beta = rzNext / rz;
        rz = rzNext;
        ParallelFor(jobs, 0, nodeCount * kDofs, kNodeGrain * kDofs,
                    [&](std::size_t i) { m_direction[i] = m_preconditioned[i] + beta * m_direction[i]; });
    }
    result.relativeResidual = residualNorm / loadNorm;
    m_converged = result.converged;
    return result;
}

BondForces StressSolver::Forces(std::uint32_t bond) const
{
    std::array<double, kDofs> out{};
    if (!m_topologyDirty)
    {
        ComputeBondForces(bond, m_solution, out);
    }
    const StressBond& b = m_graph.bonds[bond];
    const Vec3d n = Vec3d::From(b.normal);
    const Vec3d force{out[0], out[1], out[2]};
    const Vec3d moment{out[3], out[4], out[5]};
    const double axial = detail::Dot(n, force);
    const double twist = detail::Dot(n, moment);
    return {.axial = static_cast<float>(axial),
            .shear = static_cast<float>(detail::Length(force - n * axial)),
            .bending = static_cast<float>(detail::Length(moment - n * twist)),
            .torsion = static_cast<float>(std::abs(twist))};
}

void StressSolver::FindOverloadedBonds(JobSystem& jobs, std::span<const float> limitScale,
                                       std::vector<std::uint32_t>& out) const
{
    INTERI_ASSERT(limitScale.empty() || limitScale.size() == m_graph.bonds.size());
    out.clear();
    if (m_topologyDirty)
    {
        return; // No solution for the current structure yet.
    }
    std::vector<std::uint8_t> overloaded(m_graph.bonds.size(), 0);
    ParallelFor(jobs, 0, m_graph.bonds.size(), kBondGrain,
                [&](std::size_t bond)
                {
                    if (m_broken[bond] != 0)
                    {
                        return;
                    }
                    const StressBond& b = m_graph.bonds[bond];
                    const float scale = limitScale.empty() ? 1.0f : limitScale[bond];
                    const BondLoad load = ToBondLoad(Forces(static_cast<std::uint32_t>(bond)), b.area);
                    overloaded[bond] = load.compression > b.compressiveLimit * scale ||
                                               load.tension > b.tensileLimit * scale ||
                                               load.shear > b.shearLimit * scale
                                           ? 1
                                           : 0;
                });
    for (std::uint32_t bond = 0; bond < overloaded.size(); ++bond)
    {
        if (overloaded[bond] != 0)
        {
            out.push_back(bond);
        }
    }
}

Vec3 StressSolver::Displacement(std::uint32_t node) const
{
    return Load3(m_solution, std::size_t{node} * kDofs).ToFloat();
}

Vec3 StressSolver::Rotation(std::uint32_t node) const
{
    return Load3(m_solution, std::size_t{node} * kDofs + 3).ToFloat();
}

float BondUtilization(const StressSolver& solver, std::uint32_t bond, float healthFraction)
{
    if (solver.IsBroken(bond) || !(healthFraction > 0.0f))
    {
        return solver.IsBroken(bond) ? 0.0f : 1.0f;
    }
    const StressBond& b = solver.Graph().bonds[bond];
    const BondLoad load = ToBondLoad(solver.Forces(bond), b.area);
    const auto ratio = [&](float value, float limit) { return limit > 0.0f ? value / (limit * healthFraction) : 0.0f; };
    return std::max({ratio(load.compression, b.compressiveLimit), ratio(load.tension, b.tensileLimit),
                     ratio(load.shear, b.shearLimit)});
}

} // namespace interi::destruction
