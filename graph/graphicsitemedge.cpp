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


#include "graphicsitemedge.h"
#include "graphicsitemnode.h"
#include "debruijnedge.h"
#include "debruijnnode.h"

#include "graph/assemblygraph.h"
#include "program/globals.h"
#include "program/settings.h"

#include <QPainterPathStroker>
#include <QPainter>
#include <QPen>
#include <QLineF>

GraphicsItemEdge::GraphicsItemEdge(DeBruijnEdge *deBruijnEdge,
                                   const AssemblyGraph &graph,
                                   QGraphicsItem *parent)
    : QGraphicsPathItem(parent), m_deBruijnEdge(deBruijnEdge) {
    m_edgeColor = graph.getCustomColour(deBruijnEdge);
    auto style = graph.getCustomStyle(deBruijnEdge);
    m_penStyle = style.lineStyle;
    if (style.width > 0)
        m_width = style.width;
    else
        m_width = deBruijnEdge->getOverlapType() == EdgeOverlapType::EXTRA_LINK ?
                  g_settings->linkWidth : g_settings->edgeWidth;

    remakePath();
}

void GraphicsItemEdge::paint(QPainter * painter, const QStyleOptionGraphicsItem *, QWidget *) {
    QColor penColour = isSelected() ? g_settings->selectionColour : m_edgeColor;
    QPen edgePen(QBrush(penColour), m_width, m_penStyle, Qt::RoundCap);
    painter->setPen(edgePen);
    painter->drawPath(path());
}

QPainterPath GraphicsItemEdge::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(m_width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path());
}

static void getControlPointLocations(const DeBruijnEdge *edge,
                                     QLineF &start, QLineF &end) {
    DeBruijnNode *startingNode = edge->getStartingNode();
    DeBruijnNode *endingNode = edge->getEndingNode();

    if (const auto *startingGraphicsItemNode = startingNode->getGraphicsItemNode()) {
        start = startingGraphicsItemNode->getLastSegment();
    } else if (const auto *startingGraphicsItemRcNode =
               startingNode->getReverseComplement()->getGraphicsItemNode()) {
        auto segment = startingGraphicsItemRcNode->getFirstSegment();
        start.setPoints(segment.p2(), segment.p1());
    }

    if (const auto *endingGraphicsItemNode = endingNode->getGraphicsItemNode()) {
        end = endingGraphicsItemNode->getFirstSegment();
    } else if (const auto *endingGraphicsItemRcNode
               = endingNode->getReverseComplement()->getGraphicsItemNode()) {
        auto segment = endingGraphicsItemRcNode->getLastSegment();
        end.setPoints(segment.p2(), segment.p1());
    }
}

static QPointF extendLine(QLineF line, double extensionLength) {
    double extensionRatio = extensionLength / line.length();
    QPointF dP = line.p2() - line.p1();
    return line.p2() + dP * extensionRatio;
}

// This function handles the special case of an edge that connects a node
// to itself where the node graphics item has only one line segment.
static void
makeSpecialPathConnectingNodeToSelf(QPainterPath &path, const QLineF &nodeLine) {
    QPointF startLocation = nodeLine.p2(), endLocation = nodeLine.p1();
    double extensionLength = g_settings->edgeLength;
    QPointF controlPoint1 = extendLine(nodeLine, extensionLength),
            controlPoint2 = extendLine(QLineF(nodeLine.p2(), nodeLine.p1()), extensionLength);

    QLineF normalUnitLine = nodeLine.normalVector().unitVector();
    QPointF perpendicularShift = (normalUnitLine.p2() - normalUnitLine.p1()) * g_settings->edgeLength;
    QPointF nodeMidPoint = (startLocation + endLocation) / 2.0;

    path.moveTo(startLocation);

    path.cubicTo(controlPoint1, controlPoint1 + perpendicularShift, nodeMidPoint + perpendicularShift);
    path.cubicTo(controlPoint2 + perpendicularShift, controlPoint2, endLocation);
}

// This function handles the special case of an edge that connects a node to its
// reverse complement and is displayed in single mode.
static void
makeSpecialPathConnectingNodeToReverseComplement(QPainterPath &path, const QLineF &nodeLine) {
    QPointF startLocation = nodeLine.p2(), endLocation = nodeLine.p2();
    double extensionLength = g_settings->edgeLength / 2.0;
    QPointF controlPoint1 = extendLine(nodeLine, extensionLength),
            controlPoint2 = extendLine(nodeLine, extensionLength);

    QPointF startToControl = controlPoint1 - startLocation;
    QPointF pathMidPoint = startLocation + startToControl * 3.0;

    QLineF normalLine = QLineF(controlPoint1, startLocation).normalVector();
    QPointF perpendicularShift = (normalLine.p2() - normalLine.p1()) * 1.5;

    path.moveTo(startLocation);

    path.cubicTo(controlPoint1, pathMidPoint + perpendicularShift, pathMidPoint);
    path.cubicTo(pathMidPoint - perpendicularShift, controlPoint2, endLocation);
}

// Ordinary path is just a single cubic Bezier curve.
static void makeOrdinaryPath(QPainterPath &path,
                             const QLineF &startSegment, const QLineF &endSegment) {
    QPointF startLocation = startSegment.p2(), endLocation = endSegment.p1();
    double edgeDistance = QLineF(startLocation, endLocation).length();

    double extensionLength = g_settings->edgeLength;
    if (extensionLength > edgeDistance / 2.0)
        extensionLength = edgeDistance / 2.0;

    QPointF controlPoint1 = extendLine(startSegment, extensionLength),
            controlPoint2 = extendLine(QLineF(endSegment.p2(), endSegment.p1()), extensionLength);

    path.moveTo(startLocation);
    path.cubicTo(controlPoint1, controlPoint2, endLocation);
}


void GraphicsItemEdge::remakePath() {
    QLineF startSegment, endSegment;
    getControlPointLocations(m_deBruijnEdge, startSegment, endSegment);

    QPainterPath path;

    // If this edge is connecting a node to itself, and that node
    // is made of only one line segment, then a special path is
    // required, otherwise the edge will be mostly hidden underneath
    // the node.
    DeBruijnNode *startingNode = m_deBruijnEdge->getStartingNode();
    DeBruijnNode *endingNode = m_deBruijnEdge->getEndingNode();
    if (startingNode == endingNode) {
        GraphicsItemNode * graphicsItemNode = startingNode->getGraphicsItemNode();
        if (!graphicsItemNode)
            graphicsItemNode = startingNode->getReverseComplement()->getGraphicsItemNode();
        if (graphicsItemNode && graphicsItemNode->m_linePoints.size() == 2)
            makeSpecialPathConnectingNodeToSelf(path, startSegment);
        else
            makeOrdinaryPath(path, startSegment, endSegment);
    } else if (startingNode == endingNode->getReverseComplement() &&
               !g_settings->doubleMode) {
        // If we are in single mode and the edge connects a node to its reverse
        // complement, then we need a special path to make it visible.
        makeSpecialPathConnectingNodeToReverseComplement(path, startSegment);
    } else
        makeOrdinaryPath(path, startSegment, endSegment);

    setPath(path);
}
