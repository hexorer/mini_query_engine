#include <print>
#include <memory>
#include <expected>

#include "parsi/parsi.hpp"

namespace { // ------------------------ utils ------------------------
template <typename T>
using UPtr = std::unique_ptr<T>;

template <typename ...Ts>
using Union = std::variant<Ts...>;
} // ------------------------ utils ------------------------

namespace { // ------------------------ parsers ------------------------

static constexpr parsi::Charset charset_whitespaces{" \n\t"};
static constexpr parsi::CharRange charrange_digits{'0', '9'};
static constexpr parsi::CharRange charrange_lower_alphabet{'a', 'z'};
static constexpr parsi::CharRange charrange_upper_alphabet{'A', 'Z'};

static constexpr auto parser_whitespace = parsi::expect(charset_whitespaces);
static constexpr auto parser_digit = parsi::expect(charrange_digits);
static constexpr auto parser_whitespaces = parsi::repeat(parser_whitespace);
static constexpr auto parser_digits = parsi::repeat<1>(parser_digit);
static constexpr auto parser_lower_alphabet = parsi::expect(charrange_lower_alphabet);
static constexpr auto parser_upper_alphabet = parsi::expect(charrange_upper_alphabet);

template <typename JoinF, typename ItemF>
static constexpr auto make_parser_joined(JoinF&& join_parser, ItemF&& item_parser) {
    return parsi::sequence(
        item_parser,
        parsi::repeat(parsi::sequence(std::move(join_parser), item_parser))
    );
}

class LateRef {
    std::shared_ptr<parsi::RTParser> _parser = std::make_shared<parsi::RTParser>([](parsi::Stream stream) { return parsi::Result{stream, false}; });
public:
    parsi::Result operator()(parsi::Stream stream) const {
        return (*_parser)(stream);
    }

    template <typename ParserT>
    void set(ParserT&& parser) && {
        static_assert(!std::same_as<std::remove_cvref_t<ParserT>, LateRef>);
        *_parser = std::forward<ParserT>(parser);
    }
};

template <typename ParserT>
struct Peek {
    ParserT parser;
    constexpr parsi::Result operator()(parsi::Stream stream) const {
        return parsi::Result{stream, parser(stream).is_valid()};
    }
};

template <typename CondParserT, typename SuccParserT>
struct OptCont {
    CondParserT cond_parser;
    SuccParserT succ_parser;
    constexpr parsi::Result operator()(parsi::Stream stream) const {
        parsi::Result result = cond_parser(stream);
        if (!result) {
            return parsi::Result{stream, true};
        }
        return succ_parser(result.stream());
    }
};

template <typename ...OptContTs>
struct OptContSet {
    static_assert(sizeof...(OptContTs) == 0, "all types must be an instantiation of OptCont.");
    static_assert(sizeof...(OptContTs) != 0, "all types must be an instantiation of OptCont.");
};

template <>
struct OptContSet<> {
    constexpr parsi::Result operator()(parsi::Stream stream) const {
        return parsi::Result{stream, false};
    }
};

template <typename T1, typename U1, typename ...RestTs>
struct OptContSet<OptCont<T1, U1>, RestTs...> {
    OptCont<T1, U1> parser;
    OptContSet<RestTs...> rest;

    constexpr parsi::Result operator()(parsi::Stream stream) const {
        parsi::Result result = parser.cond_parser(stream);
        if (!result) {
            return rest(stream);
        }
        return parser.succ_parser(result.stream());
    }
};

template <typename ParserT, typename ...ParserTs>
static constexpr auto opt_cont_set(ParserT&& parser, ParserTs&& ...parsers) {
    if constexpr (sizeof...(parsers) == 0) {
        return OptContSet<ParserT>{
            .parser = std::forward<ParserT>(parser),
            .rest = OptContSet<>{},
        };
    } else {
        return OptContSet<ParserT, ParserTs...>{
            .parser = std::forward<ParserT>(parser),
            .rest = opt_cont_set(std::forward<ParserTs>(parsers)...),
        };
    }
}

} // namespace ------------------------ parsers ------------------------

namespace { // ------------------------ ast ------------------------

template <typename T>
struct Parsed {
    T value;
    parsi::Stream stream;
};

struct Failed {
    parsi::Result result;
};

template <typename T>
using ParserResult = std::expected<Parsed<T>, Failed>;

struct ASTNumber {
    std::uint64_t number = 0;
    std::uint64_t fraction = 0;
    bool is_negative = false;
    bool has_fraction = false;

