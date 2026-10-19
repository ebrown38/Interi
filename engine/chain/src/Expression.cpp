// SPDX-License-Identifier: MIT
#include <interi/chain/Expression.h>
#include <interi/core/Assert.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace interi::chain
{

using values::Value;
using values::ValueType;

namespace detail
{

enum class Op : std::uint8_t
{
    Constant,
    Payload,
    ValueRead,
    Self,
    ToFloat,
    Negate,
    Not,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    And,
    Or,
    Member,
    Call,
    Min,
    Max,
    Abs,
    Clamp,
    Sqrt,
    Floor,
    ToInt,
    MakeVec3,
    Length,
    Dot,
};

struct ExprNode
{
    Op op = Op::Constant;
    ValueType type = ValueType::Bool;
    std::uint32_t a = 0; ///< First operand, or an index (constant, payload field, value id, function, component).
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    std::uint32_t firstArg = 0; ///< Call: into ExprProgram::args.
    std::uint32_t argCount = 0;
};

struct ExprProgram
{
    std::vector<ExprNode> nodes;
    std::vector<std::uint32_t> args;
    std::vector<Value> constants;
    std::vector<std::string> valueIds;
    std::vector<std::shared_ptr<const FunctionImpl>> functions;
    std::vector<std::string> valueReads; ///< Sorted, unique.
    std::uint32_t root = 0;
};

} // namespace detail

namespace
{

using detail::ExprNode;
using detail::ExprProgram;
using detail::Op;

// --- Lexer -------------------------------------------------------------------------------------------------------

enum class Tok : std::uint8_t
{
    Int,
    U32,
    Float,
    String,
    Name,
    LParen,
    RParen,
    Comma,
    Dot,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    AndAnd,
    OrOr,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    EqualEqual,
    NotEqual,
    End,
};

struct Token
{
    Tok kind = Tok::End;
    SourceSpan span;
    std::string text; ///< Name, or a string literal's decoded content.
    Value value;      ///< Number literals.
};

[[nodiscard]] SourceSpan Span(std::size_t begin, std::size_t end)
{
    return {.begin = static_cast<std::uint32_t>(begin), .end = static_cast<std::uint32_t>(end)};
}

[[nodiscard]] bool IsNameStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

[[nodiscard]] bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool IsNameChar(char c)
{
    return IsNameStart(c) || IsDigit(c);
}

std::expected<std::vector<Token>, ExprError> Lex(std::string_view source)
{
    std::vector<Token> tokens;
    std::size_t i = 0;
    const auto error = [&](std::string message, std::size_t begin, std::size_t end)
    { return std::unexpected(ExprError{.message = std::move(message), .span = Span(begin, end)}); };
    while (i < source.size())
    {
        const char c = source[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        Token token;
        if (IsDigit(c))
        {
            bool isFloat = false;
            while (i < source.size() && IsDigit(source[i]))
            {
                ++i;
            }
            if (i + 1 < source.size() && source[i] == '.' && IsDigit(source[i + 1]))
            {
                isFloat = true;
                ++i;
                while (i < source.size() && IsDigit(source[i]))
                {
                    ++i;
                }
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E'))
            {
                std::size_t j = i + 1;
                if (j < source.size() && (source[j] == '+' || source[j] == '-'))
                {
                    ++j;
                }
                if (j < source.size() && IsDigit(source[j]))
                {
                    isFloat = true;
                    i = j;
                    while (i < source.size() && IsDigit(source[i]))
                    {
                        ++i;
                    }
                }
            }
            const std::string_view digits = source.substr(begin, i - begin);
            if (isFloat)
            {
                float parsed = 0.0f;
                const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
                if (ec != std::errc{} || end != digits.data() + digits.size() || !std::isfinite(parsed))
                {
                    return error(std::format("invalid float literal '{}'", digits), begin, i);
                }
                token.kind = Tok::Float;
                token.value = parsed;
            }
            else
            {
                std::uint64_t parsed = 0;
                const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
                const bool isU32 = i < source.size() && source[i] == 'u';
                if (isU32)
                {
                    ++i;
                }
                const std::uint64_t limit = isU32
                                                ? std::numeric_limits<std::uint32_t>::max()
                                                : static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
                if (ec != std::errc{} || end != digits.data() + digits.size() || parsed > limit)
                {
                    return error(std::format("integer literal '{}' is out of range", source.substr(begin, i - begin)),
                                 begin, i);
                }
                token.kind = isU32 ? Tok::U32 : Tok::Int;
                token.value =
                    isU32 ? Value(static_cast<std::uint32_t>(parsed)) : Value(static_cast<std::int32_t>(parsed));
            }
            if (i < source.size() && IsNameChar(source[i]))
            {
                return error("unexpected character after a number", i, i + 1);
            }
        }
        else if (IsNameStart(c))
        {
            while (true)
            {
                while (i < source.size() && IsNameChar(source[i]))
                {
                    ++i;
                }
                // Dotted and qualified names ("payload.damage", "com.author.fps:Fn") are one token.
                if (i + 1 < source.size() && (source[i] == '.' || source[i] == ':') && IsNameStart(source[i + 1]))
                {
                    ++i;
                    continue;
                }
                break;
            }
            token.kind = Tok::Name;
            token.text = std::string(source.substr(begin, i - begin));
        }
        else if (c == '"')
        {
            ++i;
            std::string text;
            bool closed = false;
            while (i < source.size())
            {
                const char d = source[i++];
                if (d == '"')
                {
                    closed = true;
                    break;
                }
                if (d == '\\' && i < source.size() && (source[i] == '"' || source[i] == '\\'))
                {
                    text.push_back(source[i++]);
                    continue;
                }
                text.push_back(d);
            }
            if (!closed)
            {
                return error("unterminated string", begin, source.size());
            }
            token.kind = Tok::String;
            token.text = std::move(text);
        }
        else
        {
            const char next = i + 1 < source.size() ? source[i + 1] : '\0';
            const auto two = [&](Tok kind)
            {
                token.kind = kind;
                i += 2;
            };
            const auto one = [&](Tok kind)
            {
                token.kind = kind;
                i += 1;
            };
            switch (c)
            {
            case '(':
                one(Tok::LParen);
                break;
            case ')':
                one(Tok::RParen);
                break;
            case ',':
                one(Tok::Comma);
                break;
            case '.':
                one(Tok::Dot);
                break;
            case '+':
                one(Tok::Plus);
                break;
            case '-':
                one(Tok::Minus);
                break;
            case '*':
                one(Tok::Star);
                break;
            case '/':
                one(Tok::Slash);
                break;
            case '%':
                one(Tok::Percent);
                break;
            case '!':
                next == '=' ? two(Tok::NotEqual) : one(Tok::Bang);
                break;
            case '<':
                next == '=' ? two(Tok::LessEqual) : one(Tok::Less);
                break;
            case '>':
                next == '=' ? two(Tok::GreaterEqual) : one(Tok::Greater);
                break;
            case '=':
                if (next != '=')
                {
                    return error("'=' is not an operator; use '=='", begin, begin + 1);
                }
                two(Tok::EqualEqual);
                break;
            case '&':
                if (next != '&')
                {
                    return error("'&' is not an operator; use '&&'", begin, begin + 1);
                }
                two(Tok::AndAnd);
                break;
            case '|':
                if (next != '|')
                {
                    return error("'|' is not an operator; use '||'", begin, begin + 1);
                }
                two(Tok::OrOr);
                break;
            default:
                return error(std::format("unexpected character '{}'", c), begin, begin + 1);
            }
        }
        token.span = Span(begin, i);
        tokens.push_back(std::move(token));
    }
    tokens.push_back(Token{.kind = Tok::End, .span = Span(source.size(), source.size()), .text = {}, .value = {}});
    return tokens;
}

// --- Parser and type checker -------------------------------------------------------------------------------------

[[nodiscard]] bool IsNumber(ValueType type)
{
    return type == ValueType::Int || type == ValueType::U32 || type == ValueType::Float;
}

[[nodiscard]] std::string_view OpText(Tok kind)
{
    switch (kind)
    {
    case Tok::Plus:
        return "+";
    case Tok::Minus:
        return "-";
    case Tok::Star:
        return "*";
    case Tok::Slash:
        return "/";
    case Tok::Percent:
        return "%";
    case Tok::Less:
        return "<";
    case Tok::LessEqual:
        return "<=";
    case Tok::Greater:
        return ">";
    case Tok::GreaterEqual:
        return ">=";
    case Tok::EqualEqual:
        return "==";
    case Tok::NotEqual:
        return "!=";
    case Tok::AndAnd:
        return "&&";
    case Tok::OrOr:
        return "||";
    default:
        return "?";
    }
}

/// Binding power of a binary operator (0: not one).
[[nodiscard]] int Precedence(Tok kind)
{
    switch (kind)
    {
    case Tok::OrOr:
        return 1;
    case Tok::AndAnd:
        return 2;
    case Tok::EqualEqual:
    case Tok::NotEqual:
        return 3;
    case Tok::Less:
    case Tok::LessEqual:
    case Tok::Greater:
    case Tok::GreaterEqual:
        return 4;
    case Tok::Plus:
    case Tok::Minus:
        return 5;
    case Tok::Star:
    case Tok::Slash:
    case Tok::Percent:
        return 6;
    default:
        return 0;
    }
}

/// A parsed subexpression: its node and its extent in the source (including parentheses).
struct Parsed
{
    std::uint32_t node = 0;
    SourceSpan extent;
    /// The span errors about this subexpression point at (excludes enclosing parentheses).
    SourceSpan span;
};

class Parser
{
public:
    Parser(std::vector<Token> tokens, const ExprEnvironment& environment)
        : m_tokens(std::move(tokens))
        , m_environment(environment)
    {
    }

    std::expected<ExprProgram, ExprError> Run()
    {
        auto root = ParseBinary(1);
        if (!root)
        {
            return std::unexpected(std::move(root.error()));
        }
        if (Peek().kind != Tok::End)
        {
            return Fail("expected an operator or the end of the expression", Peek().span);
        }
        if (m_program.nodes.size() > kMaxNodes)
        {
            return Fail("the expression is too long", {.begin = 0, .end = m_tokens.back().span.end});
        }
        m_program.root = root->node;
        std::ranges::sort(m_program.valueReads);
        const auto duplicates = std::ranges::unique(m_program.valueReads);
        m_program.valueReads.erase(duplicates.begin(), duplicates.end());
        return std::move(m_program);
    }

private:
    using ParseResult = std::expected<Parsed, ExprError>;

    [[nodiscard]] const Token& Peek() const { return m_tokens[m_position]; }
    const Token& Next() { return m_tokens[m_position++]; }

    static std::unexpected<ExprError> Fail(std::string message, SourceSpan span)
    {
        return std::unexpected(ExprError{.message = std::move(message), .span = span});
    }

    [[nodiscard]] ValueType TypeOf(std::uint32_t node) const { return m_program.nodes[node].type; }

    std::uint32_t Add(ExprNode node)
    {
        m_program.nodes.push_back(node);
        return static_cast<std::uint32_t>(m_program.nodes.size() - 1);
    }

    std::uint32_t AddConstant(Value value)
    {
        const ValueType type = values::TypeOf(value);
        m_program.constants.push_back(std::move(value));
        return Add({.op = Op::Constant,
                    .type = type,
                    .a = static_cast<std::uint32_t>(m_program.constants.size() - 1),
                    .b = 0,
                    .c = 0,
                    .firstArg = 0,
                    .argCount = 0});
    }

    std::uint32_t Unary(Op op, ValueType type, std::uint32_t operand)
    {
        return Add({.op = op, .type = type, .a = operand, .b = 0, .c = 0, .firstArg = 0, .argCount = 0});
    }

    std::uint32_t Binary(Op op, ValueType type, std::uint32_t left, std::uint32_t right)
    {
        return Add({.op = op, .type = type, .a = left, .b = right, .c = 0, .firstArg = 0, .argCount = 0});
    }

    std::uint32_t PromoteToFloat(std::uint32_t node)
    {
        return TypeOf(node) == ValueType::Float ? node : Unary(Op::ToFloat, ValueType::Float, node);
    }

    /// Brings two numbers to one type (int or u32 with float -> float). False for int with u32.
    bool UnifyNumbers(std::uint32_t& left, std::uint32_t& right)
    {
        const ValueType lt = TypeOf(left);
        const ValueType rt = TypeOf(right);
        if (lt == rt)
        {
            return true;
        }
        if (lt == ValueType::Float || rt == ValueType::Float)
        {
            left = PromoteToFloat(left);
            right = PromoteToFloat(right);
            return true;
        }
        return false;
    }

    /// The evaluator recurses over the tree, so its size (and with it the depth of a left-leaning chain) is bounded.
    static constexpr std::size_t kMaxNodes = 2048;

    /// Nesting (parentheses, call arguments, unary operators) is bounded so hostile input can't exhaust the stack.
    static constexpr int kMaxNesting = 64;

    struct NestingGuard
    {
        explicit NestingGuard(int& depth) noexcept
            : m_depth(depth)
        {
            ++m_depth;
        }
        ~NestingGuard() { --m_depth; }
        NestingGuard(const NestingGuard&) = delete;
        NestingGuard& operator=(const NestingGuard&) = delete;
        int& m_depth;
    };

    ParseResult ParseBinary(int minPrecedence)
    {
        const NestingGuard guard(m_nesting);
        if (m_nesting > kMaxNesting)
        {
            return Fail("the expression is nested too deeply", Peek().span);
        }
        auto left = ParseUnary();
        if (!left)
        {
            return left;
        }
        while (true)
        {
            const Tok kind = Peek().kind;
            const int precedence = Precedence(kind);
            if (precedence == 0 || precedence < minPrecedence)
            {
                return left;
            }
            Next();
            auto right = ParseBinary(precedence + 1);
            if (!right)
            {
                return right;
            }
            const SourceSpan span{.begin = left->extent.begin, .end = right->extent.end};
            auto combined = Combine(kind, left->node, right->node, span);
            if (!combined)
            {
                return std::unexpected(std::move(combined.error()));
            }
            left = Parsed{.node = *combined, .extent = span, .span = span};
        }
    }

    std::expected<std::uint32_t, ExprError> Combine(Tok kind, std::uint32_t left, std::uint32_t right, SourceSpan span)
    {
        const ValueType lt = TypeOf(left);
        const ValueType rt = TypeOf(right);
        const auto mismatch = [&]
        {
            return Fail(std::format("operator '{}' cannot apply to {} and {}", OpText(kind), values::ToString(lt),
                                    values::ToString(rt)),
                        span);
        };
        switch (kind)
        {
        case Tok::AndAnd:
        case Tok::OrOr:
            if (lt != ValueType::Bool || rt != ValueType::Bool)
            {
                return mismatch();
            }
            return Binary(kind == Tok::AndAnd ? Op::And : Op::Or, ValueType::Bool, left, right);
        case Tok::EqualEqual:
        case Tok::NotEqual:
        {
            const Op op = kind == Tok::EqualEqual ? Op::Equal : Op::NotEqual;
            if (IsNumber(lt) && IsNumber(rt))
            {
                if (!UnifyNumbers(left, right))
                {
                    return mismatch();
                }
                return Binary(op, ValueType::Bool, left, right);
            }
            if (lt != rt)
            {
                return mismatch();
            }
            return Binary(op, ValueType::Bool, left, right);
        }
        case Tok::Less:
        case Tok::LessEqual:
        case Tok::Greater:
        case Tok::GreaterEqual:
        {
            if (!IsNumber(lt) || !IsNumber(rt) || !UnifyNumbers(left, right))
            {
                return mismatch();
            }
            const Op op = kind == Tok::Less        ? Op::Less
                          : kind == Tok::LessEqual ? Op::LessEqual
                          : kind == Tok::Greater   ? Op::Greater
                                                   : Op::GreaterEqual;
            return Binary(op, ValueType::Bool, left, right);
        }
        case Tok::Plus:
        case Tok::Minus:
        case Tok::Star:
        case Tok::Slash:
        case Tok::Percent:
        {
            const Op op = kind == Tok::Plus    ? Op::Add
                          : kind == Tok::Minus ? Op::Subtract
                          : kind == Tok::Star  ? Op::Multiply
                          : kind == Tok::Slash ? Op::Divide
                                               : Op::Modulo;
            if (IsNumber(lt) && IsNumber(rt))
            {
                if (!UnifyNumbers(left, right))
                {
                    return mismatch();
                }
                return Binary(op, TypeOf(left), left, right);
            }
            if (op == Op::Add && lt == ValueType::String && rt == ValueType::String)
            {
                return Binary(op, ValueType::String, left, right);
            }
            if ((op == Op::Add || op == Op::Subtract) && lt == ValueType::Vec3 && rt == ValueType::Vec3)
            {
                return Binary(op, ValueType::Vec3, left, right);
            }
            if ((op == Op::Multiply || op == Op::Divide) && lt == ValueType::Vec3 && IsNumber(rt))
            {
                return Binary(op, ValueType::Vec3, left, PromoteToFloat(right));
            }
            if (op == Op::Multiply && IsNumber(lt) && rt == ValueType::Vec3)
            {
                // Scalar first: evaluate as vec3 * scalar (multiplication commutes exactly in IEEE).
                return Binary(op, ValueType::Vec3, right, PromoteToFloat(left));
            }
            return mismatch();
        }
        default:
            return Fail("expected an operator", span);
        }
    }

    ParseResult ParseUnary()
    {
        const NestingGuard guard(m_nesting);
        if (m_nesting > kMaxNesting)
        {
            return Fail("the expression is nested too deeply", Peek().span);
        }
        const Token& token = Peek();
        if (token.kind == Tok::Minus || token.kind == Tok::Bang)
        {
            const Tok kind = token.kind;
            const std::uint32_t begin = token.span.begin;
            Next();
            auto operand = ParseUnary();
            if (!operand)
            {
                return operand;
            }
            const SourceSpan span{.begin = begin, .end = operand->extent.end};
            const ValueType type = TypeOf(operand->node);
            if (kind == Tok::Bang)
            {
                if (type != ValueType::Bool)
                {
                    return Fail(std::format("operator '!' cannot apply to {}", values::ToString(type)), span);
                }
                return Parsed{.node = Unary(Op::Not, ValueType::Bool, operand->node), .extent = span, .span = span};
            }
            if (!IsNumber(type) && type != ValueType::Vec3)
            {
                return Fail(std::format("operator '-' cannot apply to {}", values::ToString(type)), span);
            }
            return Parsed{.node = Unary(Op::Negate, type, operand->node), .extent = span, .span = span};
        }
        return ParsePostfix();
    }

    ParseResult ParsePostfix()
    {
        auto primary = ParsePrimary();
        if (!primary)
        {
            return primary;
        }
        while (Peek().kind == Tok::Dot)
        {
            Next();
            const Token& member = Next();
            if (member.kind != Tok::Name)
            {
                return Fail("expected a member name after '.'", member.span);
            }
            const SourceSpan span{.begin = primary->extent.begin, .end = member.span.end};
            // A dotted name after '.' ("(v).x.y") lexes as one token; apply each segment.
            std::string_view rest = member.text;
            while (!rest.empty())
            {
                const std::size_t dot = rest.find('.');
                const std::string_view segment = rest.substr(0, dot);
                auto applied = ApplyMember(primary->node, segment, span);
                if (!applied)
                {
                    return std::unexpected(std::move(applied.error()));
                }
                primary->node = *applied;
                rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
            }
            primary->extent = span;
            primary->span = span;
        }
        return primary;
    }

    std::expected<std::uint32_t, ExprError> ApplyMember(std::uint32_t node, std::string_view member, SourceSpan span)
    {
        const ValueType type = TypeOf(node);
        if (type != ValueType::Vec3 || member.size() != 1 || (member[0] != 'x' && member[0] != 'y' && member[0] != 'z'))
        {
            return Fail(std::format("{} has no member '{}'", values::ToString(type), member), span);
        }
        return Add({.op = Op::Member,
                    .type = ValueType::Float,
                    .a = node,
                    .b = static_cast<std::uint32_t>(member[0] - 'x'),
                    .c = 0,
                    .firstArg = 0,
                    .argCount = 0});
    }

    ParseResult ParsePrimary()
    {
        const Token& token = Next();
        const auto leaf = [&](std::uint32_t node)
        { return Parsed{.node = node, .extent = token.span, .span = token.span}; };
        switch (token.kind)
        {
        case Tok::Int:
        case Tok::U32:
        case Tok::Float:
            return leaf(AddConstant(token.value));
        case Tok::String:
            return leaf(AddConstant(Value(token.text)));
        case Tok::LParen:
        {
            auto inner = ParseBinary(1);
            if (!inner)
            {
                return inner;
            }
            const Token& close = Next();
            if (close.kind != Tok::RParen)
            {
                return Fail("expected ')'", close.span);
            }
            return Parsed{.node = inner->node,
                          .extent = SourceSpan{.begin = token.span.begin, .end = close.span.end},
                          .span = inner->span};
        }
        case Tok::Name:
            return ParseName(token);
        default:
            return Fail("expected an operand", token.span);
        }
    }

    ParseResult ParseName(const Token& token)
    {
        const std::string& name = token.text;
        if (Peek().kind == Tok::LParen)
        {
            return ParseCall(token);
        }
        const auto leaf = [&](std::uint32_t node)
        { return Parsed{.node = node, .extent = token.span, .span = token.span}; };
        if (name == "true" || name == "false")
        {
            return leaf(AddConstant(Value(name == "true")));
        }
        if (name == "self")
        {
            return leaf(Add(
                {.op = Op::Self, .type = ValueType::EntityRef, .a = 0, .b = 0, .c = 0, .firstArg = 0, .argCount = 0}));
        }
        constexpr std::string_view kPayload = "payload.";
        if (name.starts_with(kPayload))
        {
            std::vector<std::string_view> segments;
            for (std::string_view rest = std::string_view(name).substr(kPayload.size());;)
            {
                const std::size_t dot = rest.find('.');
                segments.push_back(rest.substr(0, dot));
                if (dot == std::string_view::npos)
                {
                    break;
                }
                rest = rest.substr(dot + 1);
            }
            const std::string_view field = segments.front();
            const auto it =
                std::ranges::find_if(m_environment.payload, [&](const PayloadField& f) { return f.name == field; });
            if (it == m_environment.payload.end())
            {
                return Fail(std::format("the payload has no field '{}'", field), token.span);
            }
            std::uint32_t node = Add({.op = Op::Payload,
                                      .type = it->type,
                                      .a = static_cast<std::uint32_t>(it - m_environment.payload.begin()),
                                      .b = 0,
                                      .c = 0,
                                      .firstArg = 0,
                                      .argCount = 0});
            for (std::size_t i = 1; i < segments.size(); ++i)
            {
                auto applied = ApplyMember(node, segments[i], token.span);
                if (!applied)
                {
                    return std::unexpected(std::move(applied.error()));
                }
                node = *applied;
            }
            return leaf(node);
        }
        if (name == "payload")
        {
            return Fail("expected 'payload.<field>'", token.span);
        }
        return Fail(std::format("unknown name '{}'", name), token.span);
    }

    /// Parses "(arg, ...)" after a call name.
    std::expected<std::pair<std::vector<Parsed>, SourceSpan>, ExprError> ParseArguments(const Token& name)
    {
        Next(); // (
        std::vector<Parsed> arguments;
        if (Peek().kind != Tok::RParen)
        {
            while (true)
            {
                auto argument = ParseBinary(1);
                if (!argument)
                {
                    return std::unexpected(std::move(argument.error()));
                }
                arguments.push_back(*argument);
                if (Peek().kind != Tok::Comma)
                {
                    break;
                }
                Next();
            }
        }
        const Token& close = Next();
        if (close.kind != Tok::RParen)
        {
            return Fail("expected ',' or ')'", close.span);
        }
        return std::pair{std::move(arguments), SourceSpan{.begin = name.span.begin, .end = close.span.end}};
    }

    ParseResult ParseCall(const Token& name)
    {
        // value("<id>") takes a string literal, not an expression.
        if (name.text == "value")
        {
            Next(); // (
            const Token& id = Next();
            if (id.kind != Tok::String)
            {
                return Fail("value() takes a Value id as a string literal", id.span);
            }
            const Token& close = Next();
            if (close.kind != Tok::RParen)
            {
                return Fail("expected ')'", close.span);
            }
            const SourceSpan span{.begin = name.span.begin, .end = close.span.end};
            const values::ValueDefinition* definition =
                m_environment.values != nullptr ? m_environment.values->Definition(id.text) : nullptr;
            if (definition == nullptr)
            {
                return Fail(std::format("no Value '{}'", id.text), span);
            }
            m_program.valueIds.push_back(id.text);
            m_program.valueReads.push_back(id.text);
            const std::uint32_t node = Add({.op = Op::ValueRead,
                                            .type = definition->schema.type,
                                            .a = static_cast<std::uint32_t>(m_program.valueIds.size() - 1),
                                            .b = 0,
                                            .c = 0,
                                            .firstArg = 0,
                                            .argCount = 0});
            return Parsed{.node = node, .extent = span, .span = span};
        }

        auto parsed = ParseArguments(name);
        if (!parsed)
        {
            return std::unexpected(std::move(parsed.error()));
        }
        auto& [arguments, span] = *parsed;
        auto node = Builtin(name.text, arguments, span);
        if (!node)
        {
            return std::unexpected(std::move(node.error()));
        }
        if (*node == kNotBuiltin)
        {
            node = ModuleFunction(name, arguments, span);
            if (!node)
            {
                return std::unexpected(std::move(node.error()));
            }
        }
        return Parsed{.node = *node, .extent = span, .span = span};
    }

    static constexpr std::uint32_t kNotBuiltin = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t AddCall(Op op, ValueType type, std::span<const std::uint32_t> arguments, std::uint32_t index = 0)
    {
        const auto first = static_cast<std::uint32_t>(m_program.args.size());
        m_program.args.insert(m_program.args.end(), arguments.begin(), arguments.end());
        return Add({.op = op,
                    .type = type,
                    .a = index,
                    .b = 0,
                    .c = 0,
                    .firstArg = first,
                    .argCount = static_cast<std::uint32_t>(arguments.size())});
    }

    std::expected<std::uint32_t, ExprError> Builtin(std::string_view name, const std::vector<Parsed>& arguments,
                                                    SourceSpan span)
    {
        struct Entry
        {
            std::string_view name;
            Op op;
            std::size_t arity;
        };
        static constexpr Entry kBuiltins[] = {
            {"min", Op::Min, 2},       {"max", Op::Max, 2},       {"abs", Op::Abs, 1},   {"clamp", Op::Clamp, 3},
            {"sqrt", Op::Sqrt, 1},     {"floor", Op::Floor, 1},   {"int", Op::ToInt, 1}, {"float", Op::ToFloat, 1},
            {"vec3", Op::MakeVec3, 3}, {"length", Op::Length, 1}, {"dot", Op::Dot, 2},
        };
        const auto* const entry = std::ranges::find(kBuiltins, name, &Entry::name);
        if (entry == std::end(kBuiltins))
        {
            return kNotBuiltin;
        }
        if (arguments.size() != entry->arity)
        {
            return Fail(std::format("{}() takes {} argument{}, not {}", name, entry->arity,
                                    entry->arity == 1 ? "" : "s", arguments.size()),
                        span);
        }
        std::vector<std::uint32_t> nodes(arguments.size());
        std::ranges::transform(arguments, nodes.begin(), &Parsed::node);
        const auto badArgument = [&](std::size_t index)
        {
            return Fail(std::format("{}() can't take {}", name, values::ToString(TypeOf(nodes[index]))),
                        arguments[index].span);
        };
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const ValueType type = TypeOf(nodes[i]);
            const bool wantsVec3 = entry->op == Op::Length || entry->op == Op::Dot;
            const bool ok = wantsVec3 ? type == ValueType::Vec3
                                      : IsNumber(type) || (entry->op == Op::ToInt && type == ValueType::Bool);
            if (!ok)
            {
                return badArgument(i);
            }
        }
        switch (entry->op)
        {
        case Op::Min:
        case Op::Max:
        case Op::Clamp:
        {
            // One common type: float if any argument is.
            const bool anyFloat =
                std::ranges::any_of(nodes, [&](std::uint32_t node) { return TypeOf(node) == ValueType::Float; });
            for (std::size_t i = 0; i < nodes.size(); ++i)
            {
                if (anyFloat)
                {
                    nodes[i] = PromoteToFloat(nodes[i]);
                }
                else if (TypeOf(nodes[i]) != TypeOf(nodes[0]))
                {
                    return Fail(std::format("{}() can't mix int and u32", name), span);
                }
            }
            return AddCall(entry->op, TypeOf(nodes[0]), nodes);
        }
        case Op::Abs:
            return AddCall(entry->op, TypeOf(nodes[0]), nodes);
        case Op::Sqrt:
        case Op::Floor:
            nodes[0] = PromoteToFloat(nodes[0]);
            return AddCall(entry->op, ValueType::Float, nodes);
        case Op::ToFloat:
            return PromoteToFloat(nodes[0]);
        case Op::ToInt:
            return TypeOf(nodes[0]) == ValueType::Int ? nodes[0] : Unary(Op::ToInt, ValueType::Int, nodes[0]);
        case Op::MakeVec3:
            for (std::uint32_t& node : nodes)
            {
                node = PromoteToFloat(node);
            }
            return AddCall(entry->op, ValueType::Vec3, nodes);
        case Op::Length:
        case Op::Dot:
            return AddCall(entry->op, ValueType::Float, nodes);
        default:
            return kNotBuiltin;
        }
    }

    std::expected<std::uint32_t, ExprError> ModuleFunction(const Token& name, const std::vector<Parsed>& arguments,
                                                           SourceSpan span)
    {
        const FunctionDefinition* definition =
            m_environment.functions != nullptr ? m_environment.functions->Resolve(name.text) : nullptr;
        if (definition == nullptr)
        {
            return Fail(std::format("unknown function '{}'", name.text), name.span);
        }
        if (arguments.size() != definition->parameters.size())
        {
            return Fail(std::format("{}() takes {} argument{}, not {}", name.text, definition->parameters.size(),
                                    definition->parameters.size() == 1 ? "" : "s", arguments.size()),
                        span);
        }
        std::vector<std::uint32_t> nodes;
        nodes.reserve(arguments.size());
        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            std::uint32_t node = arguments[i].node;
            const ValueType want = definition->parameters[i];
            if (TypeOf(node) != want)
            {
                if (want == ValueType::Float && IsNumber(TypeOf(node)))
                {
                    node = PromoteToFloat(node);
                }
                else
                {
                    return Fail(std::format("argument {} of {}() must be {}, not {}", i + 1, name.text,
                                            values::ToString(want), values::ToString(TypeOf(node))),
                                arguments[i].span);
                }
            }
            nodes.push_back(node);
        }
        m_program.functions.push_back(definition->implementation);
        return AddCall(Op::Call, definition->result, nodes, static_cast<std::uint32_t>(m_program.functions.size() - 1));
    }

    std::vector<Token> m_tokens;
    std::size_t m_position = 0;
    int m_nesting = 0;
    const ExprEnvironment& m_environment;
    ExprProgram m_program;
};

// --- Evaluation --------------------------------------------------------------------------------------------------

[[nodiscard]] std::int32_t WrapInt(std::uint32_t bits)
{
    return static_cast<std::int32_t>(bits); // modular since C++20
}

[[nodiscard]] std::int32_t IntDivide(std::int32_t a, std::int32_t b)
{
    if (b == 0)
    {
        return 0;
    }
    if (b == -1)
    {
        return WrapInt(0u - static_cast<std::uint32_t>(a));
    }
    return a / b;
}

[[nodiscard]] std::int32_t IntModulo(std::int32_t a, std::int32_t b)
{
    return b == 0 || b == -1 ? 0 : a % b;
}

[[nodiscard]] std::int32_t SaturatingInt(float value)
{
    if (std::isnan(value))
    {
        return 0;
    }
    if (value >= 2147483648.0f)
    {
        return std::numeric_limits<std::int32_t>::max();
    }
    if (value < -2147483648.0f)
    {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(value);
}

template <typename T>
[[nodiscard]] T MinOf(T a, T b)
{
    return b < a ? b : a;
}

template <typename T>
[[nodiscard]] T MaxOf(T a, T b)
{
    return a < b ? b : a;
}

class Evaluator
{
public:
    Evaluator(const ExprProgram& program, const ExprContext& context)
        : m_program(program)
        , m_context(context)
    {
    }

    [[nodiscard]] Value Eval(std::uint32_t index) const
    {
        const ExprNode& node = m_program.nodes[index];
        switch (node.op)
        {
        case Op::Constant:
            return m_program.constants[node.a];
        case Op::Payload:
            if (node.a < m_context.payload.size() && values::TypeOf(m_context.payload[node.a]) == node.type)
            {
                return m_context.payload[node.a];
            }
            return values::ZeroValue(node.type);
        case Op::ValueRead:
        {
            const Value* value =
                m_context.values != nullptr ? m_context.values->Read(m_program.valueIds[node.a]) : nullptr;
            return value != nullptr && values::TypeOf(*value) == node.type ? *value : values::ZeroValue(node.type);
        }
        case Op::Self:
            return values::EntityRef{.id = m_context.self};
        case Op::ToFloat:
            return ToFloat(Eval(node.a));
        case Op::ToInt:
            return ToInt(Eval(node.a));
        case Op::Negate:
            return Negate(Eval(node.a));
        case Op::Not:
            return !std::get<bool>(Eval(node.a));
        case Op::And:
            return std::get<bool>(Eval(node.a)) && std::get<bool>(Eval(node.b));
        case Op::Or:
            return std::get<bool>(Eval(node.a)) || std::get<bool>(Eval(node.b));
        case Op::Add:
        case Op::Subtract:
        case Op::Multiply:
        case Op::Divide:
        case Op::Modulo:
            return Arithmetic(node.op, Eval(node.a), Eval(node.b));
        case Op::Less:
        case Op::LessEqual:
        case Op::Greater:
        case Op::GreaterEqual:
            return Compare(node.op, Eval(node.a), Eval(node.b));
        case Op::Equal:
            return Eval(node.a) == Eval(node.b);
        case Op::NotEqual:
            return !(Eval(node.a) == Eval(node.b));
        case Op::Member:
        {
            const Vec3 v = std::get<Vec3>(Eval(node.a));
            return node.b == 0 ? v.x : node.b == 1 ? v.y : v.z;
        }
        case Op::Call:
        {
            std::vector<Value> arguments = Arguments(node);
            const Value result = (*m_program.functions[node.a])(arguments);
            return values::TypeOf(result) == node.type ? result : values::ZeroValue(node.type);
        }
        case Op::Min:
        case Op::Max:
        case Op::Abs:
        case Op::Clamp:
        case Op::Sqrt:
        case Op::Floor:
        case Op::MakeVec3:
        case Op::Length:
        case Op::Dot:
            return CallBuiltin(node.op, Arguments(node));
        }
        return Value{};
    }

private:
    [[nodiscard]] std::vector<Value> Arguments(const ExprNode& node) const
    {
        std::vector<Value> arguments;
        arguments.reserve(node.argCount);
        for (std::uint32_t i = 0; i < node.argCount; ++i)
        {
            arguments.push_back(Eval(m_program.args[node.firstArg + i]));
        }
        return arguments;
    }

    static Value ToFloat(const Value& value)
    {
        if (const auto* i = std::get_if<std::int32_t>(&value.Variant()))
        {
            return static_cast<float>(*i);
        }
        if (const auto* u = std::get_if<std::uint32_t>(&value.Variant()))
        {
            return static_cast<float>(*u);
        }
        return std::get<float>(value);
    }

    static Value ToInt(const Value& value)
    {
        if (const auto* f = std::get_if<float>(&value.Variant()))
        {
            return SaturatingInt(*f);
        }
        if (const auto* u = std::get_if<std::uint32_t>(&value.Variant()))
        {
            return static_cast<std::int32_t>(MinOf<std::uint32_t>(*u, std::numeric_limits<std::int32_t>::max()));
        }
        if (const auto* b = std::get_if<bool>(&value.Variant()))
        {
            return static_cast<std::int32_t>(*b ? 1 : 0);
        }
        return std::get<std::int32_t>(value);
    }

    static Value Negate(const Value& value)
    {
        return std::visit(
            [](const auto& v) -> Value
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::int32_t>)
                {
                    return WrapInt(0u - static_cast<std::uint32_t>(v));
                }
                else if constexpr (std::is_same_v<T, std::uint32_t>)
                {
                    return static_cast<std::uint32_t>(0u - v);
                }
                else if constexpr (std::is_same_v<T, float>)
                {
                    return -v;
                }
                else if constexpr (std::is_same_v<T, Vec3>)
                {
                    return Vec3(-v.x, -v.y, -v.z);
                }
                else
                {
                    return Value{};
                }
            },
            value.Variant());
    }

    static Value Arithmetic(Op op, const Value& a, const Value& b)
    {
        if (const auto* x = std::get_if<std::int32_t>(&a.Variant()))
        {
            const std::int32_t y = std::get<std::int32_t>(b);
            const auto ux = static_cast<std::uint32_t>(*x);
            const auto uy = static_cast<std::uint32_t>(y);
            switch (op)
            {
            case Op::Add:
                return WrapInt(ux + uy);
            case Op::Subtract:
                return WrapInt(ux - uy);
            case Op::Multiply:
                return WrapInt(ux * uy);
            case Op::Divide:
                return IntDivide(*x, y);
            default:
                return IntModulo(*x, y);
            }
        }
        if (const auto* x = std::get_if<std::uint32_t>(&a.Variant()))
        {
            const std::uint32_t y = std::get<std::uint32_t>(b);
            switch (op)
            {
            case Op::Add:
                return static_cast<std::uint32_t>(*x + y);
            case Op::Subtract:
                return static_cast<std::uint32_t>(*x - y);
            case Op::Multiply:
                return static_cast<std::uint32_t>(*x * y);
            case Op::Divide:
                return y == 0 ? 0u : *x / y;
            default:
                return y == 0 ? 0u : *x % y;
            }
        }
        if (const auto* x = std::get_if<float>(&a.Variant()))
        {
            const float y = std::get<float>(b);
            switch (op)
            {
            case Op::Add:
                return *x + y;
            case Op::Subtract:
                return *x - y;
            case Op::Multiply:
                return *x * y;
            case Op::Divide:
                return *x / y;
            default:
                return std::fmod(*x, y);
            }
        }
        if (const auto* s = std::get_if<std::string>(&a.Variant()))
        {
            return *s + std::get<std::string>(b);
        }
        const Vec3 v = std::get<Vec3>(a);
        if (const auto* w = std::get_if<Vec3>(&b.Variant()))
        {
            return op == Op::Add ? Vec3(v.x + w->x, v.y + w->y, v.z + w->z) : Vec3(v.x - w->x, v.y - w->y, v.z - w->z);
        }
        const float s = std::get<float>(b);
        return op == Op::Multiply ? Vec3(v.x * s, v.y * s, v.z * s) : Vec3(v.x / s, v.y / s, v.z / s);
    }

    static Value Compare(Op op, const Value& a, const Value& b)
    {
        return std::visit(
            [&](const auto& x) -> Value
            {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> ||
                              std::is_same_v<T, float>)
                {
                    const T y = std::get<T>(b);
                    switch (op)
                    {
                    case Op::Less:
                        return x < y;
                    case Op::LessEqual:
                        return x <= y;
                    case Op::Greater:
                        return x > y;
                    default:
                        return x >= y;
                    }
                }
                else
                {
                    return false;
                }
            },
            a.Variant());
    }

    static Value CallBuiltin(Op op, const std::vector<Value>& arguments)
    {
        switch (op)
        {
        case Op::Sqrt:
            return std::sqrt(std::get<float>(arguments[0]));
        case Op::Floor:
            return std::floor(std::get<float>(arguments[0]));
        case Op::MakeVec3:
            return Vec3(std::get<float>(arguments[0]), std::get<float>(arguments[1]), std::get<float>(arguments[2]));
        case Op::Length:
        {
            const Vec3 v = std::get<Vec3>(arguments[0]);
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
        case Op::Dot:
        {
            const Vec3 v = std::get<Vec3>(arguments[0]);
            const Vec3 w = std::get<Vec3>(arguments[1]);
            return v.x * w.x + v.y * w.y + v.z * w.z;
        }
        default:
            break;
        }
        return std::visit(
            [&](const auto& x) -> Value
            {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> ||
                              std::is_same_v<T, float>)
                {
                    switch (op)
                    {
                    case Op::Min:
                        return MinOf(x, std::get<T>(arguments[1]));
                    case Op::Max:
                        return MaxOf(x, std::get<T>(arguments[1]));
                    case Op::Clamp:
                        return MinOf(MaxOf(x, std::get<T>(arguments[1])), std::get<T>(arguments[2]));
                    default: // Abs
                        if constexpr (std::is_same_v<T, std::int32_t>)
                        {
                            return x < 0 ? WrapInt(0u - static_cast<std::uint32_t>(x)) : x;
                        }
                        else if constexpr (std::is_same_v<T, float>)
                        {
                            return std::fabs(x);
                        }
                        else
                        {
                            return x;
                        }
                    }
                }
                else
                {
                    return Value{};
                }
            },
            arguments[0].Variant());
    }

    const ExprProgram& m_program;
    const ExprContext& m_context;
};

} // namespace

