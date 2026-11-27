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


#include "bandagegraphicsscene.h"
#include "graph/nodedisplayname.h"
#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"
#include "graph/debruijnedge.h"
#include "graph/graphicsitemnode.h"
#include "graph/graphicsitemedge.h"
#include "graph/graphicsitemlink.h"
#include "layout/graphlayout.h"
#include "program/globals.h"
#include "program/settings.h"

#include <unordered_set>

BandageGraphicsScene::BandageGraphicsScene(QObject *parent) :
    QGraphicsScene(parent)
{ }


static bool compareNodePointers(OrientedNode a, OrientedNode b) {
    QString aName = nodeDisplayName(a);
    QString bName = nodeDisplayName(b);

    QString aName2 = aName;
    aName2.chop(1);
    QString bName2 = bName;
    bName2.chop(1);

    bool ok1, ok2;
    long long aNum = aName2.toLongLong(&ok1);
    long long bNum = bName2.toLongLong(&ok2);

    //If the node names are essentially numbers, then sort them as numbers
    if (ok1 && ok2 && aNum != bNum)
        return aNum < bNum;

    return aName < bName;
}

// This function returns all of the selected nodes, sorted by their node number.
std::vector<OrientedNode> BandageGraphicsScene::getSelectedNodes() {
    std::vector<OrientedNode> returnVector;

    for (auto *selectedItem : selectedItems())
        if (auto *selectedNodeItem = dynamic_cast<GraphicsItemNode *>(selectedItem))
            returnVector.push_back(selectedNodeItem->m_deBruijnNode);

    std::sort(returnVector.begin(), returnVector.end(), compareNodePointers);

    return returnVector;
}

// This function works like getSelectedNodes, but only positive nodes are
// returned.  If a negative node is selected, its positive complement is in the
// results.  If both nodes in a pair are selected, then only the positive node
// of the pair is in the results.
std::vector<OrientedNode> BandageGraphicsScene::getSelectedPositiveNodes() {
    std::vector<OrientedNode> selectedNodes = getSelectedNodes();

    //First turn all of the nodes to positive nodes.
    std::unordered_set<OrientedNode> allPositive;
    for (OrientedNode node : selectedNodes)
        allPositive.insert(node.getCanonical());

    return { allPositive.begin(), allPositive.end() };
}

//This function returns all of the selected graphics item nodes, unsorted.
std::vector<GraphicsItemNode *> BandageGraphicsScene::getSelectedGraphicsItemNodes() {
    std::vector<GraphicsItemNode *> returnVector;

    for (auto *selectedItem : selectedItems())
        if (auto *selectedNodeItem = dynamic_cast<GraphicsItemNode *>(selectedItem))
            returnVector.push_back(selectedNodeItem);

    return returnVector;
}


std::vector<DeBruijnEdge *> BandageGraphicsScene::getSelectedEdges() {
    std::vector<DeBruijnEdge *> returnVector;

    for (auto *selectedItem : selectedItems())
        if (auto *selectedEdgeItem = dynamic_cast<GraphicsItemEdge *>(selectedItem))
            returnVector.push_back(selectedEdgeItem->edge());

    return returnVector;
}

OrientedNode BandageGraphicsScene::getOneSelectedNode() {
    std::vector<OrientedNode> selectedNodes = getSelectedNodes();
    if (selectedNodes.empty())
        return nullptr;

    return selectedNodes.front();
}

DeBruijnEdge *BandageGraphicsScene::getOneSelectedEdge()
{
    std::vector<DeBruijnEdge *> selectedEdges = getSelectedEdges();
    if (selectedEdges.empty())
        return nullptr;

    return selectedEdges.front();
}

//This function, like getOneSelectedNode, returns a single selected node from
//graph and a 0 if it can't be done.  However, it will always return the
//positive node in the pair, and if two complementary nodes are selected, it
//will still work.
OrientedNode BandageGraphicsScene::getOnePositiveSelectedNode()
{
    std::vector<OrientedNode> selectedNodes = getSelectedNodes();
    if (selectedNodes.empty())
        return nullptr;

    if (selectedNodes.size() == 1)
        return selectedNodes.front().getCanonical();

    if (selectedNodes.size() == 2) {
        OrientedNode selectedNode1 = selectedNodes[0];
        OrientedNode selectedNode2 = selectedNodes[1];
        if (selectedNode1.reverse() != selectedNode2)
            return nullptr;

        return selectedNode1.getCanonical();
    }

    return nullptr;
}

