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

#pragma once

#include "layout/graphlayout.h"

#include <QGraphicsScene>
#include <vector>
#include <unordered_set>

class DeBruijnNode;
class DeBruijnEdge;
class GraphicsItemNode;
class GraphicsItemEdge;
class AssemblyGraph;

class BandageGraphicsScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit BandageGraphicsScene(QObject *parent = nullptr);
    size_t addGraphicsItemsToScene(AssemblyGraph &graph,
                                   const GraphLayout &layout);
    size_t addGraphicsItemsToScene(std::vector<DeBruijnEdge*> newEdges);

    std::vector<DeBruijnNode *> getSelectedNodes();
    std::vector<DeBruijnNode *> getSelectedPositiveNodes();
    std::vector<GraphicsItemNode *> getSelectedGraphicsItemNodes();
    std::vector<DeBruijnEdge *> getSelectedEdges();
    DeBruijnNode * getOneSelectedNode();
    DeBruijnEdge * getOneSelectedEdge();
    DeBruijnNode * getOnePositiveSelectedNode();
    double getTopZValue();
    void setSceneRectangle();
    void possiblyExpandSceneRectangle(std::vector<GraphicsItemNode *> * movedNodes);

    static void removeGraphicsItemEdges(const std::vector<DeBruijnEdge *> &edges,
                                        bool reverseComplement);
    static void removeGraphicsItemNodes(const std::vector<DeBruijnNode *> &nodes,
                                        bool reverseComplement);
    static void removeAllGraphicsEdgesFromNode(DeBruijnNode *node,
                                               bool reverseComplement);

    void duplicateGraphicsNode(DeBruijnNode * originalNode, DeBruijnNode * newNode,
                               const AssemblyGraph &graph);

private:
    void removeGraphicsItemNodes(const std::unordered_set<GraphicsItemNode*> &nodes);
    void removeGraphicsItemEdges(const std::unordered_set<GraphicsItemEdge*> &edges);
};
