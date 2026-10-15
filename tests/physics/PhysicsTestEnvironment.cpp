// SPDX-License-Identifier: MIT
// One JoltRuntime for the whole test process, as `main` would hold it.
#include <interi/physics/JoltRuntime.h>

#include <gtest/gtest.h>

#include <memory>

namespace
{

class PhysicsEnvironment final : public ::testing::Environment
{
public:
    void SetUp() override { m_runtime = std::make_unique<interi::physics::JoltRuntime>(); }
    void TearDown() override { m_runtime.reset(); }

private:
    std::unique_ptr<interi::physics::JoltRuntime> m_runtime;
};

[[maybe_unused]] ::testing::Environment* const g_physicsEnvironment =
    ::testing::AddGlobalTestEnvironment(new PhysicsEnvironment());

} // namespace
