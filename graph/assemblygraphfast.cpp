//Copyright 2022 Andrey Zakharov

//This file is part of Bandage

//Bandage is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//Bandage is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with Bandage.  If not, see <http://www.gnu.org/licenses/>.


#include "assemblygraph.h"
#include "path.h"

#include "graph/debruijnedge.h"
#include "graph/debruijnnode.h"
#include "graph/sequenceutils.hpp"
#include "ui/myprogressdialog.h"

#include <QApplication>

#include <lexy/action/parse.hpp> // lexy::parse
#include <lexy/action/trace.hpp> // lexy::trace
#include <lexy/dsl.hpp>          // lexy::dsl::*
#include <lexy/callback.hpp>     // lexy callbacks
#include <lexy/grammar.hpp>
#include <lexy/input/string_input.hpp>
#include <lexy/input/argv_input.hpp>
#include <lexy_ext/report_error.hpp>

#include <algorithm>
#include <memory>
#include <type_traits>
#include <vector>
#include <string>
#include <string_view>
#include <variant>

#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cmath>

#include <zlib.h>

namespace gfa {
struct tag {
    char name[2];
    char type;
    std::variant<int64_t, std::string, float> val;

    template<typename T>
    tag(std::string_view n, std::string_view t, T v)
            : name{n[0], n[1]}, type(t.front()), val(std::move(v))
    {}

    void print() const {
        std::fprintf(stdout, "%c%c", name[0], name[1]);
        std::fputs(":", stdout);
        std::visit([&](const auto& value) { _print(value); }, val);
    }

  private:
    void _print(int64_t val) const {
        std::fprintf(stdout, "%c:%" PRId64, type, val);
    }

    void _print(const std::string &str) const {
        std::fprintf(stdout, "%c:%s", type, str.c_str());
    }

    void _print(float val) const {
        std::fprintf(stdout, "%c:%g", type, val);
    }
};

struct header {
    std::vector<gfa::tag> tags;

    header() {}

    explicit header(std::vector<gfa::tag> t)
            : tags(std::move(t)) {}

    void print() const {
        std::fputs("H", stdout);
        for (const auto &tag : tags) {
            fputs("\t", stdout);
            tag.print();
        }
    }
};

struct segment {
    std::string_view name;
    std::string_view seq;
    std::vector<gfa::tag> tags;

    explicit segment(std::string_view n, std::vector<gfa::tag> t)
            : name{std::move(n)}, tags(std::move(t)) {}

    template<typename Seq>
    explicit segment(std::string_view n, Seq s, std::vector<gfa::tag> t)
            : name{std::move(n)}, seq{s.data(), s.size()}, tags(std::move(t)) {}

