#ifndef BANDAGE_GRAPH_BEDLOADER_HPP_
#define BANDAGE_GRAPH_BEDLOADER_HPP_

#include <string>
#include <vector>
#include <filesystem>

#include <fast-cpp-csv-parser/csv.h>
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

#endif //BANDAGE_GRAPH_BEDLOADER_HPP_
