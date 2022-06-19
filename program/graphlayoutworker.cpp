// Copyright 2017 Ryan Wick
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

#include "graphlayoutworker.h"
#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"
#include "graph/debruijnedge.h"

#include "ogdf/energybased/FMMMLayout.h"
#include "ogdf/energybased/fmmm/FMMMOptions.h"

#include <ctime>

GraphLayoutWorker::GraphLayoutWorker(AssemblyGraph &graph,
                                     int graphLayoutQuality, bool useLinearLayout,
                                     double graphLayoutComponentSeparation, double aspectRatio)
        : m_layout(new ogdf::FMMMLayout),
          m_graph(graph),
          m_graphLayoutQuality(graphLayoutQuality),
          m_useLinearLayout(useLinearLayout),
          m_graphLayoutComponentSeparation(graphLayoutComponentSeparation),
          m_aspectRatio(aspectRatio) {}

static void addToOgdfGraph(DeBruijnNode *node,
                           ogdf::Graph &ogdfGraph, ogdf::GraphAttributes &graphAttributes,
                           ogdf::EdgeArray<double> &edgeLengths, double xPos, double yPos) {
    // If this node or its reverse complement is already in OGDF, then
    // it's not necessary to make the node.
    if (node->thisOrReverseComplementInOgdf())
        return;

    // Each node in the graph sense is made up of multiple nodes in the
    // OGDF sense.  This way, Velvet nodes appear as lines whose length
    // corresponds to the sequence length.
    double drawnNodeLength = node->getDrawnNodeLength();
    int numberOfGraphEdges = DeBruijnNode::getNumberOfOgdfGraphEdges(drawnNodeLength);
    int numberOfGraphNodes = numberOfGraphEdges + 1;
    double drawnLengthPerEdge = drawnNodeLength / numberOfGraphEdges;

    ogdf::node newNode = nullptr;
    ogdf::node previousNode = nullptr;
    for (int i = 0; i < numberOfGraphNodes; ++i) {
        newNode = ogdfGraph.newNode();
        node->getOgdfNode().push_back(newNode);

        if (g_settings->linearLayout) {
            graphAttributes.x(newNode) = xPos;
            graphAttributes.y(newNode) = yPos;
            xPos += g_settings->nodeSegmentLength;
        }

        if (i > 0) {
            ogdf::edge newEdge = ogdfGraph.newEdge(previousNode, newNode);
            edgeLengths[newEdge] = drawnLengthPerEdge;
        }

        previousNode = newNode;
    }
}

static void addToOgdfGraph(DeBruijnEdge *edge,
                           ogdf::Graph &ogdfGraph, ogdf::EdgeArray<double> &edgeArray) {
    ogdf::node firstEdgeOgdfNode;
    ogdf::node secondEdgeOgdfNode;

    const auto *startingNode = edge->getStartingNode();
    if (startingNode->inOgdf())
        firstEdgeOgdfNode = startingNode->getOgdfNode().back();
    else if (startingNode->getReverseComplement()->inOgdf())
        firstEdgeOgdfNode = startingNode->getReverseComplement()->getOgdfNode().front();
    else
        return; //Ending node or its reverse complement isn't in OGDF

    const auto *endingNode = edge->getEndingNode();
    if (endingNode->inOgdf())
        secondEdgeOgdfNode = endingNode->getOgdfNode().front();
    else if (endingNode->getReverseComplement()->inOgdf())
        secondEdgeOgdfNode = endingNode->getReverseComplement()->getOgdfNode().back();
    else
        return; //Ending node or its reverse complement isn't in OGDF

    // If this in an edge connected a single-segment node to itself, then we
    // don't want to put it in the OGDF graph, because it would be redundant
    // with the node segment (and created conflict with the node/edge length).
    if (startingNode == endingNode) {
        if (DeBruijnNode::getNumberOfOgdfGraphEdges(startingNode->getDrawnNodeLength()) == 1)
            return;
    }

    ogdf::edge newEdge = ogdfGraph.newEdge(firstEdgeOgdfNode, secondEdgeOgdfNode);
    edgeArray[newEdge] = g_settings->edgeLength;
}