    void print() const {
        std::fputs("S", stdout);
        std::fprintf(stdout, "\t%s", std::string(name).c_str());
        std::fprintf(stdout, "\t%s", seq.size() ? std::string(seq).c_str() : "*");
        for (const auto &tag : tags) {
            fputs("\t", stdout);
            tag.print();
        }
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

struct link {
    std::string_view lhs;
    bool lhs_revcomp;
    std::string_view rhs;
    bool rhs_revcomp;
    cigar_string overlap;
    std::vector<gfa::tag> tags;

    explicit link(std::string_view l, std::string_view lr, std::string_view r, std::string_view rr,
                  std::vector<gfa::tag> t)
            : lhs{std::move(l)}, lhs_revcomp(lr.front() == '-'), rhs{std::move(r)}, rhs_revcomp(rr.front() == '-'),
              tags(std::move(t)) {}

    explicit link(std::string_view l, std::string_view lr, std::string_view r, std::string_view rr,
                  cigar_string o,
                  std::vector<gfa::tag> t)
            : lhs{std::move(l)}, lhs_revcomp(lr.front() == '-'), rhs{std::move(r)}, rhs_revcomp(rr.front() == '-'),
              overlap(std::move(o)), tags(std::move(t)) {}

    void print() const {
        std::fputs("L", stdout);
        std::fprintf(stdout, "\t%s\t%c", std::string(lhs).c_str(), lhs_revcomp ? '-' : '+');
        std::fprintf(stdout, "\t%s\t%c", std::string(rhs).c_str(), rhs_revcomp ? '-' : '+');

        std::fputc('\t', stdout);
        if (overlap.size() == 0)
            std::fputc('*', stdout);
        else {
            for (const auto &ovl : overlap)
                ovl.print();
        }

        for (const auto &tag : tags) {
            fputs("\t", stdout);
            tag.print();
        }
    }
};

struct path {
    std::string_view name;
    std::vector<std::string_view> segments;
    std::vector<cigar_string> overlaps;
    std::vector<gfa::tag> tags;

    explicit path(std::string_view n, std::vector<std::string_view> s, std::vector<gfa::tag> t)
            : name{std::move(n)}, segments(std::move(s)),  tags(std::move(t)) {}

    explicit path(std::string_view n, std::vector<std::string_view> s, std::vector<cigar_string> o, std::vector<gfa::tag> t)
            : name{std::move(n)}, segments(std::move(s)), overlaps(std::move(o)), tags(std::move(t)) {}

    void print() const {
        std::fputc('P', stdout);
        std::fprintf(stdout, "\t%s\t", std::string(name).c_str());
        for (size_t i = 0; i < segments.size(); ++i) {
            std::fprintf(stdout, "%s", std::string(segments[i]).c_str());
            if ((i+1) < segments.size())
                std::fputc(',', stdout);
        }

        std::fputc('\t', stdout);
        if (overlaps.size() == 0)
            std::fputc('*', stdout);
        else {
            for (size_t i = 0; i < overlaps.size(); ++i) {
                for (const auto &ovl : overlaps[i])
                    ovl.print();
                if ((i+1) < overlaps.size())
                    std::fputc(',', stdout);
            }
        }

        for (const auto &tag : tags) {
            std::fputc('\t', stdout);
            tag.print();
        }
    }
};

using record = std::variant<header, segment, link, path>;
}

namespace grammar {
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
            auto integer = dsl::if_(dsl::lit_c<'-'>) + dsl::digits<>.no_leading_zero();
            auto fraction  = dsl::lit_c<'.'> >> dsl::digits<>;
            auto exp_char = dsl::lit_c<'e'> | dsl::lit_c<'E'>;
            auto exponent = exp_char >> (dsl::lit_c<'+'> | dsl::lit_c<'-'>) + dsl::digits<>;
            return dsl::peek(dsl::lit_c<'-'> / dsl::digit<>) >>
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
        return dsl::p<tag_name> + colon +
        (
            dsl::capture(LEXY_LIT("A")) >> colon + dsl::p<tag_character> |
            dsl::capture(LEXY_LIT("i")) >> colon + dsl::p<tag_integer>   |
            dsl::capture(LEXY_LIT("f")) >> colon + dsl::p<tag_float>     |
            dsl::capture(LEXY_LIT("Z")) >> colon + dsl::p<tag_string>    |
            dsl::capture(LEXY_LIT("J")) >> colon + dsl::p<tag_string>    |
            dsl::capture(LEXY_LIT("H")) >> colon + dsl::p<tag_string>    |
            dsl::capture(LEXY_LIT("B")) >> colon + dsl::p<tag_string>
        );
    }();

    static constexpr auto value = lexy::callback<gfa::tag>(
        [](std::string_view name, auto type, auto val) {
            return gfa::tag{name, std::string_view{type.data(), type.size()}, val};
        });
};

auto tab = dsl::lit_c<'\t'>;

struct segment_name {
    static constexpr auto name = "segment name";
    static constexpr auto rule =
            dsl::identifier(dsl::ascii::graph - LEXY_LIT("=") - LEXY_LIT("*") - LEXY_LIT(","),
                            dsl::ascii::graph - LEXY_LIT(",") - LEXY_LIT("+") - LEXY_LIT("-"));
    static constexpr auto value = lexy::as_string<std::string_view>;
};

struct segment_orientation {
    static constexpr auto name = "segment orientation";
    static constexpr auto rule = dsl::capture(LEXY_ASCII_ONE_OF("+-"));
    static constexpr auto value = lexy::as_string<std::string_view>;
};

struct oriented_segment {
    static constexpr auto rule = dsl::capture(dsl::token(dsl::p<segment_name> + dsl::p<segment_orientation>));
    static constexpr auto value = lexy::as_string<std::string_view>;
};

struct cigar_string {
    static constexpr auto name = "CIGAR string";

