// SPDX-License-Identifier: MIT
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/StressSolver.h>
#include <interi/jobs/JobSystem.h>
#include <interi/testing/DeterministicRng.h>

#include "StressTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::Vec3;

constexpr double kWeight = 1000.0 * 9.81;
constexpr std::uint32_t kManyIterations = 20000;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

SolveResult SolveFully(StressSolver& solver, JobSystem& jobs)
{
    const SolveResult result = solver.Solve(jobs, kManyIterations);
    EXPECT_TRUE(result.converged) << "residual " << result.relativeResidual;
    return result;
}

/// Single row along +x from an anchored cell at x = 0.
GridStructure Cantilever(int length, float limit = 1.0e12f)
{
    std::vector<Cell> cells;
    for (int x = 0; x <= length; ++x)
    {
        cells.push_back({x, 0, 0});
    }
    GridOptions options;
    options.limit = limit;
    options.anchored = [](const Cell& cell) { return cell[0] == 0; };
    return MakeGrid(std::move(cells), options);
}

float MaxLoad(const BondLoad& load)
{
    return std::max({load.compression, load.tension, load.shear});
}

/// Wall with random external loads (deterministic for a given RNG state).
StressSolver RandomLoadedWall(interi::testing::DeterministicRng& rng)
{
    GridStructure wall = MakeGrid(BoxCells(12, 8, 2), {.limit = 1.0f});
    StressSolver solver(wall.graph);
    for (std::uint32_t node = 0; node < solver.NodeCount(); node += 3)
    {
        solver.SetExternalForce(
            node, Vec3(rng.NextFloat(-2e4f, 2e4f), rng.NextFloat(-5e4f, 0.0f), rng.NextFloat(-2e4f, 2e4f)));
    }
    return solver;
}

std::vector<std::uint64_t> SolutionBits(const StressSolver& solver)
{
    std::vector<std::uint64_t> bits;
    for (std::uint32_t node = 0; node < solver.NodeCount(); ++node)
    {
        for (const Vec3& v : {solver.Displacement(node), solver.Rotation(node)})
        {
            bits.push_back(std::bit_cast<std::uint32_t>(v.x));
            bits.push_back(std::bit_cast<std::uint32_t>(v.y));
            bits.push_back(std::bit_cast<std::uint32_t>(v.z));
        }
    }
    for (std::uint32_t bond = 0; bond < solver.BondCount(); ++bond)
    {
        const BondForces forces = solver.Forces(bond);
        bits.push_back(std::bit_cast<std::uint32_t>(forces.axial));
        bits.push_back(std::bit_cast<std::uint32_t>(forces.shear));
        bits.push_back(std::bit_cast<std::uint32_t>(forces.bending));
        bits.push_back(std::bit_cast<std::uint32_t>(forces.torsion));
    }
    return bits;
}

class StressDeterminism : public interi::testing::SeededTest
{
};

} // namespace

