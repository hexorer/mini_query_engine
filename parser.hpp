#pragma once

#include "parsi/parsi.hpp"

namespace mini
{
// template <std::size_t MaxCountV, typename ParserF>
// constexpr auto parsi_repeat(ParserF&& parser) {
//     return [parser](auto contination_parser, parsi::Stream stream) -> parsi::Result {
//         if constexpr (MaxCountV == 0) {
//             return parsi::Result{stream, false};
//         }
//         return parser(parsi::sequence(parsi_repeat<MaxCountV - 1>(parser), contination_parser), stream);
//     };
// }

// template <typename ParserF>
// constexpr auto parsi_peek(ParserF&& parser) {
//     return [parser](auto continuation_parser, parsi::Stream stream) -> parsi::Result {
//         //
//     };
// }
}
