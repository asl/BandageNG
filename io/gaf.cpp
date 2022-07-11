// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#include "gaf.h"
#include "cigar.inl"

#include <lexy/action/parse.hpp> // lexy::parse
#include <lexy/action/trace.hpp> // lexy::trace
#include <lexy/dsl.hpp>          // lexy::dsl::*
#include <lexy/callback.hpp>     // lexy callbacks
#include <lexy/grammar.hpp>
#include <lexy/input/string_input.hpp>
#include <lexy_ext/report_error.hpp>

#include <optional>

namespace gaf {
    namespace grammar {
        namespace dsl = lexy::dsl;
        using cigar::grammar::tab;
        using cigar::grammar::opt_tags;

        struct path_name {
            static constexpr auto rule = dsl::identifier(dsl::ascii::word);
            static constexpr auto value = lexy::as_string<std::string_view>;
        };

        struct uint {
            static constexpr auto rule = dsl::integer<std::uint64_t>(dsl::digits<>.no_leading_zero());
            static constexpr auto value = lexy::as_integer<std::uint64_t>;
        };

        struct strand {
            static constexpr auto rule = dsl::capture(LEXY_ASCII_ONE_OF("+-"));
            static constexpr auto value = lexy::as_string<std::string_view>;
        };

        struct oriented_segment {
            static constexpr auto rule = dsl::capture(dsl::token(LEXY_ASCII_ONE_OF("<>") + dsl::identifier(dsl::ascii::word)));
            static constexpr auto value = lexy::as_string<std::string_view>;
        };

        struct segments {
            static constexpr auto rule = dsl::list(dsl::p<oriented_segment>);
            static constexpr auto value = lexy::as_list<std::vector<std::string_view>>;
        };

        struct record {
            static constexpr auto name = "GAF record";

            static constexpr auto rule = [] {
                return dsl::p<path_name> + tab + // query name
                       dsl::p<uint> + tab + // query length
                       dsl::p<uint> + tab + // query start
                       dsl::p<uint> + tab + // query_end
                       dsl::p<strand> + tab + // strand
                       dsl::p<segments> + tab + // segments
                       dsl::p<uint> + tab + // path length
                       dsl::p<uint> + tab + // start position on path
                       dsl::p<uint> + tab + // end position on path
                       dsl::p<uint> + tab + // number or matches
                       dsl::p<uint> + tab + // alignment block length
                       dsl::p<uint> + // mapping quality
                       dsl::p<opt_tags>;
            }();

            static constexpr auto value = lexy::construct<gaf::record>;
        };
    }

    std::optional<record> parseRecord(const char* line, size_t len) {
        auto result = lexy::parse<grammar::record>(lexy::string_input(line, len), lexy_ext::report_error);
        if (result.has_value())
            return std::make_optional(result.value());

        return {};
    }
}