TEST(Stress, ColumnOnGround_AxialForceIsWeightAbove)
{
    JobSystem jobs(Workers(2));
    const GridStructure column = MakeGrid({{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {0, 4, 0}});
    StressSolver solver(column.graph);
    SolveFully(solver, jobs);
    for (int y = 0; y < 4; ++y)
    {
        const std::uint32_t bond = *column.Bond({0, y, 0}, {0, y + 1, 0});
        const BondForces forces = solver.Forces(bond);
        const double above = kWeight * (4 - y);
        EXPECT_NEAR(forces.axial, -above, above * 1e-4) << "bond above y = " << y;
        EXPECT_NEAR(forces.shear, 0.0f, above * 1e-4);
        EXPECT_NEAR(forces.bending, 0.0f, above * 1e-4);
    }
}

TEST(Stress, ColumnOnGround_NoBreak)
{
    JobSystem jobs(Workers(2));
    // Concrete-like limits for a 1 m^2 face: 30 MPa compression, 3 MPa tension, 4 MPa shear.
    GridStructure column = MakeGrid({{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {0, 4, 0}});
    for (StressBond& bond : column.graph.bonds)
    {
        bond.compressiveLimit = 30e6f;
        bond.tensileLimit = 3e6f;
        bond.shearLimit = 4e6f;
    }
    StressSolver solver(column.graph);
    SolveFully(solver, jobs);
    std::vector<std::uint32_t> overloaded;
    solver.FindOverloadedBonds(jobs, {}, overloaded);
    EXPECT_TRUE(overloaded.empty());
}

TEST(Stress, Cantilever_RootShearAndMomentMatchStatics)
{
    JobSystem jobs(Workers(2));
    const GridStructure beam = Cantilever(6);
    StressSolver solver(beam.graph);
    SolveFully(solver, jobs);
    // Free cells x = 1..6 hang off the root face at x = 1; cell x has its centroid at x + 0.5.
    for (int face = 1; face <= 6; ++face)
    {
        double shear = 0.0;
        double moment = 0.0;
        for (int x = face; x <= 6; ++x)
        {
            shear += kWeight;
            moment += kWeight * (x + 0.5 - face);
        }
        const BondForces forces = solver.Forces(*beam.Bond({face - 1, 0, 0}, {face, 0, 0}));
        EXPECT_NEAR(forces.shear, shear, shear * 1e-4) << "face " << face;
        EXPECT_NEAR(forces.bending, moment, moment * 1e-4) << "face " << face;
        EXPECT_NEAR(forces.axial, 0.0f, shear * 1e-4) << "face " << face;
        EXPECT_NEAR(forces.torsion, 0.0f, shear * 1e-4) << "face " << face;
    }
}

TEST(Stress, CantileverBeamOverload_BreaksAtRoot)
{
    JobSystem jobs(Workers(2));
    // Root moment 18 mg -> bending load 108 mg; the next face carries 12.5 mg -> 75 mg. A limit of 108 mg / 1.3
    // fails only the root.
    const float limit = static_cast<float>(108.0 * kWeight / 1.3);
    GridStructure beam = Cantilever(6, limit);
    for (StressBond& bond : beam.graph.bonds)
    {
        bond.shearLimit = 1e12f;
    }
    StressSolver solver(beam.graph);
    SolveFully(solver, jobs);
    std::vector<std::uint32_t> overloaded;
    solver.FindOverloadedBonds(jobs, {}, overloaded);
    ASSERT_EQ(overloaded.size(), 1u);
    EXPECT_EQ(overloaded[0], *beam.Bond({0, 0, 0}, {1, 0, 0}));
}

TEST(Stress, RemoveSupportColumn_DependentBondsExceedLimit)
{
    JobSystem jobs(Workers(2));
    // A slab at y = 2 over x = 0..4 on two columns (x = 0 and x = 4, y = 0..1).
    std::vector<Cell> cells{{0, 0, 0}, {0, 1, 0}, {4, 0, 0}, {4, 1, 0}};
    for (int x = 0; x <= 4; ++x)
    {
        cells.push_back({x, 2, 0});
    }
    const GridStructure frame = MakeGrid(cells, {.limit = 1.0f});
    StressSolver solver(frame.graph);
    SolveFully(solver, jobs);
    float intactMax = 0.0f;
    for (std::uint32_t bond = 0; bond < solver.BondCount(); ++bond)
    {
        intactMax = std::max(intactMax, MaxLoad(ToBondLoad(solver.Forces(bond), 1.0f)));
    }
    // Limits 1.5x the worst intact load: the intact frame holds.
    const std::vector<float> scale(solver.BondCount(), intactMax * 1.5f);
    std::vector<std::uint32_t> overloaded;
    solver.FindOverloadedBonds(jobs, scale, overloaded);
    EXPECT_TRUE(overloaded.empty());

    solver.RemoveNode(*frame.Node({4, 0, 0}));
    solver.RemoveNode(*frame.Node({4, 1, 0}));
    EXPECT_FALSE(solver.IsConverged());
    SolveFully(solver, jobs);
    solver.FindOverloadedBonds(jobs, scale, overloaded);
    ASSERT_FALSE(overloaded.empty());
    // The slab now cantilevers off the left column: its root (above the column) is overloaded.
    const std::uint32_t root = *frame.Bond({0, 2, 0}, {1, 2, 0});
    EXPECT_NE(std::ranges::find(overloaded, root), overloaded.end());
}

TEST(Stress, BridgeCenterLoad_TensionAtBottomCompressionAtTop)
{
    JobSystem jobs(Workers(2));
    // Two layers (y = 0, 1), x = 0..8, both ends anchored.
    GridOptions options;
    options.anchored = [](const Cell& cell) { return cell[0] == 0 || cell[0] == 8; };
    const GridStructure bridge = MakeGrid(BoxCells(9, 2, 1), options);
    StressSolver solver(bridge.graph);
    const float load = 1.0e6f;
    solver.SetExternalForce(*bridge.Node({4, 1, 0}), Vec3(0.0f, -load, 0.0f));
    SolveFully(solver, jobs);
    for (const int x : {3, 4})
    {
        const BondForces top = solver.Forces(*bridge.Bond({x, 1, 0}, {x + 1, 1, 0}));
        const BondForces bottom = solver.Forces(*bridge.Bond({x, 0, 0}, {x + 1, 0, 0}));
        EXPECT_LT(top.axial, -0.1f * load) << "x = " << x;
        EXPECT_GT(bottom.axial, 0.1f * load) << "x = " << x;
        EXPECT_NEAR(top.axial, -bottom.axial, 0.05f * load) << "chord forces balance";
    }
}

TEST(Stress, WarmStart_ConvergesInFewerIterations)
{
    JobSystem jobs(Workers(2));
    const GridStructure wall = MakeGrid(BoxCells(16, 10, 2));
    const std::uint32_t bond = *wall.Bond({8, 5, 0}, {8, 6, 0});
    StressSolver warm(wall.graph);
    SolveFully(warm, jobs);
    warm.BreakBond(bond);
    EXPECT_FALSE(warm.IsConverged());
    const SolveResult warmResult = SolveFully(warm, jobs);

    StressSolver cold(wall.graph);
    cold.BreakBond(bond);
    const SolveResult coldResult = SolveFully(cold, jobs);
    // Restarted CG keeps only the solution, so a local change still costs most of a solve (measured: 214 vs 221).
    EXPECT_LT(warmResult.iterations, coldResult.iterations);
}

TEST(Stress, ConvergedSolve_RunsNoIterations)
{
    JobSystem jobs(Workers(2));
    StressSolver solver(MakeGrid(BoxCells(4, 4, 1)).graph);
    SolveFully(solver, jobs);
    const SolveResult again = solver.Solve(jobs, kManyIterations);
    EXPECT_TRUE(again.converged);
    EXPECT_EQ(again.iterations, 0u);
}

TEST(Stress, IterationBudget_ContinuesAcrossSolves)
{
    JobSystem jobs(Workers(2));
    const GridStructure wall = MakeGrid(BoxCells(16, 10, 2));
    StressSolver budgeted(wall.graph);
    SolveResult step;
    std::uint32_t total = 0;
    for (int frame = 0; frame < 1000 && !budgeted.IsConverged(); ++frame)
    {
        step = budgeted.Solve(jobs, 4);
        EXPECT_LE(step.iterations, 4u);
        total += step.iterations;
    }
    EXPECT_TRUE(step.converged);
    EXPECT_GT(total, 4u);
}

TEST(Stress, FloatingPiece_LeavesSolveAndRestConverges)
{
    JobSystem jobs(Workers(2));
    const GridStructure wall = MakeGrid(BoxCells(6, 6, 1));
    StressSolver solver(wall.graph);
    // Detach the 2x2 block at x = 2..3, y = 2..3 by breaking every bond around it.
    const auto inBlock = [](const Cell& c) { return c[0] >= 2 && c[0] <= 3 && c[1] >= 2 && c[1] <= 3; };
    for (std::uint32_t bond = 0; bond < solver.BondCount(); ++bond)
    {
        const StressBond& b = wall.graph.bonds[bond];
        if (inBlock(wall.cells[b.nodeA]) != inBlock(wall.cells[b.nodeB]))
        {
            solver.BreakBond(bond);
        }
    }
    SolveFully(solver, jobs);
    for (std::uint32_t node = 0; node < solver.NodeCount(); ++node)
    {
        EXPECT_EQ(solver.IsSupported(node), !inBlock(wall.cells[node])) << node;
    }
    // The block's internal bonds carry nothing (it is not part of the solve).
    const BondForces inside = solver.Forces(*wall.Bond({2, 2, 0}, {3, 2, 0}));
    EXPECT_EQ(inside.axial, 0.0f);
    EXPECT_EQ(inside.shear, 0.0f);
}

TEST(Stress, NoAnchors_NothingSupported)
{
    JobSystem jobs(Workers(2));
    GridOptions options;
    options.anchored = [](const Cell&) { return false; };
    StressSolver solver(MakeGrid(BoxCells(3, 3, 1), options).graph);
    const SolveResult result = solver.Solve(jobs, 10);
    EXPECT_TRUE(result.converged);
    EXPECT_EQ(result.iterations, 0u);
    EXPECT_FALSE(solver.IsSupported(0));
}

TEST(Stress, ParallelVsSerial_SameBrokenBondSet)
{
    interi::testing::DeterministicRng rng(1234);
    StressSolver serial = RandomLoadedWall(rng);
    interi::testing::DeterministicRng rng2(1234);
    StressSolver parallel = RandomLoadedWall(rng2);

    JobSystem one(Workers(0));
    JobSystem many(Workers(8));
    const SolveResult a = serial.Solve(one, kManyIterations);
    const SolveResult b = parallel.Solve(many, kManyIterations);
    EXPECT_EQ(a.iterations, b.iterations);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a.relativeResidual), std::bit_cast<std::uint64_t>(b.relativeResidual));
    EXPECT_EQ(SolutionBits(serial), SolutionBits(parallel));

    const std::vector<float> scale(serial.BondCount(), 2.0e4f);
    std::vector<std::uint32_t> brokenSerial;
    std::vector<std::uint32_t> brokenParallel;
    serial.FindOverloadedBonds(one, scale, brokenSerial);
    parallel.FindOverloadedBonds(many, scale, brokenParallel);
    EXPECT_FALSE(brokenSerial.empty());
    EXPECT_EQ(brokenSerial, brokenParallel);
}