    static ParserResult<ASTNumber> parse(parsi::Stream stream);
    std::string stringify() const;
};

enum class BinaryOp {
    mul,
    div,
    add,
    sub,
    pow,
};

struct ASTExpr;

struct ASTBinOpExpr {
    UPtr<ASTExpr> lhs;
    BinaryOp op;
    UPtr<ASTExpr> rhs;

    static ParserResult<ASTBinOpExpr> parse(parsi::Stream stream);
    std::string stringify() const;
};

struct ASTExpr {
    Union<UPtr<ASTNumber>, UPtr<ASTBinOpExpr>, UPtr<ASTExpr>> subexpr;

    static ParserResult<ASTExpr> parse(parsi::Stream stream);
    std::string stringify() const;
};

ParserResult<ASTNumber> ASTNumber::parse(parsi::Stream stream) {
    ASTNumber ret{};
    static constexpr auto strview_to_ull = [](std::string_view str) {
        std::uint64_t ret = 0;
        for (const char chr : str) {
            ret *= 10;
            ret += static_cast<std::uint64_t>(chr - '0');
        }
        return ret;
    };
    auto parser_number_sign = parsi::anyof(
        parsi::extract(parsi::expect('-'), [&ret](auto&&) { ret.is_negative = true; }),
        parsi::extract(parsi::expect('+'), [&ret](auto&&) { ret.is_negative = false; })
    );
    auto parser_number = parsi::sequence(
        parsi::optional(parser_number_sign),
        parsi::extract(parser_digits, [&ret](std::string_view str) { ret.number = strview_to_ull(str); }),
        OptCont{
            parsi::expect('.'),
            parsi::extract(parser_digits, [&ret](std::string_view str) { ret.fraction = strview_to_ull(str); })
        }
    );
    parsi::Result result = parser_number(stream);
    if (!result) {
        return std::unexpected(Failed{result});
    }
    return Parsed{ret, result.stream()};
}

ParserResult<ASTBinOpExpr> ASTBinOpExpr::parse(parsi::Stream stream)
{
    // TODO
}

ParserResult<ASTExpr> ASTExpr::parse(parsi::Stream stream)
{
    // TODO
}

std::string ASTNumber::stringify() const {
    if (has_fraction) {
        std::size_t frac = fraction;
        while (frac % 10 == 0) {
            frac = frac / 10;
        }
        return std::format("{}{}.{}", is_negative ? "-" : "", number, frac);
    }
    return std::format("{}{}", is_negative ? "-" : "", number);
}

std::string ASTBinOpExpr::stringify() const {
    const std::string_view op_str = [this]() {
        switch (op) {
            case BinaryOp::mul: return "*";
            case BinaryOp::div: return "/";
            case BinaryOp::add: return "+";
            case BinaryOp::sub: return "-";
        }
        return "";
    }();
    return std::format("{} {} {}", lhs->stringify(), op_str, rhs->stringify());
}

std::string ASTExpr::stringify() const {
    return std::visit([](auto&& ast_subexpr) { return ast_subexpr->stringify(); }, subexpr);
}

class RTAST {
    struct VTable {
        void*(*copy_constructor)(void*) = nullptr;
        void(*copy_assign_oper)(void*, void*) = nullptr;
        void(*destructor)(void*) = nullptr;
        std::string(*stringify)(const void*) = nullptr;
    };

public:
    template <typename T> requires (!std::same_as<T, RTAST>)
    RTAST(T&& object)
        : _object(new T(std::forward<T>(object)))
        , _vtable(vtable_for<std::remove_cvref_t<T>>)
    {
    }
    RTAST(const RTAST& other) : _object(nullptr), _vtable(other._vtable) {
        _object = _vtable.copy_constructor(other._object);
    }
    RTAST& operator=(const RTAST& other) {
        if (&other == this) {
            return *this;
        }
        _vtable.copy_assign_oper(_object, other._object);
        return *this;
    }
    RTAST(RTAST&& other) noexcept : _object(std::exchange(other._object, nullptr)), _vtable(other._vtable) {}
    RTAST& operator=(RTAST&& other) noexcept {
        std::swap(_object, other._object);
        return *this;
    }
    ~RTAST() {
        if (_object) {
            _vtable.destructor(_object);
        }
    }

