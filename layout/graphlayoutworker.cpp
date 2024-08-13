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

#include "program/settings.h"

#include "ogdf/basic/GraphCopy.h"
#include "ogdf/basic/simple_graph_alg.h"
#include "ogdf/energybased/FMMMLayout.h"
#include "ogdf/energybased/fmmm/MAARPacking.h"
#include "ogdf/energybased/FastMultipoleEmbedder.h"
#include "ogdf/energybased/fmmm/FMMMOptions.h"

#include <QFutureSynchronizer>
#include <QtConcurrent>

#include <ctime>

GraphLayouter::GraphLayouter(int graphLayoutQuality, bool useLinearLayout,
                             double graphLayoutComponentSeparation, double aspectRatio)
        :   m_graphLayoutQuality(graphLayoutQuality),
            m_useLinearLayout(useLinearLayout),
            m_graphLayoutComponentSeparation(graphLayoutComponentSeparation),
            m_aspectRatio(aspectRatio)
{}

class FMMGraphLayout : public GraphLayouter {
public:
    using GraphLayouter::GraphLayouter;

    void init() override {
        init(m_layout);
    }

    void cancel() override {
        m_layout.fixedIterations(0);
        m_layout.fineTuningIterations(0);
        m_layout.threshold(std::numeric_limits<double>::max());
    }