TEST_F(StressDeterminism, SameSeedSameResult)
{
    const std::uint64_t seed = Rng().NextU64();
    std::vector<std::uint64_t> first;
    for (int run = 0; run < 2; ++run)
    {
        interi::testing::DeterministicRng rng(seed);
        StressSolver solver = RandomLoadedWall(rng);
        JobSystem jobs(Workers(4));
        // Solve in budgeted steps with a break in between, like the runtime does.
        (void)solver.Solve(jobs, 7);
        solver.BreakBond(static_cast<std::uint32_t>(rng.NextInt(0, static_cast<std::int64_t>(solver.BondCount()) - 1)));
        (void)solver.Solve(jobs, kManyIterations);
        if (run == 0)
        {
            first = SolutionBits(solver);
        }
        else
        {
            EXPECT_EQ(first, SolutionBits(solver)) << "seed " << seed;
        }
    }
}

TEST(Stress, ToBondLoad_AddsBendingAndTorsion)
{
    const BondLoad load = ToBondLoad({.axial = -100.0f, .shear = 10.0f, .bending = 4.0f, .torsion = 1.0f}, 4.0f);
    // sqrt(A) = 2: bending 6 * 4 / 2 = 12; torsion ~4.8 * 1 / 2 = 2.4.
    EXPECT_NEAR(load.compression, 112.0f, 1e-3f);
    EXPECT_NEAR(load.tension, 12.0f, 1e-3f);
    EXPECT_NEAR(load.shear, 12.4f, 0.05f);
}

