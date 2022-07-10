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

#pragma once

#include "cigar.h"

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace gaf {
    using cigar::tag;

    struct record {
        std::string_view name;
        size_t qlen;
        uint64_t qstart;
        uint64_t qend;
        std::string_view strand;
        std::vector<std::string_view> segments;
        size_t plen;
        uint64_t pstart;
        uint64_t pend;
        size_t matches;
        size_t alen;
        unsigned mapq;

        std::vector<tag> tags;

        explicit record(std::string_view n,
                        size_t ql, uint64_t qs, uint64_t qe,
                        std::string_view s,
                        std::vector<std::string_view> seg,
                        size_t pl, uint64_t ps, uint64_t pe,
                        size_t m, size_t al, unsigned mq,
                        std::vector<tag> t)
            : name{n}, qlen(ql), qstart(qs), qend(qe), strand(s),
              segments{std::move(seg)},
              plen(pl), pstart(ps), pend(pe),
              matches(m), alen(al), mapq(mq),
              tags(std::move(t)) {}
    };

    std::optional<record> parseRecord(const char* line, size_t len);
} // namespace gaf