    void run(ogdf::GraphAttributes &GA, const ogdf::EdgeArray<double> &edges) override {
        m_layout.call(GA, edges);
    }

private:
    void init(ogdf::FMMMLayout &layout) const {
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

    ogdf::FMMMLayout m_layout;
};

GraphLayoutWorker::GraphLayoutWorker(int graphLayoutQuality, bool useLinearLayout,
                                     double graphLayoutComponentSeparation, double aspectRatio)
        : m_graphLayoutQuality(graphLayoutQuality),
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

using OGDFGraphLayout = GraphLayoutStorage<ogdf::node>;

static void addToOgdfGraph(DeBruijnNode *node,
                           ogdf::Graph &ogdfGraph, ogdf::GraphAttributes &GA,
                           ogdf::EdgeArray<double> &edgeLengths,
                           OGDFGraphLayout &layout,
                           double xPos, double yPos, bool linearLayout) {
    // If this node or its reverse complement is already in OGDF, then
    // it's not necessary to make the node.
    if (layout.contains(node) || layout.contains(node->getReverseComplement()))
        return;

    // Each node in the graph sense is made up of multiple nodes in the
    // OGDF sense.  This way, graph nodes appear as lines whose length
    // corresponds to the sequence length.
    double drawnNodeLength = getDrawnNodeLength(node);
    int numberOfGraphEdges = getNumberOfOgdfGraphEdges(drawnNodeLength);
    int numberOfGraphNodes = numberOfGraphEdges + 1;
    double drawnLengthPerEdge = drawnNodeLength / numberOfGraphEdges;

    ogdf::node newNode;
    ogdf::node previousNode = nullptr;
    for (int i = 0; i < numberOfGraphNodes; ++i) {
        newNode = ogdfGraph.newNode();
        layout.add(node, newNode);

        if (linearLayout) {
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

static void addToOgdfGraph(const DeBruijnEdge *edge,
                           ogdf::Graph &ogdfGraph, ogdf::EdgeArray<double> &edgeArray,
                           const OGDFGraphLayout &layout) {
    ogdf::node firstEdgeOgdfNode;
    ogdf::node secondEdgeOgdfNode;

    const auto *startingNode = edge->getStartingNode();
    if (layout.contains(startingNode))
        firstEdgeOgdfNode = layout.segments(startingNode).back();
    else if (layout.contains(startingNode->getReverseComplement()))
        firstEdgeOgdfNode = layout.segments(startingNode->getReverseComplement()).front();
    else
        return; //Ending node or its reverse complement isn't in OGDF

    const auto *endingNode = edge->getEndingNode();
    if (layout.contains(endingNode))
        secondEdgeOgdfNode = layout.segments(endingNode).front();
    else if (layout.contains(endingNode->getReverseComplement()))
        secondEdgeOgdfNode = layout.segments(endingNode->getReverseComplement()).back();
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

void determineLinearNodePositions(ogdf::Graph &ogdfGraph,
                                  ogdf::GraphAttributes &ogdfGraphAttributes,
                                  ogdf::EdgeArray<double> &ogdfEdgeLengths,
                                  OGDFGraphLayout &layout) {
    const AssemblyGraph &graph = layout.graph();
    QList<DeBruijnNode *> sortedDrawnNodes;

    // We first try to sort the nodes numerically.
    QList<QPair<int, DeBruijnNode *>> numericallySortedDrawnNodes;
    bool successfulIntConversion = true;
    for (auto *node : graph.m_deBruijnGraphNodes) {
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
        for (auto *node : graph.m_deBruijnGraphNodes) {
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
    for (auto *node : sortedDrawnNodes) {
        if (layout.contains(node) || layout.contains(node->getReverseComplement()))
            continue;
        std::vector<DeBruijnNode *> upstreamNodes = node->getUpstreamNodes();
        for (size_t j = 0; j < upstreamNodes.size(); ++j) {
            DeBruijnNode * upstreamNode = upstreamNodes[j];
            if (!layout.contains(upstreamNode))
                continue;

            ogdf::node upstreamEnd = layout.segments(upstreamNode).back();
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
        addToOgdfGraph(node, ogdfGraph, ogdfGraphAttributes, ogdfEdgeLengths, layout, xPos, yPos, true);
        usedStartPositions.insert(QPair<long long, long long>(intXPos, intYPos));
        lastXPos = ogdfGraphAttributes.x(layout.segments(node).back());
    }
}

static void buildGraph(ogdf::Graph &ogdfGraph,
                       ogdf::GraphAttributes &ogdfGraphAttributes,
                       ogdf::EdgeArray<double> &ogdfEdgeLengths,
                       OGDFGraphLayout &layout,
                       bool useLinearLayout) {
    const AssemblyGraph &graph = layout.graph();
    // If performing a linear layout, we first sort the drawn nodes and add them left-to-right.
    if (useLinearLayout) {
        determineLinearNodePositions(ogdfGraph, ogdfGraphAttributes, ogdfEdgeLengths,
                                     layout);
        // If the layout isn't linear, then we don't worry about the initial positions because they'll be randomised anyway.
    } else {
        for (auto *node : graph.m_deBruijnGraphNodes) {
            if (!node->isDrawn() ||
                layout.contains(node) || layout.contains(node->getReverseComplement()))
                continue;

            addToOgdfGraph(node,
                           ogdfGraph, ogdfGraphAttributes, ogdfEdgeLengths, layout,
                           0.0, 0.0, false);
        }
    }

    // Then loop through each edge determining its drawn status and adding it to OGDF if it is drawn.
    for (const DeBruijnEdge *edge : graph.m_deBruijnGraphEdges) {
        if (!edge->isVisible())
            continue;

        if (edge->getOverlapType() == JUMP || edge->getOverlapType() == EXTRA_LINK)
            continue;

        addToOgdfGraph(edge,ogdfGraph, ogdfEdgeLengths, layout);
    }
}

using namespace ogdf::energybased;

static fmmm::Rectangle
calculateBoundingRectangle(const ogdf::GraphAttributes &GA,
                           const ogdf::List<ogdf::node> &nodesInCC,
                           double graphLayoutComponentSeparation,
                           int componentIndex) {
    ogdf::node first = nodesInCC.front();
    fmmm::Rectangle r;

    // max_boundary is the maximum of half of the width and half of the
    // height of each node; (needed to be able to tip rectangles over
    // without having access to the height and width of each node)
    double max_boundary = std::max(GA.width(first) / 2, GA.height(first) / 2);
    double x_min = GA.x(first) - max_boundary,
            x_max = GA.x(first) + max_boundary,
            y_min = GA.y(first) - max_boundary,
            y_max = GA.y(first) + max_boundary;

    for (ogdf::node v: nodesInCC) {
        max_boundary = std::max(GA.width(v) / 2, GA.height(v) / 2);
        double act_x_min = GA.x(v) - max_boundary,
                act_x_max = GA.x(v) + max_boundary,
                act_y_min = GA.y(v) - max_boundary,
                act_y_max = GA.y(v) + max_boundary;
        if (act_x_min < x_min) x_min = act_x_min;
        if (act_x_max > x_max) x_max = act_x_max;
        if (act_y_min < y_min) y_min = act_y_min;
        if (act_y_max > y_max) y_max = act_y_max;
    }

    // add offset
    x_min -= graphLayoutComponentSeparation / 2;
    x_max += graphLayoutComponentSeparation / 2;
    y_min -= graphLayoutComponentSeparation / 2;
    y_max += graphLayoutComponentSeparation / 2;

    r.set_rectangle(x_max - x_min, y_max - y_min, x_min, y_min, componentIndex);

    return r;
}

static ogdf::List<fmmm::Rectangle>
calculateBoundingRectanglesOfComponents(const ogdf::GraphAttributes &GA,
                                        const ogdf::Array<ogdf::List<ogdf::node> > &nodesInCC,
                                        double graphLayoutComponentSeparation) {
    int numCCs = nodesInCC.size();
    ogdf::List<fmmm::Rectangle> R;

    for (int i = 0; i < numCCs; ++i) {
        auto r = calculateBoundingRectangle(GA, nodesInCC[i],
                                            graphLayoutComponentSeparation, i);
        R.pushBack(r);
    }

    return R;
}

static double calculateArea(double width, double height, int comp_nr,
                            double aspectRatio) {
    double scaling = 1.0;
    if (comp_nr == 1) {  //calculate aspect ratio area of the rectangle
        double ratio = width / height;
        if (ratio < aspectRatio) { //scale width
            scaling = aspectRatio / ratio;
        } else { //scale height
            scaling = ratio / aspectRatio;
        }
    }
    return width * height * scaling;
}

static ogdf::List<fmmm::Rectangle>
rotateComponentsAndCalculateBoundingRectangles(
        ogdf::GraphAttributes &GA,
        const ogdf::Array<ogdf::List<ogdf::node> > &nodesInCC,
        double graphLayoutComponentSeparation, double aspectRatio,
        int stepsForRotatingComponents = 50) {
    int numCCs = nodesInCC.size();
    const ogdf::Graph &G = const_cast<ogdf::Graph &>(GA.constGraph());

    ogdf::List<fmmm::Rectangle> R;

    for (int i = 0; i < numCCs; i++) {
        fmmm::Rectangle r_act, r_best;
        ogdf::DPoint new_pos, new_dlc;

        ogdf::NodeArray<ogdf::DPoint> best_coords(G), old_coords(G);
        //init r_best, best_area and best_(old)coords
        r_best = calculateBoundingRectangle(GA, nodesInCC[i],
                                            graphLayoutComponentSeparation, i);
        double best_area = calculateArea(r_best.get_width(), r_best.get_height(),
                                         numCCs, aspectRatio);
        for (ogdf::node v: nodesInCC[i])
            old_coords[v] = best_coords[v] = GA.point(v);

        //rotate the components
        for (int j = 1; j <= stepsForRotatingComponents; j++) {
            //calculate new positions for the nodes, the new rectangle and area
            double angle = ogdf::Math::pi_2 * (double(j) / double(stepsForRotatingComponents + 1));
            double sin_j = sin(angle);
            double cos_j = cos(angle);
            for (ogdf::node v: nodesInCC[i]) {
                new_pos.m_x = cos_j * old_coords[v].m_x
                              - sin_j * old_coords[v].m_y;
                new_pos.m_y = sin_j * old_coords[v].m_x
                              + cos_j * old_coords[v].m_y;
                GA.x(v) = new_pos.m_x;
                GA.y(v) = new_pos.m_y;
            }

            r_act = calculateBoundingRectangle(GA, nodesInCC[i],
                                               graphLayoutComponentSeparation, i);
            double act_area = calculateArea(r_act.get_width(), r_act.get_height(),
                                            numCCs, aspectRatio);

            double act_area_PI_half_rotated;
            if (numCCs == 1)
                act_area_PI_half_rotated = calculateArea(r_act.get_height(),
                                                         r_act.get_width(),
                                                         numCCs, aspectRatio);

            // store placement of the nodes with minimal area (in case that
            // number_of_components >1) else store placement with minimal aspect ratio area
            if (act_area < best_area) {
                r_best = r_act;
                best_area = act_area;
                for (ogdf::node v: nodesInCC[i])
                    best_coords[v] = GA.point(v);
            } else if ((numCCs == 1) && (act_area_PI_half_rotated <
                                         best_area)) { //test if rotating further with PI_half would be an improvement
                r_best = r_act;
                best_area = act_area_PI_half_rotated;
                for (ogdf::node v: nodesInCC[i])
                    best_coords[v] = GA.point(v);
                //the needed rotation step follows in the next if statement
            }
        }

        //tipp the smallest rectangle over by angle PI/2 around the origin if it makes the
        //aspect_ratio of r_best more similar to the desired aspect_ratio
        double ratio = r_best.get_width() / r_best.get_height();
        if ((aspectRatio < 1 && ratio > 1) || (aspectRatio >= 1 && ratio < 1)) {
            for (ogdf::node v: nodesInCC[i]) {
                new_pos.m_x = best_coords[v].m_y * (-1);
                new_pos.m_y = best_coords[v].m_x;
                best_coords[v] = new_pos;
            }

            //calculate new rectangle
            new_dlc.m_x = r_best.get_old_dlc_position().m_y * (-1) - r_best.get_height();
            new_dlc.m_y = r_best.get_old_dlc_position().m_x;

            double new_width = r_best.get_height();
            double new_height = r_best.get_width();
            r_best.set_width(new_width);
            r_best.set_height(new_height);
            r_best.set_old_dlc_position(new_dlc);
        }

        //save the computed information in A_sub and R
        for (ogdf::node v: nodesInCC[i]) {
            GA.x(v) = best_coords[v].m_x;
            GA.y(v) = best_coords[v].m_y;
        }

        R.pushBack(r_best);
    }

    return R;
}

static void reassembleDrawings(ogdf::GraphAttributes &GA,
                               double graphLayoutComponentSeparation, double aspectRatio,
                               const ogdf::Array<ogdf::List<ogdf::node> > &nodesInCC) {
#if 0
    auto R =
            calculateBoundingRectanglesOfComponents(GA, nodesInCC,
                                                    graphLayoutComponentSeparation);
#else
    auto R =
            rotateComponentsAndCalculateBoundingRectangles(GA, nodesInCC,
                                                           graphLayoutComponentSeparation, aspectRatio);
#endif

    double aspect_ratio_area, bounding_rectangles_area;
    fmmm::MAARPacking().pack_rectangles_using_Best_Fit_strategy(R, aspectRatio,
                                                                ogdf::FMMMOptions::PreSort::DecreasingHeight,
                                                                ogdf::FMMMOptions::TipOver::NoGrowingRow,
                                                                aspect_ratio_area, bounding_rectangles_area);

    for (const auto &r: R) {
        int i = r.get_component_index();
        if (r.is_tipped_over()) {
            // calculate tipped coordinates of the nodes
            for (auto v: nodesInCC[i]) {
                ogdf::DPoint tipped_pos(-GA.y(v), GA.x(v));
                GA.x(v) = tipped_pos.m_x;
                GA.y(v) = tipped_pos.m_y;
            }
        }

        for (auto v: nodesInCC[i]) {
            ogdf::DPoint newpos = GA.point(v);
            newpos += r.get_new_dlc_position();
            newpos -= r.get_old_dlc_position();
            GA.x(v) = newpos.m_x;
            GA.y(v) = newpos.m_y;
        }
    }
}

GraphLayout GraphLayoutWorker::layoutGraph(const AssemblyGraph &graph) {
    ogdf::Graph G;
    ogdf::EdgeArray<double> edgeLengths(G);
    ogdf::GraphAttributes GA(G,
                             ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    OGDFGraphLayout layout(graph);
    buildGraph(G, GA, edgeLengths, layout, m_useLinearLayout);

    //first we split the graph into its components
    ogdf::NodeArray<int> componentNumber(G);
    int numberOfComponents = connectedComponents(G, componentNumber);
    if (numberOfComponents == 0)
        return GraphLayout(graph);

    ogdf::Array<ogdf::List<ogdf::node> > nodesInCC(numberOfComponents);
    for (auto v : G.nodes)
        nodesInCC[componentNumber[v]].pushBack(v);

    for (size_t i= 0; i < numberOfComponents; ++i) {
        m_state.emplace_back(new FMMGraphLayout(m_graphLayoutQuality,
                                               m_useLinearLayout,
                                               m_graphLayoutComponentSeparation,
                                               m_aspectRatio));
        m_state.back()->init();
    }

    for (int i = 0; i < numberOfComponents; i++) {
        m_taskSynchronizer.addFuture(
                QtConcurrent::run([&](GraphLayouter *layout,
                        const ogdf::List<ogdf::node> &nodesInCC) {

                    ogdf::GraphCopy GC;
                    ogdf::EdgeArray<double> cedgeLengths(GC);
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
                        cedgeLengths(e) = edgeLengths(GC.original(e));

                    layout->run(cGA, cedgeLengths);

                    for (ogdf::node v : GC.nodes) {
                        ogdf::node w = GC.original(v);
                        if (w == nullptr)
                            continue;

                        GA.x(w) = cGA.x(v);
                        GA.y(w) = cGA.y(v);
                    }

                }, m_state[i].get(), nodesInCC[i]));
    }
    m_taskSynchronizer.waitForFinished();

    reassembleDrawings(GA,
                       m_graphLayoutComponentSeparation, m_aspectRatio,
                       nodesInCC);

    GraphLayout res(graph);
    for (const auto & entry : layout) {
        for (ogdf::node node : entry.second) {
            res.add(entry.first, { GA.x(node), GA.y(node) });
        }
    }
    // In double mode add layout for the reverse-complement nodes (in opposite direction)
    for (const auto & entry : layout) {
        auto *rcNode = entry.first->getReverseComplement();
        if (!rcNode->isDrawn())
            continue;
        for (auto rIt = entry.second.rbegin(); rIt != entry.second.rend(); ++rIt)
            res.add(rcNode, { GA.x(*rIt), GA.y(*rIt) });
    }

    return res;
}

[[maybe_unused]] void GraphLayoutWorker::cancelLayout() {
    for (auto &layouter : m_state)
        layouter->cancel();
    for (auto & future : m_taskSynchronizer.futures())
        future.cancel();
}