TEST(Stress, MakeStressGraph_NodesAreChunks)
{
    DestructibleAsset asset;
    asset.materials.resize(1);
    asset.chunks.resize(3);
    asset.chunks[0].childCount = 2;
    asset.chunks[0].firstChild = 1;
    asset.chunks[0].mass = 30.0f;
    asset.chunks[1].parent = 0;
    asset.chunks[1].mass = 10.0f;
    asset.chunks[1].centroid = {0.5f, 0.5f, 0.5f};
    asset.chunks[2].parent = 0;
    asset.chunks[2].mass = 20.0f;
    asset.chunks[2].centroid = {0.5f, 1.5f, 0.5f};
    Bond bond;
    bond.chunkA = 1;
    bond.chunkB = 2;
    bond.area = 1.0f;
    bond.centroid = {0.5f, 1.0f, 0.5f};
    bond.normal = {0.0f, 1.0f, 0.0f};
    bond.compressiveLimit = 3.0f;
    bond.tensileLimit = 2.0f;
    bond.shearLimit = 1.0f;
    asset.bonds.push_back(bond);
    asset.anchors = {1};

    const StressGraph graph = MakeStressGraph(asset);
    ASSERT_EQ(graph.nodes.size(), 3u);
    EXPECT_FALSE(graph.nodes[0].anchored);
    EXPECT_TRUE(graph.nodes[1].anchored);
    EXPECT_FALSE(graph.nodes[2].anchored);
    EXPECT_EQ(graph.nodes[2].mass, 20.0f);
    EXPECT_EQ(graph.nodes[2].position, asset.chunks[2].centroid);
    ASSERT_EQ(graph.bonds.size(), 1u);
    EXPECT_EQ(graph.bonds[0].nodeA, 1u);
    EXPECT_EQ(graph.bonds[0].nodeB, 2u);
    EXPECT_EQ(graph.bonds[0].tensileLimit, 2.0f);
}
