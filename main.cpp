#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <unordered_set>

#include <parsi/parsi.hpp>

struct Empty {};
struct StrError { const char* str = "unknown"; };
using RCString = std::string; // TODO

template <typename ValueT, typename ErrorT = StrError>
using Result = std::expected<ValueT, ErrorT>;

template <typename T>
using Ref = std::unique_ptr<T>;

template <typename T>
using RCRef = std::shared_ptr<T>;

template <typename T>
using WRef = std::shared_ptr<T>;

template <typename T>
using Vec = std::vector<T>;

template <typename ...Ts>
using Union = std::variant<Ts...>;

template <typename ...Ts>
using UnionVec = Vec<Union<Ts...>>;

template <typename ...Ts>
using UnionRCRefVec = UnionVec<Union<RCRef<Ts>...>>;

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

static constexpr auto parser_expecti(std::string_view str) {
    return [str](parsi::Stream stream) -> parsi::Result {
        return parsi::expect(std::string(str))(stream);
    };
}


struct CLI {
    std::string query;
    std::filesystem::path input_file_path;
};

static void print_help(std::string_view program_name) {
    std::println(std::cerr, "Usage:");
    std::println(std::cerr, "\t{} <query> <csv-file>", program_name);
}

static Result<CLI, std::string> parse_args(int argc, char* argv[]) {
    if (argc != 3) {
        return std::unexpected("insufficient args.");
    }
    return CLI{.query = argv[1], .input_file_path = argv[2]};
}


template <typename ...Fs>
struct Overloaded : public Fs... { using Fs::operator()...; };

template <typename ...Fs>
Overloaded(Fs&&...) -> Overloaded<Fs...>;

namespace mini {

struct EOS {};
struct Number {};

struct ASTWildcard {
};

struct ASTIdentifier {
    RCString name;
};

struct ASTLiteralString {
    RCString value;
};

struct ASTLiteralNumeric {
    Number value;
};

struct ASTExpressionScalar {
    Union<ASTIdentifier, ASTLiteralString, ASTLiteralNumeric> value;
};

struct ASTQuery;
struct ASTSelect {
    UnionRCRefVec<ASTQuery, ASTExpressionScalar, ASTWildcard> items;
};

struct ASTQuery {
    ASTSelect select;
};

struct QueryPlan {
    // TODO
};

class IPipeline {
public:
    ~IPipeline() = default;
};

struct ExecutionPlan {
    // TODO
};

static Result<ASTQuery> parse_query(const std::string_view query_str) {
    constexpr auto parser_identifier = parsi::sequence(
        parsi::repeat<1>(parsi::anyof(parsi::expect('_'), parser_lower_alphabet, parser_upper_alphabet)),
        parsi::repeat(parsi::anyof(parsi::expect('_'), parser_digits, parser_lower_alphabet, parser_upper_alphabet))
    );
    constexpr auto parser_string_literal = parsi::sequence(
        parsi::expect('"'),
        parsi::repeat(parsi::expect_not('"')), // TODO
        parsi::expect('"')
    );
    constexpr auto parser_numeric_literal = parsi::sequence(
        parsi::optional(parsi::anyof(parsi::expect('-'), parsi::expect('-'))),
        parsi::repeat<1>(parser_digits) // TODO
    );
    auto parser_select_item = parsi::anyof(
        parser_identifier,
        parser_string_literal,
        parser_numeric_literal
    );
    auto parser = parsi::sequence(
        parsi::expect("select"),
        parser_whitespaces,
        parser_select_item,
        parser_whitespaces,
        parsi::eos()
    );
    return ASTQuery{};
}

static Result<QueryPlan> plan_query(const ASTQuery& query) {
    return QueryPlan{};
}

static std::string query_tree_to_string(const ASTQuery& query) {
    return "";
}

static ExecutionPlan compile_execution_plan(const QueryPlan& plan) {
    return ExecutionPlan{};
}

static Result<Empty> execute_pipeline(ExecutionPlan pipeline) {
    return std::unexpected(StrError{"not implemented"});
}

template <typename F>
static Result<Empty> parse_csv(std::string_view input, F&& callback) {
    std::vector<std::string_view> columns;

    auto item_visit_cb = [&columns](std::string_view item) {
        columns.push_back(item);
    };

    auto line_visit_cb = [&columns, &callback](const std::string_view& /* empty_line */) {
        callback(std::exchange(columns, {}));
    };

    auto eos_visit_cb = [&callback](const std::string_view& /* empty */) {
        // callback(EOS{});
    };

    auto item_parser = parsi::repeat(parsi::expect(parsi::Charset(",\n").opposite())); // TODO quoted values escaping comma
    auto line_parser = parsi::sequence(
        parsi::extract(item_parser, item_visit_cb),
        parsi::repeat(
            parsi::sequence(
                parsi::expect(','),
                parsi::extract(item_parser, item_visit_cb)
            )
        ),
        parsi::anyof(
            parsi::extract(parsi::expect('\n'), line_visit_cb),
            parsi::extract(parsi::eos(), line_visit_cb)
        )
    );
    auto parser = parsi::sequence(
        parsi::repeat(
            parsi::sequence(
                [](parsi::Stream stream) { return parsi::Result{stream, stream.size() != 0}; },
                line_parser
            )
        ),
        parsi::extract(parsi::eos(), eos_visit_cb)
    );

    parsi::Result result = parser(input);
    if (!result) {
        return std::unexpected(result.stream().as_string_view());
    }

    return {};
}

static Result<std::string> generate_csv_row(std::span<const std::string_view> columns) {
    if (columns.size() <= 0) {
        return "";
    }
    if (columns.size() == 1) {
        return std::string(columns[0]);
    }

    std::string ret{columns[0]};

    for (std::size_t index = 1; index < columns.size(); ++index) {
        ret += ',';
        ret += columns[index];
    }

    return ret;
}
} // namespace mini