std::span<const std::string> CompiledExpression::ValueReads() const noexcept
{
    return m_program != nullptr ? std::span<const std::string>(m_program->valueReads) : std::span<const std::string>{};
}

std::expected<CompiledExpression, ExprError> CompileExpression(std::string_view source,
                                                               const ExprEnvironment& environment)
{
    auto tokens = Lex(source);
    if (!tokens)
    {
        return std::unexpected(std::move(tokens.error()));
    }
    auto program = Parser(std::move(*tokens), environment).Run();
    if (!program)
    {
        return std::unexpected(std::move(program.error()));
    }
    CompiledExpression compiled;
    compiled.m_type = program->nodes[program->root].type;
    compiled.m_program = std::make_shared<const ExprProgram>(std::move(*program));
    return compiled;
}

Value EvaluateExpression(const CompiledExpression& expression, const ExprContext& context)
{
    INTERI_ASSERT(expression.Valid(), "evaluating an invalid expression");
    if (!expression.Valid())
    {
        return Value{};
    }
    return Evaluator(*expression.m_program, context).Eval(expression.m_program->root);
}

CompiledExpression ConstantExpression(Value value)
{
    ExprProgram program;
    const ValueType type = values::TypeOf(value);
    program.constants.push_back(std::move(value));
    program.nodes.push_back({.op = Op::Constant, .type = type, .a = 0, .b = 0, .c = 0, .firstArg = 0, .argCount = 0});
    CompiledExpression compiled;
    compiled.m_type = type;
    compiled.m_program = std::make_shared<const ExprProgram>(std::move(program));
    return compiled;
}

} // namespace interi::chain
