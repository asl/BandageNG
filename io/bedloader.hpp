// Copyright 2022 Andrey Zakharov

// This file is part of Bandage-NG

// Bandage-NG is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage-NG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include <QColor>

namespace bed {

enum class Strand : char {
    UNKNOWN = '.',
    NORMAL = '+',
    REVERSE_COMPLEMENT = '-', // ?? maybe simple complement or simple reverse
};

struct ItemRgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    QColor toQColor() const {
        return {r, g, b};
    }
};

struct Block {
    int64_t start, end;
};

struct Line {
    std::string chrom{};
    int64_t chromStart = 0;
    int64_t chromEnd = 0;
    std::string name{};
    int score = 0;
    Strand strand = Strand::UNKNOWN;
    int64_t thickStart = -1;
    int64_t thickEnd = -1;
    ItemRgb itemRgb{};
    std::vector<Block> blocks{};
};

std::vector<int64_t> parseIntArray(const std::string &intArrayString);

std::vector<Line> load(const std::filesystem::path &path);

}
