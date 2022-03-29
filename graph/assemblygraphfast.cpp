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
#include "ui/myprogressdialog.h"

#include <QApplication>
#include <QList>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>

void AssemblyGraph::buildDeBruijnGraphFromGfa(const QString &fullFileName,
                                              bool *unsupportedCigar,
                                              bool *customLabels,
                                              bool *customColours,
                                              QString *bandageOptionsError) {
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
                throw AssemblyGraphError("expected LN tag because sequence is " + sequence);
            }
            length = lnTag.value();
            sequence = "*";
        } else
            length = int(sequence.size());

        auto dp = tagWrapper.getNumberTag<float>("DP");
        auto kc = tagWrapper.getNumberTag<float>("KC");
        auto rc = tagWrapper.getNumberTag<float>("RC");
        auto fc = tagWrapper.getNumberTag<float>("FC");

        double nodeDepth = 0;
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

        auto nodePtr = new DeBruijnNode(nodeName, nodeDepth, sequence, length);
        auto oppositeNodePtr = new DeBruijnNode(oppositeNodeName,
                                                nodeDepth,
                                                reverseComplementSequence,
                                                length);

        auto lb = tagWrapper.getTagString("LB");
        auto l2 = tagWrapper.getTagString("L2");
        *customLabels = *customLabels || lb != nullptr || l2 != nullptr;
        if (lb != nullptr) nodePtr->setCustomLabel(lb);
        if (l2 != nullptr) oppositeNodePtr->setCustomLabel(l2);

        auto cl = tagWrapper.getTagString("CB");
        auto c2 = tagWrapper.getTagString("C2");
        *customColours = *customColours || cl != nullptr || c2 != nullptr;
        if (cl != nullptr) nodePtr->setCustomColour(cl);
        if (c2 != nullptr) oppositeNodePtr->setCustomColour(c2);

        nodePtr->setReverseComplement(oppositeNodePtr);
        oppositeNodePtr->setReverseComplement(nodePtr);

        m_deBruijnGraphNodes[nodeName] = nodePtr;
        m_deBruijnGraphNodes[oppositeNodeName] = oppositeNodePtr;
        nodePtrs[gfa::Wrapper::getVertexId(i)] = nodePtr;
        nodePtrs[gfa::Wrapper::getComplementVertexId(gfa::Wrapper::getVertexId(i))] = oppositeNodePtr;
    }

    /* ------------------------ EDGES ------------------------ */
    std::vector<DeBruijnEdge *> linkIdToEdgePtr(gfaWrapper.edgesCount());
    for (int64_t i = 0; i < gfaWrapper.edgesCount(); i++) {
        auto gfaEdge = gfaWrapper.getEdgeById(i);
        auto fromNodePtr = nodePtrs[gfaEdge.getStartVertexId()];
        auto toNodePtr = nodePtrs[gfaEdge.getEndVertexId()];
        auto edgePtr = new DeBruijnEdge(fromNodePtr, toNodePtr);

        if (gfaEdge.fromOverlapLength() != gfaEdge.toOverlapLength()) {
            throw AssemblyGraphError("Non-exact overlaps in gfa are not supported.");
        }
        edgePtr->setOverlap(static_cast<int>(gfaEdge.fromOverlapLength()));
        edgePtr->setOverlapType(EXACT_OVERLAP);

        bool isOwnPair = fromNodePtr == toNodePtr->getReverseComplement()
            && toNodePtr == fromNodePtr->getReverseComplement();

        auto oppositeEdgePtr = isOwnPair ? edgePtr : linkIdToEdgePtr[gfaEdge.getLinkId()];
        if (oppositeEdgePtr != nullptr) {
            edgePtr->setReverseComplement(oppositeEdgePtr);
            oppositeEdgePtr->setReverseComplement(edgePtr);
        }
        linkIdToEdgePtr[gfaEdge.getLinkId()] = edgePtr;

        auto &oldEdgePtr = m_deBruijnGraphEdges[{fromNodePtr, toNodePtr}];
        if (oldEdgePtr != nullptr) {
            throw AssemblyGraphError("Multiple edges are not supported.");
        }
        oldEdgePtr = edgePtr;

        fromNodePtr->addEdge(edgePtr);
        toNodePtr->addEdge(edgePtr);
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

    tryUpdateNodeDepthsForCanuGraphs();

    m_sequencesLoadedFromFasta = NOT_TRIED;
}