double BandageGraphicsScene::getTopZValue()
{
    double topZ = 0.0;

    QList<QGraphicsItem *> allItems = items();
    for (int i = 0; i < allItems.size(); ++i)
    {
        QGraphicsItem *item = allItems[i];
        if (i == 0)
            topZ = item->zValue();
        else
        {
            double z = item->zValue();
            if (z > topZ)
                topZ = z;
        }
    }

    return topZ;
}


//Expands the scene rectangle a bit beyond the items, so they aren't drawn right to the edge.
void BandageGraphicsScene::setSceneRectangle()
{
    QRectF boundingRect = itemsBoundingRect();
    double width = boundingRect.width();
    double height = boundingRect.height();
    double margin = std::max(width, height);
    margin *= 0.05; //5% margin

    setSceneRect(boundingRect.left() - margin, boundingRect.top() - margin,
                 width + 2 * margin, height + 2 * margin);
}



//After the user drags nodes, it may be necessary to expand the scene rectangle
//if the nodes were moved out of the existing rectangle.
void BandageGraphicsScene::possiblyExpandSceneRectangle(std::vector<GraphicsItemNode *> * movedNodes)
{
    QRectF currentSceneRect = sceneRect();
    QRectF newSceneRect = currentSceneRect;

    for (auto node : *movedNodes)
    {
        QRectF nodeRect = node->boundingRect();
        newSceneRect = newSceneRect.united(nodeRect);
    }

    if (newSceneRect != currentSceneRect)
        setSceneRect(newSceneRect);
}

void BandageGraphicsScene::recolourNodeGraphics(AssemblyGraph &graph) {
    for (OrientedNode positive : graph.canonical_nodes()) {
        GraphicsItemNode *positiveItem = positive.getGraphicsItemNode();
        GraphicsItemNode *negativeItem = positive.isSelfComplement() ? nullptr
                                                                     : positive.reverse().getGraphicsItemNode();
        if (positiveItem != nullptr && negativeItem != nullptr) {
            auto colours = g_settings->nodeColorer->get(positiveItem, negativeItem);
            positiveItem->setNodeColour(colours.first);
            negativeItem->setNodeColour(colours.second);
            continue;
        }
        if (positiveItem != nullptr)
            positiveItem->setNodeColour(g_settings->nodeColorer->get(positiveItem));
        if (negativeItem != nullptr)
            negativeItem->setNodeColour(g_settings->nodeColorer->get(negativeItem));
    }
}

void BandageGraphicsScene::addGraphicsItemsToScene(AssemblyGraph &graph,
                                                   const GraphLayout &layout) {
    graph.clearNodeGraphicsItems();
    graph.clearEdgeGraphicsItems();
    clear();

    double meanDrawnDepth = graph.getMeanDepth(true);

    // First make the GraphicsItemNode objects
    for (auto &entry : layout) {
        OrientedNode node = entry.first;
        if (!node.isDrawn())
            continue;

        auto *graphicsItemNode =
                new GraphicsItemNode(node,
                                     meanDrawnDepth == 0 ? 1.0 : node.getDepth() / meanDrawnDepth,
                                     entry.second);
        // If we are in double mode and this node's complement is also drawn,
        // then we should shift the points so the two nodes are not drawn directly
        // on top of each other. A palindrome is its own reverse and is not shifted.
        const bool twoOrientations = node && !node.isSelfComplement();
        if (g_settings->doubleMode && twoOrientations && node.reverse().isDrawn())
            graphicsItemNode->shiftPointsLeft();

        node.setGraphicsItemNode(graphicsItemNode);
        graphicsItemNode->setFlag(QGraphicsItem::ItemIsSelectable);
        graphicsItemNode->setFlag(QGraphicsItem::ItemIsMovable);

        bool colSet = false;
        if (twoOrientations) {
            if (auto *revCompGraphNode = node.reverse().getGraphicsItemNode()) {
                auto colPair = g_settings->nodeColorer->get(graphicsItemNode, revCompGraphNode);
                graphicsItemNode->setNodeColour(colPair.first);
                revCompGraphNode->setNodeColour(colPair.second);
                colSet = true;
            }
        }
        if (!colSet)
            graphicsItemNode->setNodeColour(g_settings->nodeColorer->get(graphicsItemNode));
    }

    // Then make the GraphicsItemEdge objects and add them to the scene first,
    // so they are drawn underneath
    for (DeBruijnEdge *edge : graph.m_deBruijnGraphEdges) {
        if (edge->isDrawn()) {
            auto *graphicsItemEdge =
                    edge->getOverlapType() == EdgeOverlapType::EXTRA_LINK ?
                    new GraphicsItemLink(edge, graph, false) :
                    new GraphicsItemEdge(edge, graph, false);
            edge->setGraphicsItemEdge(graphicsItemEdge);
            graphicsItemEdge->setFlag(QGraphicsItem::ItemIsSelectable);
            addItem(graphicsItemEdge);
        }
        if (edge->isDrawnReverse()) {
            auto *graphicsItemEdge =
                    edge->getOverlapType() == EdgeOverlapType::EXTRA_LINK ?
                    new GraphicsItemLink(edge, graph, true) :
                    new GraphicsItemEdge(edge, graph, true);
            graph.setReverseGraphicsItem(edge, graphicsItemEdge);
            graphicsItemEdge->setFlag(QGraphicsItem::ItemIsSelectable);
            addItem(graphicsItemEdge);
        }
    }

    // Now add the GraphicsItemNode objects to the scene, so they are drawn
    // on top
    for (OrientedNode node : graph.oriented_nodes()) {
        if (GraphicsItemNode *item = node.getGraphicsItemNode())
            addItem(item);
    }
}

