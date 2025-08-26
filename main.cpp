#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <unordered_set>

#include <parsi/parsi.hpp>

struct Empty {};

struct CLI {
    std::string query;
    std::filesystem::path input_file_path;
};

static void print_help(std::string_view program_name) {
    std::println(std::cerr, "Usage:");
    std::println(std::cerr, "\t{} <query> <csv-file>", program_name);
}

static std::expected<CLI, std::string> parse_args(int argc, char* argv[]) {
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

// template <typename F>
// static auto parsi_negate(F&& parser) {
//     return [parser=std::forward<F>(parser)](parsi::Stream stream) -> parsi::Result {
//         if (parsi::Result result = parser(stream); !result) {
//             return result.
//         }
//         return 
//     };
// }

// static auto parsi_abort(std::string error) {
//     return [error=std::move(error)](parsi::Stream stream) -> parsi::Result {
//         throw std::runtime_error{error + " ::: " + std::string(stream.as_string_view())};
//     };
// }

static constexpr auto parser_whitespace = parsi::repeat(parsi::expect(parsi::Charset(" \t")));
static constexpr auto parser_digits = parsi::repeat<1>(parsi::expect(parsi::Charset("0123456789")));

static std::size_t digits_to_number(std::string_view str) {
    std::size_t ret = 0;
    for (char chr : str) {
        ret *= 10;
        ret += chr - '0';
    }
    return ret;
}

struct QueryTree {
    struct Select {
        std::vector<std::size_t> indices;
    };

    Select select;
};

static std::expected<QueryTree, Empty> parse_query(std::string_view query_str) {
    QueryTree tree;

    auto visit_select_item_cb = [&tree](std::string_view str) {
        tree.select.indices.push_back(digits_to_number(str));
    };

    auto inner_select_parser = parsi::sequence(
        parsi::extract(parser_digits, visit_select_item_cb),
        parsi::repeat(
            parsi::sequence(
                parser_whitespace,
                parsi::expect(','),
                parser_whitespace,
                parsi::extract(parser_digits, visit_select_item_cb)
            )
        )
    );

    auto select_parser = parsi::sequence(
        parser_whitespace,
        parsi::expect("select"),
        parser_whitespace,
        parsi::expect('('),
        parser_whitespace,
        parsi::anyof(
            parsi::expect(')'),
            parsi::sequence(
                inner_select_parser,
                parser_whitespace,
                parsi::expect(')')
            )
        ),
        parser_whitespace
    );

    if (!select_parser(query_str)) {
        return std::unexpected(Empty{});
    }

    return tree;
}

static std::string query_tree_to_string(const QueryTree& query_tree) {
    std::string ret;
    ret += "select(";
    if (!query_tree.select.indices.empty()) {
        ret += std::to_string(query_tree.select.indices[0]);
    }
    for (std::size_t index = 1; index < query_tree.select.indices.size(); ++index) {
        ret += ',';
        ret += ' ';
        ret += std::to_string(query_tree.select.indices[index]);
    }
    ret += ")";
    return ret;
}

struct QueryPlan {
    std::unordered_set<std::size_t> select_indices;
};

static std::expected<QueryPlan, Empty> plan_query(QueryTree query_tree) {
    QueryPlan ret;
    for (const std::size_t select_index : query_tree.select.indices) {
        ret.select_indices.insert(select_index);
    }
    return ret;
}

struct ExecutionPlan {
    QueryPlan plan;
};

static ExecutionPlan compile_query(QueryPlan query_plan) {
    return ExecutionPlan{.plan = std::move(query_plan)};
}

template <typename SourceF, typename SinkF>
static void execute_plan(ExecutionPlan exec_plan, SourceF&& source_fn, SinkF&& sink_fn) {
    auto callback = Overloaded{
        [&sink_fn, &exec_plan](const std::vector<std::string_view>& columns) {
            std::vector<std::string_view> new_columns{};
            for (std::size_t index = 0; index < columns.size(); ++index) {
                if (exec_plan.plan.select_indices.contains(index)) {
                    new_columns.push_back(columns[index]);
                }
            }
            sink_fn(new_columns);
        },
        [&sink_fn](EOS) {
            sink_fn(EOS{});
        }
    };
    source_fn(callback);
}


template <typename F>
static std::expected<Empty, std::string_view> parse_csv(std::string_view input, F&& callback) {
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

static std::expected<std::string, Empty> generate_csv_row(std::span<const std::string_view> columns) {
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

    auto query_plan_res = mini::plan_query(std::move(*query_tree_res));
    if (!query_plan_res) {
        std::println(std::cerr, "Error: failed to plan the query.");
        return 1;
    }

    mini::ExecutionPlan execution_plan = mini::compile_query(std::move(*query_plan_res));

    std::ifstream input_file{cli->input_file_path};
    std::istream& input_stream = (cli->input_file_path == "-") ? std::cin : input_file;

    if (!input_stream) {
        std::println(std::cerr, "Error: failed to open the file.");
        return 1;
    }

    auto export_fn = Overloaded{
        [](const std::vector<std::string_view>& columns) {
            std::println("{}", mini::generate_csv_row(columns).value_or("ERROR"));
        },
        [](mini::EOS) {
            // nothing.
        }
    };

    auto source_fn = [&](auto&& receive_fn) {
        std::string line;
        while (std::getline(input_stream, line)) {
            // TODO cancellable receiver
            if (auto res = mini::parse_csv(line, receive_fn); !res) {
                std::println(std::cerr, "error: csv parser failed. at: ", res.error());
                break;
            }
        }
        receive_fn(mini::EOS{});
    };

    mini::execute_plan(std::move(execution_plan), source_fn, export_fn);

    return 0;
}