int main(int argc, char* argv[]) {
    auto cli = parse_args(argc, argv);
    if (!cli) {
        std::println(std::cerr, "Error: {}", cli.error());
        print_help(argv[0]);
        return 1;
    }

    std::println(std::cerr, "[Info] query: {}", cli->query);
    std::println(std::cerr, "[Info] input file path: {}", cli->input_file_path.c_str());

    auto query_tree_res = mini::parse_query(cli->query);
    if (!query_tree_res) {
        std::println(std::cerr, "Error: failed to parse the query.");
        return 1;
    }

    std::println(std::cerr, "[Info] query tree: {}", mini::query_tree_to_string(*query_tree_res));

    auto execution_plan_res = mini::plan_query(std::move(*query_tree_res));
    if (!execution_plan_res) {
        std::println(std::cerr, "Error: failed to plan the query.");
        return 1;
    }

    mini::ExecutionPlan execution_pipeline = mini::compile_execution_plan(std::move(*execution_plan_res));

    std::ifstream input_file{cli->input_file_path};
    std::istream& input_stream = (cli->input_file_path == "-") ? std::cin : input_file;

    if (!input_stream) {
        std::println(std::cerr, "Error: failed to open the file.");
        return 1;
    }

    // auto export_fn = Overloaded{
    //     [](const std::vector<std::string_view>& columns) {
    //         std::println("{}", mini::generate_csv_row(columns).value_or("ERROR"));
    //     },
    //     [](mini::EOS) {
    //         // nothing.
    //     }
    // };

    // auto source_fn = [&](auto&& receive_fn) {
    //     std::string line;
    //     while (std::getline(input_stream, line)) {
    //         // TODO cancellable receiver
    //         if (auto res = mini::parse_csv(line, receive_fn); !res) {
    //             std::println(std::cerr, "error: csv parser failed. at: ", res.error());
    //             break;
    //         }
    //     }
    //     receive_fn(mini::EOS{});
    // };

    if (Result<Empty> res = mini::execute_pipeline(std::move(execution_pipeline)); !res) {
        std::println(std::cerr, "Error: pipeline execution failed: {}", res.error().str);
    }

    return 0;
}