// `reverseComplement` removes both drawn directions. Otherwise only the
// direction that has `node` as an end.
void BandageGraphicsScene::removeAllGraphicsEdgesFromNode(OrientedNode node, bool reverseComplement) {
    std::unordered_set<GraphicsItemEdge *> graphicsItemEdgesToDelete;
    for (auto *edge : node.edges()) {
        for (bool reversed : {false, true}) {
            if (!reverseComplement) {
                auto [from, to] = edge->orientedEnds(reversed);
                if (node != from && node != to)
                    continue;
            }
            if (GraphicsItemEdge *item = g_assemblyGraph->takeGraphicsItem(edge, reversed))
                graphicsItemEdgesToDelete.insert(item);
        }
    }

    if (graphicsItemEdgesToDelete.empty())
        return;

    BandageGraphicsScene *scene = dynamic_cast<BandageGraphicsScene *>((*graphicsItemEdgesToDelete.begin())->scene());
    if (!scene)
        return;

    scene->removeGraphicsItemEdges(graphicsItemEdgesToDelete);
}

void BandageGraphicsScene::removeGraphicsItemEdges(const std::vector<DeBruijnEdge *> &edges,
                                                   bool reverseComplement) {
    std::unordered_set<GraphicsItemEdge *> graphicsItemEdgesToDelete;
    for (auto *edge : edges) {
        if (GraphicsItemEdge *item = g_assemblyGraph->takeGraphicsItem(edge, false))
            graphicsItemEdgesToDelete.insert(item);
        if (reverseComplement) {
            if (GraphicsItemEdge *item = g_assemblyGraph->takeGraphicsItem(edge, true))
                graphicsItemEdgesToDelete.insert(item);
        }
    }

    // Nothing to do
    if (graphicsItemEdgesToDelete.empty())
        return;

    BandageGraphicsScene *scene = dynamic_cast<BandageGraphicsScene *>((*graphicsItemEdgesToDelete.begin())->scene());
    if (!scene)
        return;

    scene->removeGraphicsItemEdges(graphicsItemEdgesToDelete);

}

void BandageGraphicsScene::removeGraphicsItemEdges(const std::unordered_set<GraphicsItemEdge *> &edges) {
    blockSignals(true);

    for (auto *graphicsItemEdge : edges) {
        if (graphicsItemEdge == nullptr)
            continue;

        removeItem(graphicsItemEdge);
        delete graphicsItemEdge;
    }
    blockSignals(false);
}

