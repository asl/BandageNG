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

#include <string>
#include <variant>
#include <string_view>
#include <optional>
#include <vector>
#include <algorithm>
#include <ostream>

#include <cstdint>
#include <cstdlib>
#include <cinttypes>
#include <cstdio>

namespace cigar {
    struct tag {
        char name[2];
        char type;
        std::variant<int64_t, std::string, float> val;

        template<typename T>
        tag(std::string_view n, std::string_view t, T v)
                : name{n[0], n[1]}, type(t.front()), val(std::move(v)) {}

        friend std::ostream &operator<<(std::ostream &s, const tag &t);

        void print() const {
            fprintf(stdout, "%c%c", name[0], name[1]);
            fputs(":", stdout);
            std::visit([&](const auto& value) { _print(value); }, val);
        }

      private:
        void _print(int64_t i) const {
            std::fprintf(stdout, "%c:%" PRId64, type, i);
        }

        void _print(const std::string &str) const {
            std::fprintf(stdout, "%c:%s", type, str.c_str());
        }

        void _print(float f) const {
            std::fprintf(stdout, "%c:%g", type, f);
        }
    };

    struct cigarop {
        uint32_t count : 24;
        char op : 8;

        void print() const {
            std::fprintf(stdout, "%u%c", count, op);
        }
    };

    using cigar_string = std::vector<cigarop>;

    static inline std::optional<tag>
    getTag(const char *name,
           const std::vector<tag> &tags) {
        auto res = std::find_if(tags.begin(), tags.end(),
                                [=](const tag &tag) {
                                    return (tag.name[0] == name[0] &&
                                            tag.name[1] == name[1]);
                                });
        if (res == tags.end())
            return {};

        return *res;
    }

    template<class T>
    std::optional<T> getTag(const char *name,
                            const std::vector<tag> &tags) {
        auto res = std::find_if(tags.begin(), tags.end(),
                                [=](const tag &tag) {
                                    return (tag.name[0] == name[0] &&
                                            tag.name[1] == name[1]);
                                });
        if (res == tags.end())
            return {};

        return std::get<T>(res->val);
    }
}