void GraphLayoutWorker::determineLinearNodePositions() {
    auto &ogdfGraph = m_graph.m_ogdfGraph;
    auto &ogdfGraphAttributes = m_graph.m_ogdfGraphAttributes;
    auto &ogdfEdgeLengths = m_graph.m_ogdfEdgeLengths;

    QList<DeBruijnNode *> sortedDrawnNodes;

    // We first try to sort the nodes numerically.
    QList<QPair<int, DeBruijnNode *>> numericallySortedDrawnNodes;
    bool successfulIntConversion = true;
    for (auto &entry : m_graph.m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;
        if (node->isDrawn() && node->thisOrReverseComplementNotInOgdf()) {
            int nodeInt = node->getNameWithoutSign().toInt(&successfulIntConversion);
            if (!successfulIntConversion)
                break;
            numericallySortedDrawnNodes.push_back(QPair<int, DeBruijnNode*>(nodeInt, node));
        }
    }
    if (successfulIntConversion) {
        std::sort(numericallySortedDrawnNodes.begin(), numericallySortedDrawnNodes.end(),
                  [](const auto &a, const auto &b) {return a.first < b.first;});
        for (int i = 0; i < numericallySortedDrawnNodes.size(); ++i) {
            sortedDrawnNodes.reserve(numericallySortedDrawnNodes.size());
            sortedDrawnNodes.push_back(numericallySortedDrawnNodes[i].second);
        }
    } // If any of the conversions from node name to integer failed, then we instead sort the nodes alphabetically.
    else {
        for (auto &entry : m_graph.m_deBruijnGraphNodes) {
            DeBruijnNode * node = entry;
            if (node->isDrawn())
                sortedDrawnNodes.push_back(node);
        }
        std::sort(sortedDrawnNodes.begin(), sortedDrawnNodes.end(),
                  [](const DeBruijnNode *a, const DeBruijnNode *b) {
            return QString::localeAwareCompare(a->getNameWithoutSign().toUpper(), b->getNameWithoutSign().toUpper()) < 0;});
    }

    // Now we add the drawn nodes to the OGDF graph, given them initial positions based on their sort order.
    QSet<QPair<long long, long long> > usedStartPositions;
    double lastXPos = 0.0;
    for (auto node : sortedDrawnNodes) {
        if (node->thisOrReverseComplementInOgdf())
            continue;
        std::vector<DeBruijnNode *> upstreamNodes = node->getUpstreamNodes();
        for (size_t j = 0; j < upstreamNodes.size(); ++j) {
            DeBruijnNode * upstreamNode = upstreamNodes[j];
            if (!upstreamNode->inOgdf())
                continue;
            ogdf::node upstreamEnd = upstreamNode->getOgdfNode().back();
            double upstreamEndPos = ogdfGraphAttributes.x(upstreamEnd);
            if (j == 0)
                lastXPos = upstreamEndPos;
            else
                lastXPos = std::max(lastXPos, upstreamEndPos);
        }
        double xPos = lastXPos + g_settings->edgeLength;
        double yPos = 0.0;
        long long intXPos = (long long)(xPos * 100.0);
        long long intYPos = (long long)(yPos * 100.0);
        while (usedStartPositions.contains(QPair<long long, long long>(intXPos, intYPos))) {
            yPos += g_settings->edgeLength;
            intYPos = (long long)(yPos * 100.0);
        }
        addToOgdfGraph(node, ogdfGraph, ogdfGraphAttributes, ogdfEdgeLengths, xPos, yPos);
        usedStartPositions.insert(QPair<long long, long long>(intXPos, intYPos));
        lastXPos = ogdfGraphAttributes.x(node->getOgdfNode().back());
    }
}

void GraphLayoutWorker::buildGraph() {
    auto &ogdfGraph = m_graph.m_ogdfGraph;
    auto &ogdfGraphAttributes = m_graph.m_ogdfGraphAttributes;
    auto &ogdfEdgeLengths = m_graph.m_ogdfEdgeLengths;

    // If performing a linear layout, we first sort the drawn nodes and add them left-to-right.
    if (m_useLinearLayout) {
        determineLinearNodePositions();
        // If the layout isn't linear, then we don't worry about the initial positions because they'll be randomised anyway.
    } else {
        for (auto &entry : m_graph.m_deBruijnGraphNodes) {
            DeBruijnNode * node = entry;
            if (!node->isDrawn() || node->thisOrReverseComplementInOgdf())
                continue;

            addToOgdfGraph(node, ogdfGraph, ogdfGraphAttributes, ogdfEdgeLengths,
                           0.0, 0.0);
        }
    }

    //Then loop through each edge determining its drawn status and adding it to OGDF if it is drawn.
    for (auto &entry : m_graph.m_deBruijnGraphEdges) {
        DeBruijnEdge *edge = entry.second;
        if (!edge->determineIfDrawn())
            continue;

        addToOgdfGraph(edge,ogdfGraph, ogdfEdgeLengths);
    }
}

void GraphLayoutWorker::layoutGraph() {
    buildGraph();

    m_layout->randSeed(clock());
    m_layout->useHighLevelOptions(false);
    m_layout->unitEdgeLength(1.0);
    m_layout->allowedPositions(ogdf::FMMMOptions::AllowedPositions::All);
    m_layout->pageRatio(m_aspectRatio);
    m_layout->minDistCC(m_graphLayoutComponentSeparation);
    m_layout->stepsForRotatingComponents(50); // Helps to make linear graph components more horizontal.
    m_layout->edgeLengthMeasurement(ogdf::FMMMOptions::EdgeLengthMeasurement::Midpoint);
    m_layout->initialPlacementForces(m_useLinearLayout ?
                                     ogdf::FMMMOptions::InitialPlacementForces::KeepPositions :
                                     ogdf::FMMMOptions::InitialPlacementForces::RandomTime);

    switch (m_graphLayoutQuality) {
        case 0:
            m_layout->fixedIterations(3);
            m_layout->fineTuningIterations(1);
            m_layout->nmPrecision(2);
            break;
        case 1:
            m_layout->fixedIterations(15);
            m_layout->fineTuningIterations(10);
            m_layout->nmPrecision(2);
            break;
        case 2:
            m_layout->fixedIterations(30);
            m_layout->fineTuningIterations(20);
            m_layout->nmPrecision(4);
            break;
        case 3:
            m_layout->fixedIterations(60);
            m_layout->fineTuningIterations(40);
            m_layout->nmPrecision(6);
            break;
        case 4:
            m_layout->fixedIterations(120);
            m_layout->fineTuningIterations(60);
            m_layout->nmPrecision(8);
            break;
    }

    m_layout->call(m_graph.m_ogdfGraphAttributes, m_graph.m_ogdfEdgeLengths);
}

void GraphLayoutWorker::cancelLayout() {
    m_layout->fixedIterations(0);
    m_layout->fineTuningIterations(0);
    m_layout->threshold(std::numeric_limits<double>::max());
}

GraphLayoutWorker::~GraphLayoutWorker() {
    delete m_layout;
}

