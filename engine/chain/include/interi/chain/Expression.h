// SPDX-License-Identifier: MIT
#pragma once

// The Chain Reaction expression language: data, not scripting. Conditions on links and parameter bindings are
// expressions over the upstream payload, Values and module-registered pure functions:
//
//     payload.damage * 2 > value("com.author.fps:Armor") && !payload.headshot
//     clamp(payload.speed, 0, value("value.MaxSpeed"))
//     vec3(1, 0, 0) * payload.distance
//     fn.HasTag(self, "Enemy")
//
// Types are the value types (values::ValueType). Arithmetic: int (int32, wrapping), u32 (wrapping), float and vec3
// (vec3 +- vec3, vec3 */ number, number * vec3, -vec3); an int or u32 mixed with a float is promoted to float, int
// mixed with u32 is an error. Comparison: < <= > >= on numbers; == != on any two values of one type. Logic: && ||
// (short-circuit) ! on bool. string + string concatenates. `.x .y .z` read a vec3's components. `self` is the
// activation's entity (an entity ref). Integer division or remainder by zero yields 0; float operations follow IEEE
// (a non-finite result is rejected when it is written to a Value).
//
// Built-in functions: min max abs clamp (numbers), sqrt floor (float), int (saturating; NaN -> 0), float, vec3(x, y,
// z), length dot (vec3). Any other call name is a function id in the FunctionRegistry. There is no trigonometry: its
// results differ between standard libraries, and chain results must be bit-identical across platforms.
//
// CompileExpression type-checks once against what the expression may reference (ExprEnvironment) and reports the
// first error with its byte span in the source. EvaluateExpression is thread-safe for one CompiledExpression.

#include <interi/chain/Functions.h>
#include <interi/values/Value.h>
#include <interi/values/ValueRegistry.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::chain
{

/// A byte range [begin, end) in an expression's source.
struct SourceSpan
{
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    friend constexpr bool operator==(const SourceSpan&, const SourceSpan&) noexcept = default;
};

/// A compile error: what is wrong and where.
struct ExprError
{
    std::string message;
    SourceSpan span;
};

/// One named field of a payload.
struct PayloadField
{
    std::string name;
    values::ValueType type = values::ValueType::Float;
};

/// What an expression may reference while it is compiled.
struct ExprEnvironment
{
    /// The payload fields `payload.<name>` may name (a payload value is passed by field index).
    std::span<const PayloadField> payload;
    /// Values `value("<id>")` may name (by their winning definition's type); nullptr: none.
    const values::ValueRegistry* values = nullptr;
    /// Functions calls may name; nullptr: built-ins only.
    const FunctionRegistry* functions = nullptr;
};

/// Reads Values while an expression is evaluated (the executor's pre-step state).
class IValueReader
{
public:
    IValueReader() = default;
    IValueReader(const IValueReader&) = default;
    IValueReader& operator=(const IValueReader&) = default;
    IValueReader(IValueReader&&) = default;
    IValueReader& operator=(IValueReader&&) = default;

    /// The value of `id`, or nullptr.
    [[nodiscard]] virtual const values::Value* Read(std::string_view id) const = 0;

protected:
    ~IValueReader() = default;
};

/// What an expression sees while it is evaluated.
struct ExprContext
{
    std::span<const values::Value> payload; ///< By field index of the compile-time ExprEnvironment::payload.
    const IValueReader* values = nullptr;   ///< nullptr: every value("...") reads as its type's zero.
    std::uint64_t self = 0;                 ///< The activation's entity id.
};

namespace detail
{
struct ExprProgram;
} // namespace detail

/// A type-checked expression. Cheap to copy (shares the immutable program).
class CompiledExpression
{
public:
    CompiledExpression() = default;

    [[nodiscard]] bool Valid() const noexcept { return m_program != nullptr; }
    /// The result type.
    [[nodiscard]] values::ValueType Type() const noexcept { return m_type; }
    /// The ids of every Value the expression reads, sorted and unique.
    [[nodiscard]] std::span<const std::string> ValueReads() const noexcept;

private:
    friend std::expected<CompiledExpression, ExprError> CompileExpression(std::string_view, const ExprEnvironment&);
    friend values::Value EvaluateExpression(const CompiledExpression&, const ExprContext&);
    friend CompiledExpression ConstantExpression(values::Value);

    std::shared_ptr<const detail::ExprProgram> m_program;
    values::ValueType m_type = values::ValueType::Bool;
};

/// Parses and type-checks `source`. The first error carries its span (byte offsets into `source`).
[[nodiscard]] std::expected<CompiledExpression, ExprError> CompileExpression(std::string_view source,
                                                                             const ExprEnvironment& environment);

/// Evaluates a valid expression. The result has the expression's Type().
[[nodiscard]] values::Value EvaluateExpression(const CompiledExpression& expression, const ExprContext& context);

/// A constant as an expression of its own type (bindings given as constants compile to this).
[[nodiscard]] CompiledExpression ConstantExpression(values::Value value);

} // namespace interi::chain
