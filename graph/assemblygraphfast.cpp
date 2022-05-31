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
#include "gfa.hpp"

#include "graph/debruijnedge.h"
#include "graph/debruijnnode.h"
#include "graph/sequenceutils.hpp"
#include "ui/myprogressdialog.h"

#include <QApplication>

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

std::string AssemblyGraph::getOppositeNodeName(std::string nodeName) const {
    return (nodeName.back() == '-' ?
            nodeName.substr(0, nodeName.size() - 1) + '+' :
            nodeName.substr(0, nodeName.size() - 1) + '-');
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

        auto result = gfa::parse_record(line, read - 1);
        if (!result)
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
            *result);
    }

    m_sequencesLoadedFromFasta = NOT_TRIED;

    if (sequencesAreMissing && !g_assemblyGraph->attemptToLoadSequencesFromFasta()) {
        throw AssemblyGraphError("Cannot load fasta file with sequences.");
    }
}
