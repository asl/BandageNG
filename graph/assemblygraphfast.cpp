//Copyright 2017 Ryan Wick

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
#include "ogdfnode.h"
#include "path.h"

#include "blast/blastsearch.h"
#include "command_line/commoncommandlinefunctions.h"
#include "graph/debruijnedge.h"
#include "graph/debruijnnode.h"
#include "graph/gfawrapper.hpp"
#include "graph/graphicsitemedge.h"
#include "graph/graphicsitemnode.h"
#include "ogdf/energybased/FMMMLayout.h"
#include "program/globals.h"
#include "program/graphlayoutworker.h"
#include "program/memory.h"
#include "program/settings.h"
#include "ui/myprogressdialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMapIterator>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <limits>
#include <cmath>

// TODO: 1) implement bn:Z tag
//       2) add assertions
//       3) don't throw strings
//       4) all paths are not circular
//       5) add *.layout.readToTig files support for node depths
//       6) move graph loading to separate thread
void AssemblyGraph::buildDeBruijnGraphFromGfaFast(QString fullFileName, bool *unsupportedCigar, bool *customLabels,
                                                  bool *customColours, QString *bandageOptionsError) {
    m_graphFileType = GFA;
    m_filename = fullFileName;

    QApplication::processEvents();
    auto gfaWrapper = gfa::load(fullFileName.toStdString());
    QApplication::processEvents();

    *unsupportedCigar = false;
    *customLabels = false;
    *customColours = false;

    /* ------------------------ NODES ------------------------ */
    std::vector<DeBruijnNode *> nodePtrs;
    nodePtrs.resize(gfaWrapper.verticesCount());
    for (int64_t i = 0; i < gfaWrapper.segmentsCount(); i++) {
        QApplication::processEvents();
        const auto &segment = gfaWrapper.getNthSegment(i);
        const auto &tagWrapper = segment.getTagsWrapper();

        QString nodeName = segment.getName();
        QByteArray sequence = segment.getSequence();

        //We check to see if the node ended in a "+" or "-".
        //If so, we assume that is giving the orientation and leave it.
        //And if it doesn't end in a "+" or "-", we assume "+" and add
        //that to the node name.
        if (nodeName.back() != '+' && nodeName.back() != '-')
            nodeName.push_back('+');

        // Canu nodes start with "tig" which we can remove for simplicity.
        nodeName = simplifyCanuNodeName(nodeName);

        //GFA can use * to indicate that the sequence is not in the
        //file.  In this case, try to use the LN tag for length.  If
        //that's not available, use a length of 0.
        //If there is a sequence, then the LN tag will be ignored.
        int length;
        if (sequence == "*" || sequence == "") {
            auto lnTag = tagWrapper.getNumberTag<int>("LN");
            if (!lnTag) {
                throw "expected LN tag because sequence is " + sequence;
            }
            length = *lnTag;
            sequence = "*";
        } else
            length = int(sequence.size());

        auto dp = tagWrapper.getNumberTag<float>("DP");
        auto kc = tagWrapper.getNumberTag<float>("KC");
        auto rc = tagWrapper.getNumberTag<float>("RC");
        auto fc = tagWrapper.getNumberTag<float>("FC");

        double nodeDepth = 1;
        if (dp.has_value()) {
            m_depthTag = "DP";
            nodeDepth = *dp;
        } else if (kc.has_value()) {
            m_depthTag = "KC";
            if (length != 0) nodeDepth = *kc / double(length);
        } else if (rc.has_value()) {
            m_depthTag = "RC";
            if (length != 0) nodeDepth = *rc / double(length);
        } else if (fc.has_value()) {
            m_depthTag = "FC";
            if (length != 0) nodeDepth = *fc / double(length);
        }

        auto oppositeNodeName = getOppositeNodeName(nodeName);

        auto reverseComplementSequence = getReverseComplement(sequence);

        auto node = new DeBruijnNode(nodeName, nodeDepth, sequence, length);
        auto oppositeNode = new DeBruijnNode(oppositeNodeName,
                                             nodeDepth,
                                             reverseComplementSequence,
                                             length);

        auto lb = tagWrapper.getTagString("LB");
        auto l2 = tagWrapper.getTagString("L2");
        *customLabels = *customLabels || lb != nullptr || l2 != nullptr;
        if (lb != nullptr) node->setCustomLabel(lb);
        if (l2 != nullptr) oppositeNode->setCustomLabel(l2);

        auto cl = tagWrapper.getTagString("CB");
        auto c2 = tagWrapper.getTagString("C2");
        *customColours = *customColours || cl != nullptr || c2 != nullptr;
        if (cl != nullptr) node->setCustomColour(cl);
        if (c2 != nullptr) node->setCustomColour(c2);

        node->setReverseComplement(oppositeNode);
        oppositeNode->setReverseComplement(node);

        m_deBruijnGraphNodes[nodeName] = node;
        m_deBruijnGraphNodes[oppositeNodeName] = oppositeNode;
        nodePtrs[gfa::Wrapper::getVertexId(i)] = node;
        nodePtrs[gfa::Wrapper::getComplementVertexId(gfa::Wrapper::getVertexId(i))] = oppositeNode;
    }

    /* ------------------------ EDGES ------------------------ */
    std::vector<bool> edgeUsed(gfaWrapper.linksCount());
    for (int64_t fromVertexId = 0; fromVertexId < gfaWrapper.verticesCount(); fromVertexId++) {
        QApplication::processEvents();
        auto oppositeFromVertexId = gfa::Wrapper::getComplementVertexId(fromVertexId);

        auto linksCount = gfaWrapper.countLeavingLinksFromVertex(fromVertexId);
        for (int64_t i = 0; i < linksCount; i++) {
            auto linkId = gfaWrapper.getNthLeavingLinkIdFromVertex(fromVertexId, i);
            if (edgeUsed[linkId]) continue;
            edgeUsed[linkId] = true;

            auto link = gfaWrapper.getLinkById(linkId);

            auto toVertexId = link.getEndVertexId();
            auto oppositeToVertexId = gfa::Wrapper::getComplementVertexId(toVertexId);
            int64_t oppositeLinkId = -1;
            auto oppositeLinksCount = gfaWrapper.countLeavingLinksFromVertex(oppositeToVertexId);
            for (int64_t j = 0; j < oppositeLinksCount; j++) {
                auto currentOppositeLinkId = gfaWrapper.getNthLeavingLinkIdFromVertex(oppositeToVertexId, j);

                if (gfaWrapper.getLinkById(currentOppositeLinkId).getEndVertexId() == oppositeFromVertexId) {
                    oppositeLinkId = currentOppositeLinkId;
                    break;
                }
            }
            assert(oppositeLinkId != -1);
            edgeUsed[oppositeLinkId] = true;
//            auto oppositeLink = gfaWrapper.getLinkById(oppositeLinkId);

            bool isOwnPair = fromVertexId == oppositeToVertexId && toVertexId == oppositeFromVertexId;

            auto fromNode = nodePtrs[fromVertexId];
            auto toNode = nodePtrs[toVertexId];
            auto oppositeToNode = nodePtrs[oppositeToVertexId];
            auto oppositeFromNode = nodePtrs[oppositeFromVertexId];

            auto edge = new DeBruijnEdge(fromNode, toNode);
            auto oppositeEdge = isOwnPair
                                ? edge
                                : new DeBruijnEdge(oppositeToNode, oppositeFromNode);

            edge->setReverseComplement(oppositeEdge);
            oppositeEdge->setReverseComplement(edge);

            // TODO: compare from and to overlaps
            edge->setOverlap(static_cast<int>(link.fromOverlapLength()));
            oppositeEdge->setOverlap(static_cast<int>(link.toOverlapLength()));
            edge->setOverlapType(EXACT_OVERLAP);
            oppositeEdge->setOverlapType(EXACT_OVERLAP);

            // TODO: check edge does not exists
            m_deBruijnGraphEdges[{fromNode, toNode}] = edge;
            if (!isOwnPair)
                m_deBruijnGraphEdges[{oppositeToNode, oppositeFromNode}] = oppositeEdge;

            fromNode->addEdge(edge);
            toNode->addEdge(edge);
            oppositeToNode->addEdge(oppositeEdge);
            oppositeFromNode->addEdge(oppositeEdge);
        }
    }

    /* ------------------------ PATHS ------------------------ */
    auto pathsCount = gfaWrapper.pathsCount();
    for (int64_t i = 0; i < pathsCount; i++) {
        QApplication::processEvents();
        auto path = gfaWrapper.getNthPath(i);
        QList<DeBruijnNode *> pathNodes;
        for (int64_t j = 0; j < path.length(); j++) {
            pathNodes.push_back(nodePtrs[path.getNthVertexId(j)]);
        }
        m_deBruijnGraphPaths[path.name()] = new Path(Path::makeFromOrderedNodes(pathNodes, false));
    }

    m_sequencesLoadedFromFasta = NOT_TRIED;
}
