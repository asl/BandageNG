// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

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

#include "gfawriter.h"

#include "assemblygraph.h"
#include "debruijnedge.h"
#include "path.h"
#include "sequenceutils.h"
#include "program/colormap.h"

#include <QFile>
#include <QTextStream>

namespace gfa {
    static void printTags(QByteArray &out, const std::vector<gfa::tag> &tags) {
        for (const auto &tag: tags) {
            out += '\t';
            out += tag.name[0];
            out += tag.name[1];
            out += ":";
            out += tag.type;
            out += ":";
            std::visit([&](const auto &val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    out += qPrintable(QString::number(val));
                } else if constexpr (std::is_same_v<T, float>) {
                    out += qPrintable(QString::number(val));
                } else if constexpr (std::is_same_v<T, std::string>) {
                    out += val;
                }
            }, tag.val);
        }
    }

    // This function gets the node's sequence for a GFA file.
    // If the sequence is missing, it will just give "*"
    QByteArray getSequenceForGfa(const DeBruijnNode *node) {
        if (node->sequenceIsMissing())
            return {"*"};

        return utils::sequenceToQByteArray(node->getSequence());
    }

    static QByteArray getGfaSegmentLine(const DeBruijnNode *node, const AssemblyGraph &graph,
                                        const QString &depthTag) {
        QByteArray gfaSequence = getSequenceForGfa(node);

        QByteArray gfaSegmentLine = "S";
        const std::string_view segmentName = node->getNameWithoutSign();
        gfaSegmentLine += "\t" + QByteArray(segmentName.data(), int(segmentName.size()));
        gfaSegmentLine += "\t" + gfaSequence;
        gfaSegmentLine += "\tLN:i:" + QString::number(gfaSequence.length()).toLatin1();

        //We use the depthTag to guide how we save the node depth.
        //If it is empty, that implies that the loaded graph did not have depth
        //information and so we don't save depth.
        if (depthTag == "DP" || depthTag == "dp")
            gfaSegmentLine += "\tDP:f:" + QString::number(node->getDepth()).toLatin1();
        else if (depthTag == "rd")
            gfaSegmentLine += "\trd:i:" + QString::number(node->getDepth()-1).toLatin1();
        else if (depthTag == "KC" || depthTag == "RC" || depthTag == "FC")
            gfaSegmentLine += "\t" + depthTag.toLatin1() + ":i:" +
                              QString::number(int(node->getDepth() * gfaSequence.length() + 0.5)).toLatin1();

        //If the user has included custom labels or colours, include those.
        QString label = graph.getCustomLabel(node);
        if (!label.isEmpty())
            gfaSegmentLine += "\tLB:Z:" + label.toLatin1();

        QString rcLabel = graph.getCustomLabel(node->getReverseComplement());
        if (!rcLabel.isEmpty())
            gfaSegmentLine += "\tL2:Z:" + rcLabel.toLatin1();
        if (graph.hasCustomColour(node))
            gfaSegmentLine += "\tCL:Z:" + getColourName(graph.getCustomColour(node)).toLatin1();
        if (graph.hasCustomColour(node->getReverseComplement()))
            gfaSegmentLine += "\tC2:Z:" + getColourName(graph.getCustomColour(node->getReverseComplement())).toLatin1();

        auto tagIt = graph.m_nodeTags.find(node);
        if (tagIt != graph.m_nodeTags.end())
            printTags(gfaSegmentLine, tagIt->second);

        return gfaSegmentLine;
    }

    static QByteArray getGfaLinkLine(const DeBruijnEdge *edge, const AssemblyGraph &graph) {
        const DeBruijnNode *startingNode = edge->getStartingNode();
        const DeBruijnNode *endingNode = edge->getEndingNode();
        bool isJump = edge->getOverlapType() == JUMP;

        QByteArray gfaLinkLine = isJump ? "J\t" : "L\t";
        const std::string_view startingName = startingNode->getNameWithoutSign();
        const std::string_view endingName = endingNode->getNameWithoutSign();
        gfaLinkLine += QByteArray(startingName.data(), int(startingName.size()));
        gfaLinkLine += '\t';
        gfaLinkLine += startingNode->getSign();
        gfaLinkLine += '\t';
        gfaLinkLine += QByteArray(endingName.data(), int(endingName.size()));
        gfaLinkLine += '\t';
        gfaLinkLine += endingNode->getSign();
        gfaLinkLine += '\t';
        const auto overlapType = edge->getOverlapType();
        if (overlapType == JUMP) {
            gfaLinkLine += edge->getOverlap() == 0 ? "*" : qPrintable(QString::number(edge->getOverlap()));
        } else if (overlapType == EXACT_OVERLAP || overlapType == AUTO_DETERMINED_EXACT_OVERLAP) {
            gfaLinkLine += qPrintable(QString::number(edge->getOverlap()));
            gfaLinkLine += "M";
        } else {
            gfaLinkLine += '*';
        }

        if (graph.hasCustomColour(edge)) {
            gfaLinkLine += "\tCL:Z:";
            gfaLinkLine += qPrintable(getColourName(graph.getCustomColour(edge)));
        }
        if (!edge->isOwnReverseComplement() && graph.hasCustomColour(edge->getReverseComplement())) {
            gfaLinkLine += "\tC2:Z:";
            gfaLinkLine += qPrintable(getColourName(graph.getCustomColour(edge->getReverseComplement())));
        }

        auto tagIt = graph.m_edgeTags.find(edge);
        if (tagIt != graph.m_edgeTags.end())
            printTags(gfaLinkLine, tagIt->second);

        return gfaLinkLine;
    }

    static QByteArray getGfaPathLine(const std::string &name, const Path &path,
                                     const AssemblyGraph &graph) {
        QByteArray gfaPathLine = "P\t";
        gfaPathLine += name;
        gfaPathLine += '\t';

        const auto &nodes = path.nodes();
        const auto &edges = path.edges();

        // edges is one less than nodes for linear paths and of
        // same length for circular paths
        for (size_t i = 0; i < edges.size(); ++i) {
            const auto *edge = edges[i];
            const std::string stepName = nodes[i]->getName();
            gfaPathLine += QByteArray(stepName.data(), int(stepName.size()));
            gfaPathLine += edge->getOverlapType() == JUMP ? ';' : ',';
        }
        // Handle last node: for circular paths we're adding extra node here
        if (nodes.size() == edges.size()) { // circular path
            const std::string stepName = nodes.front()->getName();
            gfaPathLine += QByteArray(stepName.data(), int(stepName.size()));
        } else {
            const std::string stepName = nodes.back()->getName();
            gfaPathLine += QByteArray(stepName.data(), int(stepName.size()));
        }

        // Path CIGAR strings are not stored, so the overlaps field is unspecified.
        gfaPathLine += "\t*";
        return gfaPathLine;
    }

    static bool pathFullyDrawn(const Path &path) {
        if (path.nodes().empty())
            return false;
        for (const auto *node : path.nodes()) {
            if (!node->thisNodeOrReverseComplementIsDrawn())
                return false;
        }
        return true;
    }

    static QByteArray getGfaWalkLine(const std::string &seqId, const Walk &walk) {
        QByteArray line = "W\t";
        line += QByteArray(walk.sampleId.data(), int(walk.sampleId.size()));
        line += '\t';
        line += QByteArray::number(walk.hapIndex);
        line += '\t';
        line += QByteArray(seqId.data(), int(seqId.size()));
        line += '\t';
        line += QByteArray::number(walk.seqStart);
        line += '\t';
        line += QByteArray::number(walk.seqEnd);
        line += '\t';
        for (const auto *node : walk.walk.nodes()) {
            line += node->isPositiveNode() ? '>' : '<';
            const auto name = node->getNameWithoutSign();
            line += QByteArray(name.data(), int(name.size()));
        }
        return line;
    }

    static void writeWalks(QTextStream &out, const AssemblyGraph &graph, bool onlyFullyDrawn) {
        for (auto it = graph.m_deBruijnGraphWalks.begin(); it != graph.m_deBruijnGraphWalks.end(); ++it) {
            if (onlyFullyDrawn && !pathFullyDrawn(it->walk))
                continue;
            out << getGfaWalkLine(it.key(), *it) << '\n';
        }
    }

    bool saveEntireGraph(const QString &filename,
                         const AssemblyGraph &graph) {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&file);

        for (const auto *node: graph.m_deBruijnGraphNodes) {
            if (node->isPositiveNode())
                out << getGfaSegmentLine(node, graph, graph.m_depthTag) << '\n';
        }

        QList<const DeBruijnEdge *> edgesToSave;
        for (const DeBruijnEdge *edge : graph.m_deBruijnGraphEdges) {
            if (edge->isPositiveEdge())
                edgesToSave.push_back(edge);
        }

        std::sort(edgesToSave.begin(), edgesToSave.end(), DeBruijnEdge::compareEdgePointers);

        for (const auto *edge: edgesToSave)
            out << getGfaLinkLine(edge, graph) << '\n';

        for (auto it = graph.m_deBruijnGraphPaths.begin(); it != graph.m_deBruijnGraphPaths.end(); ++it)
            out << getGfaPathLine(it.key(), *it, graph) << '\n';

        writeWalks(out, graph, false);
        return true;
    }

    bool saveVisibleGraph(const QString &filename, const AssemblyGraph &graph) {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&file);

        for (const auto *node: graph.m_deBruijnGraphNodes) {
            if (node->thisNodeOrReverseComplementIsDrawn() && node->isPositiveNode())
                out << getGfaSegmentLine(node, graph, graph.m_depthTag) << '\n';
        }

        QList<const DeBruijnEdge *> edgesToSave;
        for (const DeBruijnEdge *edge : graph.m_deBruijnGraphEdges) {
            if (edge->getStartingNode()->thisNodeOrReverseComplementIsDrawn() &&
                edge->getEndingNode()->thisNodeOrReverseComplementIsDrawn() &&
                edge->isPositiveEdge())
                edgesToSave.push_back(edge);
        }

        std::sort(edgesToSave.begin(), edgesToSave.end(), DeBruijnEdge::compareEdgePointers);

        for (const auto *node: edgesToSave)
            out << getGfaLinkLine(node, graph) << '\n';

        for (auto it = graph.m_deBruijnGraphPaths.begin(); it != graph.m_deBruijnGraphPaths.end(); ++it) {
            if (pathFullyDrawn(*it))
                out << getGfaPathLine(it.key(), *it, graph) << '\n';
        }

        writeWalks(out, graph, true);
        return true;
    }
}