    static constexpr auto cigaropcode =
            LEXY_CHAR_CLASS("CIGAR opcode",
                            LEXY_LIT("M") / LEXY_LIT("I") / LEXY_LIT("D") /
                            LEXY_LIT("N") / LEXY_LIT("S") / LEXY_LIT("H") /
                            LEXY_LIT("P") / LEXY_LIT("X") / LEXY_LIT("="));

    struct cigarop : lexy::transparent_production {
        static constexpr auto name = "CIGAR operation";

        static constexpr auto rule = dsl::integer<std::uint32_t> >> dsl::capture(cigaropcode);
        static constexpr auto value = lexy::callback<gfa::cigarop>(
            [](std::uint32_t cnt, auto lexeme) {
                return gfa::cigarop{cnt, lexeme[0]};
            });
    };

    static constexpr auto rule = dsl::list(dsl::p<cigarop>);
    static constexpr auto value = lexy::as_list<std::vector<gfa::cigarop>>;
};

struct opt_tags {
    static constexpr auto rule = [] {
        auto tags = dsl::list(dsl::p<tag>, dsl::sep(tab));
        return dsl::eof | (tab >> tags + dsl::eof);
    }();
    static constexpr auto value = lexy::as_list<std::vector<gfa::tag>>;
};

// Header
// ======
// Required fields:
// Column    Field        Type        Regexp    Description
// 1         RecordType   Character   H         Record type
struct header {
    static constexpr auto rule = LEXY_LIT("H") >> dsl::p<opt_tags>;
    static constexpr auto value = lexy::construct<gfa::header>;
};

// Segment
// =======
// Required fields:
// Column    Field       Type        Regexp               Description
// 1         RecordType  Character   S                    Record type
// 2         Name        String      [!-)+-<>-~][!-~]*    Segment name
// 3         Sequence    String      \*|[A-Za-z=.]+       Optional nucleotide sequence
struct segment {
    static constexpr auto name = "GFA segment";

    static constexpr auto rule =
            LEXY_LIT("S") >>
            tab + dsl::p<segment_name> +
            tab + (LEXY_LIT("*") |
                   dsl::identifier(dsl::ascii::alpha)) +
            dsl::p<opt_tags>;
    static constexpr auto value = lexy::construct<gfa::segment>;
};

// Link line
// =========
// Required fields:
// Column   Field        Type        Regexp                   Description
// 1        RecordType   Character   L                        Record type
// 2        From         String      [!-)+-<>-~][!-~]*        Name of segment
// 3        FromOrient   String      +|-                      Orientation of From segment
// 4        To           String      [!-)+-<>-~][!-~]*        Name of segment
// 5        ToOrient     String      +|-                      Orientation of To segment
// 6        Overlap      String      \*|([0-9]+[MIDNSHPX=])+  Optional CIGAR string describing overlap
// The Overlap field is optional and can be *, meaning that the CIGAR string is not specified.
struct link {
    static constexpr auto name = "GFA link line";

    static constexpr auto rule =
            LEXY_LIT("L") >>
            tab + dsl::p<segment_name> +
            tab + dsl::p<segment_orientation> +
            tab + dsl::p<segment_name> +
            tab + dsl::p<segment_orientation> +
            tab + (LEXY_LIT("*") | dsl::p<cigar_string>) +
            dsl::p<opt_tags>;
    static constexpr auto value = lexy::construct<gfa::link>;
};

// Path line
// =========
// Required fields
// Column   Field            Type        Regexp                   Description
// 1        RecordType       Character   P                        Record type
// 2        PathName         String      [!-)+-<>-~][!-~]*        Path name
// 3        SegmentNames     String      [!-)+-<>-~][!-~]*        A comma-separated list of segment names and orientations
// 4        Overlaps         String      \*|([0-9]+[MIDNSHPX=])+  Optional comma-separated list of CIGAR strings
struct path {
    static constexpr auto name = "GFA path record";