// If reverseComplement is true, this function will also remove the graphics items for reverse complements of the nodes.
void BandageGraphicsScene::removeGraphicsItemNodes(const std::vector<OrientedNode> &nodes,
                                                   bool reverseComplement) {
    std::unordered_set<GraphicsItemNode *> graphicsItemNodesToDelete;
    for (OrientedNode node : nodes) {
        removeAllGraphicsEdgesFromNode(node, reverseComplement);

        if (auto *graphicsItemNode = node.getGraphicsItemNode())
            graphicsItemNodesToDelete.insert(graphicsItemNode);
        node.setGraphicsItemNode(nullptr);

        if (reverseComplement && node && !node.isSelfComplement()) {
            OrientedNode rcNode = node.reverse();
            if (auto *graphicsItemNode = rcNode.getGraphicsItemNode())
                graphicsItemNodesToDelete.insert(graphicsItemNode);
            rcNode.setGraphicsItemNode(nullptr);
        }
    }

    // Nothing to do
    if (graphicsItemNodesToDelete.empty())
        return;

    BandageGraphicsScene *scene = dynamic_cast<BandageGraphicsScene *>((*graphicsItemNodesToDelete.begin())->scene());
    if (!scene)
        return;

    scene->removeGraphicsItemNodes(graphicsItemNodesToDelete);
}

void BandageGraphicsScene::removeGraphicsItemNodes(const std::unordered_set<GraphicsItemNode *> &nodes) {
    blockSignals(true);
    for (auto *graphicsItemNode : nodes) {
        if (graphicsItemNode == nullptr)
            continue;

        removeItem(graphicsItemNode);
        delete graphicsItemNode;
    }
    blockSignals(false);
}

void BandageGraphicsScene::duplicateGraphicsNode(OrientedNode originalNode, OrientedNode newNode,
                                                 AssemblyGraph &graph) {
    GraphicsItemNode *originalGraphicsItemNode = originalNode.getGraphicsItemNode();
    if (originalGraphicsItemNode == nullptr)
        return;

    auto *newGraphicsItemNode = new GraphicsItemNode(newNode, originalGraphicsItemNode);

    newNode.setGraphicsItemNode(newGraphicsItemNode);
    newGraphicsItemNode->setFlag(QGraphicsItem::ItemIsSelectable);
    newGraphicsItemNode->setFlag(QGraphicsItem::ItemIsMovable);

    originalGraphicsItemNode->shiftPointsLeft();
    newGraphicsItemNode->shiftPointsRight();
    originalGraphicsItemNode->fixEdgePaths();

    addItem(newGraphicsItemNode);

    for (auto *newEdge : newNode.edges())
        addIncidentEdgeGraphics(newEdge, graph);
}

void BandageGraphicsScene::addIncidentEdgeGraphics(DeBruijnEdge *edge, AssemblyGraph &graph,
                                                   bool requireDrawn) {
    auto makeItem = [&](bool reversed) {
        GraphicsItemEdge *item = edge->getOverlapType() == EdgeOverlapType::EXTRA_LINK
                                     ? static_cast<GraphicsItemEdge *>(new GraphicsItemLink(edge, graph, reversed))
                                     : new GraphicsItemEdge(edge, graph, reversed);
        item->setZValue(-1.0);
        item->setFlag(QGraphicsItem::ItemIsSelectable);
        addItem(item);
        return item;
    };

    auto directionReady = [](OrientedNode from, OrientedNode to) {
        return from && to &&
               from.getGraphicsItemNode() != nullptr &&
               to.getGraphicsItemNode() != nullptr;
    };
    auto strandHasItem = [](OrientedNode node) {
        if (!node)
            return false;
        if (node.getGraphicsItemNode() != nullptr)
            return true;
        OrientedNode rc = node.reverse();
        return rc.getGraphicsItemNode() != nullptr;
    };

    if (edge->getGraphicsItemEdge() == nullptr && (!requireDrawn || edge->isDrawn())) {
        auto [from, to] = edge->orientedEnds(false);
        bool ready = g_settings->doubleMode ? directionReady(from, to)
                                            : (strandHasItem(from) && strandHasItem(to));
        if (ready)
            edge->setGraphicsItemEdge(makeItem(false));
    }
    if (g_settings->doubleMode && !edge->isOwnReverseComplement() &&
        graph.reverseGraphicsItem(edge) == nullptr &&
        (!requireDrawn || edge->isDrawnReverse())) {
        auto [from, to] = edge->orientedEnds(true);
        if (directionReady(from, to))
            graph.setReverseGraphicsItem(edge, makeItem(true));
    }
}
