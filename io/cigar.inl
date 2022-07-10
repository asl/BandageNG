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

#include <lexy/dsl.hpp>          // lexy::dsl::*
#include <lexy/callback.hpp>     // lexy callbacks
#include <lexy/grammar.hpp>

namespace cigar::grammar {
    namespace dsl = lexy::dsl;

    struct tag {
        struct tag_character : lexy::token_production {
            static constexpr auto rule = dsl::capture(dsl::ascii::alpha_digit);
            static constexpr auto value = lexy::as_string<std::string>;
        };

        struct tag_integer : lexy::token_production {
            static constexpr auto rule =
                    dsl::minus_sign + dsl::integer<std::int64_t>(dsl::digits<>.no_leading_zero());
            static constexpr auto value = lexy::as_integer<std::int64_t>;
        };

        struct tag_string : lexy::token_production {
            static constexpr auto rule = dsl::identifier(dsl::ascii::print);
            static constexpr auto value = lexy::as_string<std::string>;
        };

        struct tag_float : lexy::token_production {
            static constexpr auto rule = [] {
                auto integer = dsl::if_(dsl::lit_c < '-' > ) + dsl::digits<>.no_leading_zero();
                auto fraction = dsl::lit_c < '.' > >> dsl::digits<>;
                auto exp_char = dsl::lit_c < 'e' > | dsl::lit_c<'E'>;
                auto exponent = exp_char >> (dsl::lit_c < '+' > | dsl::lit_c < '-' > ) + dsl::digits<>;
                return dsl::peek(dsl::lit_c < '-' > / dsl::digit<>) >>
                                                                    dsl::position +
                                                                    integer +
                                                                    dsl::if_(fraction) +
                                                                    dsl::if_(exponent) +
                                                                    dsl::position;
            }();

            static constexpr auto value = lexy::callback<float>(
                    // std::from_chars(const char*, const char*, float) is only
                    // available starting from libc++ from LLVM 14 :(
                    [](const char *first, const char *) { return ::atof(first); }
            );
        };

        struct tag_name : lexy::token_production {
            static constexpr auto rule = dsl::capture(dsl::token(dsl::ascii::alpha + dsl::ascii::alpha_digit));
            static constexpr auto value = lexy::as_string<std::string_view>;
        };

        static constexpr auto rule = [] {
            auto colon = dsl::lit_c<':'>;
            return dsl::p < tag_name > +colon +
                   (
                           dsl::capture(LEXY_LIT("A")) >> colon + dsl::p < tag_character > |
                           dsl::capture(LEXY_LIT("i")) >> colon + dsl::p < tag_integer > |
                           dsl::capture(LEXY_LIT("f")) >> colon + dsl::p < tag_float > |
                           dsl::capture(LEXY_LIT("Z")) >> colon + dsl::p < tag_string > |
                           dsl::capture(LEXY_LIT("J")) >> colon + dsl::p < tag_string > |
                           dsl::capture(LEXY_LIT("H")) >> colon + dsl::p < tag_string > |
                           dsl::capture(LEXY_LIT("B")) >> colon + dsl::p < tag_string >
                   );
        }();

        static constexpr auto value = lexy::callback<gfa::tag>(
                [](std::string_view name, auto type, auto val) {
                    return gfa::tag{name, std::string_view{type.data(), type.size()}, val};
                });
    };

    struct cigar_string {
        static constexpr auto name = "CIGAR string";

        static constexpr auto cigaropcode =
                LEXY_CHAR_CLASS("CIGAR opcode",
                                LEXY_LIT("M") / LEXY_LIT("I") / LEXY_LIT("D") /
                                LEXY_LIT("N") / LEXY_LIT("S") / LEXY_LIT("H") /
                                LEXY_LIT("P") / LEXY_LIT("X") / LEXY_LIT("=")) / LEXY_LIT("J");

        struct cigarop : lexy::transparent_production {
            static constexpr auto name = "CIGAR operation";

            static constexpr auto rule =
                    dsl::period |
                    dsl::integer<std::uint32_t> >> dsl::capture(cigaropcode);
            static constexpr auto value = lexy::callback<gfa::cigarop>(
                    []() { return gfa::cigarop{0, 0}; },
                    [](std::uint32_t cnt, auto lexeme) {
                        return gfa::cigarop{cnt, lexeme[0]};
                    });
        };

        static constexpr auto rule = dsl::list(dsl::p<cigarop>);
        static constexpr auto value = lexy::as_list<std::vector<gfa::cigarop>>;
    };

    auto tab = dsl::lit_c<'\t'>;

    struct opt_tags {
        static constexpr auto rule = [] {
            auto tags = dsl::list(dsl::p<tag>, dsl::sep(tab));
            return dsl::eof | (tab >> tags + dsl::eof);
        }();
        static constexpr auto value = lexy::as_list<std::vector<gfa::tag>>;
    };
}