    std::string stringify() const {
        return _vtable.stringify(_object);
    }

private:
    template <typename T>
    inline static constexpr VTable vtable_for{
        .copy_constructor = [](void* other_ptr) -> void* {
            return new T(*static_cast<T*>(other_ptr));
        },
        .copy_assign_oper = [](void* ptr, void* other_ptr) {
            *static_cast<T*>(ptr) = *static_cast<T*>(other_ptr);
        },
        .destructor = [](void* ptr) {
            static_cast<T*>(ptr)->~T();
        },
    };

    void* _object;
    VTable _vtable;
};
} // namespace ------------------------ ast ------------------------

template <typename ...>
struct print;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::println("Usage:\n\t{} <string>", argv[0]);
        return 1;
    }

    // std::size_t index = 0;
    // auto parser_visited_number = parsi::extract(parser_digits, [&index](std::string_view str) { std::println("Number[{}] => {}", index++, str); });

    // auto parser_comma_separed_numbers = make_parser_joined(
    //     parsi::sequence(
    //         parser_whitespaces,
    //         parsi::expect(','),
    //         parser_whitespaces
    //     ),
    //     parsi::sequence(
    //         parser_whitespaces,
    //         parser_visited_number,
    //         parser_whitespaces
    //     )
    // );

    // auto parser_list_numbers = parsi::sequence(
    //     parser_whitespaces,
    //     parsi::expect('['),
    //     parser_whitespaces,
    //     parsi::optional(parser_comma_separed_numbers),
    //     parser_whitespaces,
    //     parsi::expect(']'),
    //     parser_whitespaces
    // );

    // TODO make a calculator

    // struct ASTNothing {
    //     std::string stringify() const { return ""; }
    // };

    auto parser_operator = parsi::anyof(
        parsi::extract(parsi::expect('*'), [&](auto&&) { /* binop = BinaryOp::mul; */ }),
        parsi::extract(parsi::expect('/'), [&](auto&&) { /* binop = BinaryOp::div; */ }),
        parsi::extract(parsi::expect('+'), [&](auto&&) { /* binop = BinaryOp::add; */ }),
        parsi::extract(parsi::expect('-'), [&](auto&&) { /* binop = BinaryOp::sub; */ }),
        parsi::extract(parsi::expect('^'), [&](auto&&) { /* binop = BinaryOp::pow; */ })
    );

    auto parser_number_sign = parsi::anyof(parsi::expect('-'), parsi::expect('+'));
    auto parser_number = parsi::sequence(
        parsi::optional(parser_number_sign),
        parser_digits,
        OptCont{parsi::expect('.'), parser_digits}
    );

    auto pdbg = [](std::string_view msg, auto parser) {
        return [msg, parser](parsi::Stream stream) {
            // std::println("[debug] message: {}", msg);
            return parser(stream);
        };
    };

    auto parser_expr_ref = LateRef{};

    auto parser_paren_expr = parsi::sequence(
        parsi::expect('('),
        parser_whitespaces,
        pdbg("parens->expr", parser_expr_ref),
        parser_whitespaces,
        parsi::expect(')')
    );

    auto parser_subexpr = opt_cont_set(
        OptCont(Peek(parsi::expect('(')), pdbg("subexpr->parens", parser_paren_expr)),
        OptCont(
            Peek(parsi::anyof(parser_number_sign, parser_digit)),
            parsi::extract(pdbg("subexpr->number", parser_number), [](std::string_view str) { std::println("EXPR: {}", str); })
        )
    );

    auto parser_expr = parsi::sequence(
        parser_whitespaces,
        pdbg("expr->subexpr", parser_subexpr),
        parser_whitespaces,
        OptCont{
            parsi::extract(parser_operator, [](std::string_view str) { std::println("OP: {}", str); }),
            pdbg("binop->expr", parser_expr_ref)
        },
        parser_whitespaces
    );

    std::move(parser_expr_ref).set(parser_expr);

    auto parser = parsi::sequence(
        parser_whitespaces,
        pdbg("parser->expr", parser_expr),
        parser_whitespaces,
        parsi::eos()
    );

    parsi::Result result = parser(argv[1]);
    if (!result) {
        std::println("Error: failed to parse from: {}", result.stream().as_string_view());
        return 1;
    }

    std::println("Rest: {}", result.stream().as_string_view());

    // auto ast = ASTBinOpExpr{
    //     .lhs = std::make_unique<ASTExpr>(std::make_unique<ASTNumber>(ASTNumber{.number = 12})),
    //     .op = BinaryOp::add,
    //     .rhs = std::make_unique<ASTExpr>(std::make_unique<ASTNumber>(ASTNumber{.number = 14})),
    // };

    // std::println("ast: {}", ast.stringify());

    return 0;
}