    struct segments {
        static constexpr auto rule = dsl::list(dsl::p<oriented_segment>, dsl::sep(dsl::comma));
        static constexpr auto value = lexy::as_list<std::vector<std::string_view>>;
    };

    struct overlaps {
        static constexpr auto rule = dsl::list(dsl::p<cigar_string>, dsl::sep(dsl::comma));
        static constexpr auto value = lexy::as_list<std::vector<std::vector<gfa::cigarop>>>;
    };

    static constexpr auto rule =
            LEXY_LIT("P") >>
            tab + dsl::p<segment_name> +
            tab + dsl::p<segments> +
            tab + (LEXY_LIT("*") | dsl::p<overlaps>) +
            dsl::p<opt_tags>;
    static constexpr auto value = lexy::construct<gfa::path>;
};

struct record {
    static constexpr auto name = "GFA record";

    struct expected_gfa_record {
        static LEXY_CONSTEVAL auto name() {
            return "expected GFA record type";
        }
    };

    static constexpr auto rule = [] {
        auto comment = dsl::lit_c<'#'> >> dsl::until(dsl::newline).or_eof();

        return dsl::p<header> |
               dsl::p<segment> |
               dsl::p<link> |
               dsl::p<path> |
               comment |
               // Explicitly ignore all other records (though require proper tab-delimited format)
               dsl::ascii::alpha >> tab + dsl::until(dsl::newline).or_eof() |
               dsl::error<expected_gfa_record>;
     }();

    static constexpr auto value = lexy::construct<gfa::record>;
};

};


std::string AssemblyGraph::getOppositeNodeName(std::string nodeName) const {
    return (nodeName.back() == '-' ?
            nodeName.substr(0, nodeName.size() - 1) + '+' :
            nodeName.substr(0, nodeName.size() - 1) + '-');
}

std::optional<gfa::tag> getTag(const char *name,
                               const std::vector<gfa::tag> &tags) {
    auto res = std::find_if(tags.begin(), tags.end(),
                            [=](const gfa::tag &tag) {
                                return (tag.name[0] == name[0] &&
                                        tag.name[1] == name[1]);
                            });
    if (res == tags.end())
        return {};

    return *res;
}

template<class T>
std::optional<T> getTag(const char *name,
                        const std::vector<gfa::tag> &tags) {
    auto res = std::find_if(tags.begin(), tags.end(),
                            [=](const gfa::tag &tag) {
                                return (tag.name[0] == name[0] &&
                                        tag.name[1] == name[1]);
                            });
    if (res == tags.end())
        return {};

    return std::get<T>(res->val);
}


static ssize_t gzgetdelim(char **buf, size_t *bufsiz, int delimiter, gzFile fp) {
    char *ptr, *eptr;

    if (*buf == NULL || *bufsiz == 0) {
        *bufsiz = BUFSIZ;
        if ((*buf = (char*)malloc(*bufsiz)) == NULL)
            return -1;
    }

    for (ptr = *buf, eptr = *buf + *bufsiz;;) {
        char c = gzgetc(fp);
        if (c == -1) {
            if (gzeof(fp)) {
                ssize_t diff = (ssize_t) (ptr - *buf);
                if (diff != 0) {
                    *ptr = '\0';
                    return diff;
                }
            }
            return -1;
        }
        *ptr++ = c;
        if (c == delimiter) {
            *ptr = '\0';
            return ptr - *buf;
        }
        if (ptr + 2 >= eptr) {
            char *nbuf;
            size_t nbufsiz = *bufsiz * 2;
            ssize_t d = ptr - *buf;
            if ((nbuf = (char*)realloc(*buf, nbufsiz)) == NULL)
                return -1;

            *buf = nbuf;
            *bufsiz = nbufsiz;
            eptr = nbuf + nbufsiz;
            ptr = nbuf + d;
        }
    }
}


static ssize_t gzgetline(char **buf, size_t *bufsiz, gzFile fp) {
    return gzgetdelim(buf, bufsiz, '\n', fp);
}

