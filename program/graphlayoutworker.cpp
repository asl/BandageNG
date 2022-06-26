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

#include "ogdf/basic/GraphCopy.h"
#include "ogdf/basic/simple_graph_alg.h"
#include "ogdf/energybased/FMMMLayout.h"
#include "ogdf/energybased/FastMultipoleEmbedder.h"
#include "ogdf/energybased/fmmm/FMMMOptions.h"
#include "ogdf/graphalg/ConvexHull.h"
#include "ogdf/packing/TileToRowsCCPacker.h"

#include <QFutureSynchronizer>
#include <QtConcurrent>

#include <ctime>

GraphLayoutWorker::GraphLayoutWorker(AssemblyGraph &graph,
                                     int graphLayoutQuality, bool useLinearLayout,
                                     double graphLayoutComponentSeparation, double aspectRatio)
        : m_graph(graph),
          m_graphLayoutQuality(graphLayoutQuality),
          m_useLinearLayout(useLinearLayout),
          m_graphLayoutComponentSeparation(graphLayoutComponentSeparation),
          m_aspectRatio(aspectRatio) {}

// FIXME: move to settings
static double getNodeLengthPerMegabase() {
    if (g_settings->nodeLengthMode == AUTO_NODE_LENGTH)
        return g_settings->autoNodeLengthPerMegabase;


    return g_settings->manualNodeLengthPerMegabase;
}

static double getDrawnNodeLength(const DeBruijnNode *node) {
    double drawnNodeLength = getNodeLengthPerMegabase() * double(node->getLength()) / 1000000.0;
    if (drawnNodeLength < g_settings->minimumNodeLength)
        drawnNodeLength = g_settings->minimumNodeLength;
    return drawnNodeLength;
}

static int getNumberOfOgdfGraphEdges(double drawnNodeLength) {
    int numberOfGraphEdges = ceil(drawnNodeLength / g_settings->nodeSegmentLength);
    if (numberOfGraphEdges <= 0)
        numberOfGraphEdges = 1;
    return numberOfGraphEdges;
}

