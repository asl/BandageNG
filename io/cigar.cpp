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

#include <lexy/action/parse.hpp> // lexy::parse
#include <lexy/input/string_input.hpp>
#include <lexy_ext/report_error.hpp>

#include "cigar.h"

#include "cigar.inl"

namespace cigar {
std::ostream &operator<<(std::ostream &s, const tag &t) {
    s << t.name[0] << t.name[1] << ':';
    return std::visit([&](const auto& value) -> std::ostream& { return s << value; }, t.val);
}

std::optional<tag> parseTag(const char* line, size_t len) {
    lexy::visualization_options opts;
    opts.max_lexeme_width = 35;

    auto result = lexy::parse<grammar::tag>(lexy::string_input(line, len), lexy_ext::report_error.opts(opts));
    if (result.has_value())
        return std::make_optional(result.value());

    return {};
}


}