void AssemblyGraph::buildDeBruijnGraphFromGfa(const QString &fullFileName,
                                              bool *unsupportedCigar,
                                              bool *customLabels,
                                              bool *customColours,
                                              QString *bandageOptionsError) {
    m_graphFileType = GFA;
    m_filename = fullFileName;

    bool sequencesAreMissing = false;

    std::unique_ptr<std::remove_pointer<gzFile>::type, decltype(&gzclose)>
            fp(gzopen(fullFileName.toStdString().c_str(), "r"), gzclose);
    if (!fp)
        throw AssemblyGraphError("failed to open file: " + fullFileName.toStdString());

    size_t i = 0;
    char *line = nullptr;
    size_t len = 0;
    ssize_t read;
    while ((read = gzgetline(&line, &len, fp.get())) != -1) {
        if (read <= 1)
            continue; // skip empty lines

        if (++i % 1000 == 0) QApplication::processEvents();

        auto result = lexy::parse<grammar::record>(lexy::string_input(line, read - 1), lexy_ext::report_error);
        if (!result.has_value())
            continue;

        std::visit(
            [&](const auto &record) {
                using T = std::decay_t<decltype(record)>;
                if constexpr (std::is_same_v<T, gfa::segment>) {
                    std::string nodeName{record.name};
                    const auto &seq = record.seq;

                    // We check to see if the node ended in a "+" or "-".
                    // If so, we assume that is giving the orientation and leave it.
                    // And if it doesn't end in a "+" or "-", we assume "+" and add
                    // that to the node name.
                    if (nodeName.back() != '+' && nodeName.back() != '-')
                        nodeName.push_back('+');

                    // GFA can use * to indicate that the sequence is not in the
                    // file.  In this case, try to use the LN tag for length.  If
                    // that's not available, use a length of 0.
                    // If there is a sequence, then the LN tag will be ignored.
                    size_t length = seq.size();
                    if (!length) {
                        auto lnTag = getTag<int64_t>("LN", record.tags);
                        if (!lnTag)
                            throw AssemblyGraphError("expected LN tag because sequence is missing");

                        length = size_t(*lnTag);
                        sequencesAreMissing = true;
                    }

                    double nodeDepth = 0;
                    if (auto dpTag = getTag<float>("DP", record.tags)) {
                        m_depthTag = "DP";
                        nodeDepth = *dpTag;
                    } else if (auto kcTag = getTag<int64_t>("KC", record.tags)) {
                        m_depthTag = "KC";
                        nodeDepth = double(*kcTag) / double(length);
                    } else if (auto rcTag = getTag<int64_t>("RC", record.tags)) {
                        m_depthTag = "RC";
                        nodeDepth = double(*rcTag) / double(length);
                    } else if (auto fcTag = getTag<int64_t>("FC", record.tags)) {
                        m_depthTag = "FC";
                        nodeDepth = double(*fcTag) / double(length);
                    }

                    std::string oppositeNodeName = getOppositeNodeName(nodeName);
                    Sequence sequence{seq};

                    // FIXME: get rid of copies and QString's
                    auto nodePtr = new DeBruijnNode(nodeName.c_str(), nodeDepth, sequence, length);
                    auto oppositeNodePtr = new DeBruijnNode(oppositeNodeName.c_str(),
                                                            nodeDepth,
                                                            sequence.GetReverseComplement(),
                                                            length);

                    nodePtr->setReverseComplement(oppositeNodePtr);
                    oppositeNodePtr->setReverseComplement(nodePtr);

                    auto lb = getTag<std::string>("LB", record.tags);
                    auto l2 = getTag<std::string>("L2", record.tags);
                    *customLabels = *customLabels || lb || l2;
                    if (lb) setCustomLabel(nodePtr, lb->c_str());
                    if (l2) setCustomLabel(oppositeNodePtr, l2->c_str());

                    auto cb = getTag<std::string>("CB", record.tags);
                    auto c2 = getTag<std::string>("C2", record.tags);
                    *customColours = *customColours || cb || c2;
                    if (cb) setCustomColour(nodePtr, cb->c_str());
                    if (c2) setCustomColour(oppositeNodePtr, c2->c_str());

                    m_deBruijnGraphNodes[nodeName] = nodePtr;
                    m_deBruijnGraphNodes[oppositeNodeName] = oppositeNodePtr;
                } else if constexpr (std::is_same_v<T, gfa::link>) {
                    std::string fromNode{record.lhs};
                    fromNode.push_back(record.lhs_revcomp ? '-' : '+');
                    std::string toNode{record.rhs};
                    toNode.push_back(record.rhs_revcomp ? '-' : '+');

                    DeBruijnNode *fromNodePtr = nullptr;
                    DeBruijnNode *toNodePtr = nullptr;

                    auto fromNodeIt = m_deBruijnGraphNodes.find(fromNode);
                    if (fromNodeIt== m_deBruijnGraphNodes.end())
                        throw AssemblyGraphError("Unknown segment name " + fromNode + " at link: " +
                                                 fromNode + " -- " + toNode);
                    else
                        fromNodePtr = *fromNodeIt;

                    auto toNodeIt = m_deBruijnGraphNodes.find(toNode);
                    if (toNodeIt== m_deBruijnGraphNodes.end())
                        throw AssemblyGraphError("Unknown segment name " + toNode + " at link: " +
                                                 fromNode + " -- " + toNode);
                    else
                        toNodePtr = *toNodeIt;

                    // Ignore dups, hifiasm seems to create them
                    if (m_deBruijnGraphEdges.count({fromNodePtr, toNodePtr}))
                        return;

                    auto edgePtr = new DeBruijnEdge(fromNodePtr, toNodePtr);
                    const auto &overlap = record.overlap;
                    size_t overlapLength = 0;
                    if (overlap.size() > 1 ||
                        (overlap.size() == 1 && overlap.front().op != 'M')) {
                        throw AssemblyGraphError("Non-exact overlaps in gfa are not supported, edge: " +
                                                 fromNodePtr->getName().toStdString() + " -- " +
                                                 toNodePtr->getName().toStdString());
                    } else if (overlap.size() == 1) {
                        overlapLength = overlap.front().count;
                    }

                    edgePtr->setOverlap(static_cast<int>(overlapLength));
                    edgePtr->setOverlapType(EXACT_OVERLAP);

                    bool isOwnPair = fromNodePtr == toNodePtr->getReverseComplement() &&
                                     toNodePtr == fromNodePtr->getReverseComplement();
                    m_deBruijnGraphEdges[{fromNodePtr, toNodePtr}] = edgePtr;
                    fromNodePtr->addEdge(edgePtr);
                    toNodePtr->addEdge(edgePtr);
                    if (isOwnPair) {
                        edgePtr->setReverseComplement(edgePtr);
                    } else {
                        auto *rcFromNodePtr = fromNodePtr->getReverseComplement();
                        auto *rcToNodePtr = toNodePtr->getReverseComplement();
                        auto rcEdgePtr = new DeBruijnEdge(rcToNodePtr, rcFromNodePtr);
                        rcEdgePtr->setOverlap(edgePtr->getOverlap());
                        rcEdgePtr->setOverlapType(edgePtr->getOverlapType());
                        rcFromNodePtr->addEdge(rcEdgePtr);
                        rcToNodePtr->addEdge(rcEdgePtr);
                        edgePtr->setReverseComplement(rcEdgePtr);
                        rcEdgePtr->setReverseComplement(edgePtr);
                        m_deBruijnGraphEdges[{rcToNodePtr, rcFromNodePtr}] = rcEdgePtr;
                    }
                } else if constexpr (std::is_same_v<T, gfa::path>) {
                    QList<DeBruijnNode *> pathNodes;
                    for (const auto &node : record.segments)
                        pathNodes.push_back(m_deBruijnGraphNodes.at(node));
                    m_deBruijnGraphPaths[record.name] = new Path(Path::makeFromOrderedNodes(pathNodes, false));
                }
            },
            result.value());
    }

    m_sequencesLoadedFromFasta = NOT_TRIED;

    if (sequencesAreMissing && !g_assemblyGraph->attemptToLoadSequencesFromFasta()) {
        throw AssemblyGraphError("Cannot load fasta file with sequences.");
    }
}