static void addToOgdfGraph(DeBruijnNode *node,
                           ogdf::Graph &ogdfGraph, ogdf::GraphAttributes &GA,
                           ogdf::EdgeArray<double> &edgeLengths, double xPos, double yPos) {
    // If this node or its reverse complement is already in OGDF, then
    // it's not necessary to make the node.
    if (node->thisOrReverseComplementInOgdf())
        return;

    // Each node in the graph sense is made up of multiple nodes in the
    // OGDF sense.  This way, Velvet nodes appear as lines whose length
    // corresponds to the sequence length.
    double drawnNodeLength = getDrawnNodeLength(node);
    int numberOfGraphEdges = getNumberOfOgdfGraphEdges(drawnNodeLength);
    int numberOfGraphNodes = numberOfGraphEdges + 1;
    double drawnLengthPerEdge = drawnNodeLength / numberOfGraphEdges;

    ogdf::node newNode;
    ogdf::node previousNode = nullptr;
    for (int i = 0; i < numberOfGraphNodes; ++i) {
        newNode = ogdfGraph.newNode();
        node->getOgdfNode().push_back(newNode);

        if (g_settings->linearLayout) {
            GA.x(newNode) = xPos;
            GA.y(newNode) = yPos;
            xPos += g_settings->nodeSegmentLength;
        }

        GA.width(newNode) = g_settings->edgeLength;
        GA.height(newNode) = g_settings->edgeLength;

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
    if (startingNode == endingNode &&
        getNumberOfOgdfGraphEdges(getDrawnNodeLength(startingNode)) == 1)
        return;

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
    for (auto *node : m_graph.m_deBruijnGraphNodes) {
        if (!node->isDrawn())
            continue;

        int nodeInt = node->getNameWithoutSign().toInt(&successfulIntConversion);
        if (!successfulIntConversion)
            break;
        numericallySortedDrawnNodes.emplace_back(nodeInt, node);
    }

    if (successfulIntConversion) {
        std::sort(numericallySortedDrawnNodes.begin(), numericallySortedDrawnNodes.end(),
                  [](const auto &a, const auto &b) {return a.first < b.first;});
        sortedDrawnNodes.reserve(numericallySortedDrawnNodes.size());
        for (const auto &entry : numericallySortedDrawnNodes)
            sortedDrawnNodes.push_back(entry.second);
    } // If any of the conversions from node name to integer failed, then we instead sort the nodes alphabetically.
    else {
        for (auto *node : m_graph.m_deBruijnGraphNodes) {
            if (!node->isDrawn())
                continue;
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
        while (usedStartPositions.contains({intXPos, intYPos})) {
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

        if (edge->getOverlapType() == JUMP)
            continue;

        addToOgdfGraph(edge,ogdfGraph, ogdfEdgeLengths);
    }
}

void GraphLayoutWorker::initLayout(ogdf::FMMMLayout &layout) const {
    layout.randSeed(clock());
    layout.useHighLevelOptions(false);
    layout.unitEdgeLength(1.0);
    layout.allowedPositions(ogdf::FMMMOptions::AllowedPositions::All);
    layout.pageRatio(m_aspectRatio);
    layout.minDistCC(m_graphLayoutComponentSeparation);
    layout.stepsForRotatingComponents(50); // Helps to make linear graph components more horizontal.
    layout.initialPlacementForces(m_useLinearLayout ?
                                     ogdf::FMMMOptions::InitialPlacementForces::KeepPositions :
                                     ogdf::FMMMOptions::InitialPlacementForces::RandomTime);

    switch (m_graphLayoutQuality) {
        case 0:
            layout.fixedIterations(3);
            layout.fineTuningIterations(1);
            layout.nmPrecision(2);
            break;
        case 1:
            layout.fixedIterations(15);
            layout.fineTuningIterations(10);
            layout.nmPrecision(2);
            break;
        case 2:
            layout.fixedIterations(30);
            layout.fineTuningIterations(20);
            layout.nmPrecision(4);
            break;
        case 3:
            layout.fixedIterations(60);
            layout.fineTuningIterations(40);
            layout.nmPrecision(6);
            break;
        case 4:
            layout.fixedIterations(120);
            layout.fineTuningIterations(60);
            layout.nmPrecision(8);
            break;
    }
}

static void reassembleDrawings(ogdf::GraphAttributes &GA,
                               double graphLayoutComponentSeparation, double aspectRatio,
                               const ogdf::Array<ogdf::List<ogdf::node> > &nodesInCC) {
    ogdf::TileToRowsCCPacker packer;
    int numberOfComponents = nodesInCC.size();

    ogdf::Array<ogdf::IPoint> box;
    ogdf::Array<ogdf::IPoint> offset;
    ogdf::Array<ogdf::DPoint> oldOffset;
    ogdf::Array<double> rotation;
    ogdf::ConvexHull CH;

    // rotate components and create bounding rectangles

    //iterate through all components and compute convex hull
    for (int j = 0; j < numberOfComponents; j++) {
        std::vector<ogdf::DPoint> points;

        //collect node positions and at the same time center average
        // at origin
        double avg_x = 0.0;
        double avg_y = 0.0;
        for (ogdf::node v: nodesInCC[j]) {
            ogdf::DPoint dp(GA.x(v), GA.y(v));
            avg_x += dp.m_x;
            avg_y += dp.m_y;
            points.push_back(dp);
        }
        avg_x /= nodesInCC[j].size();
        avg_y /= nodesInCC[j].size();

        //adapt positions to origin
        int count = 0;
        //assume same order of vertices and positions
        for (ogdf::node v: nodesInCC[j]) {
            GA.x(v) = GA.x(v) - avg_x;
            GA.y(v) = GA.y(v) - avg_y;
            points.at(count).m_x -= avg_x;
            points.at(count).m_y -= avg_y;

            count++;
        }

        // calculate convex hull
        ogdf::DPolygon hull = CH.call(points);

        double best_area = std::numeric_limits<double>::max();
        ogdf::DPoint best_normal;
        double best_width = 0.0;
        double best_height = 0.0;

        // find best rotation by using every face as rectangle border once.
        for (ogdf::DPolygon::iterator iter = hull.begin(); iter != hull.end(); ++iter) {
            ogdf::DPolygon::iterator k = hull.cyclicSucc(iter);

            double dist = 0.0;
            ogdf::DPoint norm = CH.calcNormal(*k, *iter);
            for (const ogdf::DPoint &z: hull) {
                double d = CH.leftOfLine(norm, z, *k);
                if (d > dist) {
                    dist = d;
                }
            }

            double left = 0.0;
            double right = 0.0;
            norm = CH.calcNormal(ogdf::DPoint(0, 0), norm);
            for (const ogdf::DPoint &z: hull) {
                double d = CH.leftOfLine(norm, z, *k);
                if (d > left) {
                    left = d;
                } else if (d < right) {
                    right = d;
                }
            }
            double width = left - right;

            ogdf::Math::updateMax(dist, 1.0);
            ogdf::Math::updateMax(width, 1.0);

            double area = dist * width;

            if (area <= best_area) {
                best_height = dist;
                best_width = width;
                best_area = area;
                best_normal = CH.calcNormal(*k, *iter);
            }
        }

        if (hull.size() <= 1) {
            best_height = 1.0;
            best_width = 1.0;
            best_normal = ogdf::DPoint(1.0, 1.0);
        }

        double angle = -atan2(best_normal.m_y, best_normal.m_x) + 1.5 * ogdf::Math::pi;
        if (best_width < best_height) {
            angle += 0.5f * ogdf::Math::pi;
            double temp = best_height;
            best_height = best_width;
            best_width = temp;
        }
        rotation.grow(1, angle);
        double left = hull.front().m_x;
        double top = hull.front().m_y;
        double bottom = hull.front().m_y;
        // apply rotation to hull and calc offset
        for (ogdf::DPoint tempP: hull) {
            double ang = atan2(tempP.m_y, tempP.m_x);
            double len = sqrt(tempP.m_x * tempP.m_x + tempP.m_y * tempP.m_y);
            ang += angle;
            tempP.m_x = cos(ang) * len;
            tempP.m_y = sin(ang) * len;

            if (tempP.m_x < left) {
                left = tempP.m_x;
            }
            if (tempP.m_y < top) {
                top = tempP.m_y;
            }
            if (tempP.m_y > bottom) {
                bottom = tempP.m_y;
            }
        }
        oldOffset.grow(1, ogdf::DPoint(left + 0.5 * graphLayoutComponentSeparation,
                                       -1.0 * best_height + 1.0 * bottom + 0.0 * top +
                                       0.5 * graphLayoutComponentSeparation));

        // save rect
        int w = static_cast<int>(best_width);
        int h = static_cast<int>(best_height);
        box.grow(1, ogdf::IPoint(w + graphLayoutComponentSeparation, h + graphLayoutComponentSeparation));
    }

    offset.init(box.size());

    // call packer
    packer.call(box, offset, aspectRatio);

    int index = 0;
    // Apply offset and rebuild Graph
    for (int j = 0; j < numberOfComponents; j++) {
        double angle = rotation[index];
        // apply rotation and offset to all nodes

        for (ogdf::node v: nodesInCC[j]) {
            double x = GA.x(v);
            double y = GA.y(v);
            double ang = atan2(y, x);
            double len = sqrt(x * x + y * y);
            ang += angle;
            x = cos(ang) * len;
            y = sin(ang) * len;

            x += static_cast<double>(offset[index].m_x);
            y += static_cast<double>(offset[index].m_y);

            x -= oldOffset[index].m_x;
            y -= oldOffset[index].m_y;

            GA.x(v) = x;
            GA.y(v) = y;
        }

        index++;
    }
}


void GraphLayoutWorker::layoutGraph() {
    buildGraph();

    //first we split the graph into its components
    ogdf::GraphAttributes &GA = m_graph.m_ogdfGraphAttributes;
    const ogdf::Graph& G = GA.constGraph();
    const ogdf::EdgeArray<double> &EL = m_graph.m_ogdfEdgeLengths;

    ogdf::NodeArray<int> componentNumber(G);
    int numberOfComponents = connectedComponents(G, componentNumber);
    if (numberOfComponents == 0)
        return;

    ogdf::Array<ogdf::List<ogdf::node> > nodesInCC(numberOfComponents);
    for (auto v : G.nodes)
        nodesInCC[componentNumber[v]].pushBack(v);

    m_layout.resize(numberOfComponents);
    for (int i = 0; i < numberOfComponents; i++) {
        initLayout(m_layout[i]);
    }

    for (int i = 0; i < numberOfComponents; i++) {
        m_taskSynchronizer.addFuture(
                QtConcurrent::run([&](ogdf::FMMMLayout *layout,
                        const ogdf::List<ogdf::node> &nodesInCC) {

                    ogdf::GraphCopy GC;
                    ogdf::EdgeArray<double> edgeLengths(GC);
                    ogdf::EdgeArray<ogdf::edge> auxCopy(G);

                    GC.createEmpty(G);

                    GC.initByNodes(nodesInCC, auxCopy);
                    ogdf::GraphAttributes cGA(GC, GA.attributes());
                    for (ogdf::node v : GC.nodes) {
                        cGA.x(v) = GA.x(GC.original(v));
                        cGA.y(v) = GA.y(GC.original(v));
                        cGA.width(v) = GA.width(GC.original(v));
                        cGA.height(v) = GA.height(GC.original(v));
                    }

                    for (ogdf::edge e : GC.edges)
                        edgeLengths(e) = EL(GC.original(e));

                    layout->call(cGA, edgeLengths);

                    for (ogdf::node v : GC.nodes) {
                        ogdf::node w = GC.original(v);
                        if (w == nullptr)
                            continue;

                        GA.x(w) = cGA.x(v);
                        GA.y(w) = cGA.y(v);
                    }

                }, &m_layout[i], nodesInCC[i]));
    }
    m_taskSynchronizer.waitForFinished();

    reassembleDrawings(GA,
                       m_graphLayoutComponentSeparation, m_aspectRatio,
                       nodesInCC);
}

[[maybe_unused]] void GraphLayoutWorker::cancelLayout() {
    for (auto & layout : m_layout) {
        layout.fixedIterations(0);
        layout.fineTuningIterations(0);
        layout.threshold(std::numeric_limits<double>::max());
    }
    for (auto & future : m_taskSynchronizer.futures())
        future.cancel();
}
