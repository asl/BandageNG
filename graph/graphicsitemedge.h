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


#ifndef GRAPHICSITEMEDGE_H
#define GRAPHICSITEMEDGE_H

#include "debruijnedge.h"
#include "program/llvm.h"

#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPointF>

#include <utility>

class DeBruijnNode;
class DeBruijnEdge;
class AssemblyGraph;

class GraphicsItemEdge : public QGraphicsPathItem {
public:
    explicit GraphicsItemEdge(DeBruijnEdge *deBruijnEdge, const AssemblyGraph &graph,
                              bool reversed = false, QGraphicsItem *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override;
    QPainterPath shape() const override;

    virtual void remakePath();
    DeBruijnEdge *edge() const { return m_edge.getPointer(); }
    bool reversed() const { return m_edge.getInt(); }

protected:
    std::pair<OrientedNode, OrientedNode> orientedNodes() const;

private:
    // The direction bit is stored in the pointer. DeBruijnEdge is at least
    // pointer-aligned, so the graphics item stays one word smaller.
    bandage::PointerIntPair<DeBruijnEdge *, 1, bool> m_edge;
    QColor m_edgeColor;
    Qt::PenStyle m_penStyle;
    float m_width;
};

static_assert(sizeof(GraphicsItemEdge) == 48,
              "GraphicsItemEdge must stay in the 48-byte size class");

#endif // GRAPHICSITEMEDGE_H
