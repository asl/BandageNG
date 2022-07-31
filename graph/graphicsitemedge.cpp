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

GraphicsItemEdge::GraphicsItemEdge(DeBruijnEdge * deBruijnEdge, QGraphicsItem * parent)
    : QGraphicsPathItem(parent), m_deBruijnEdge(deBruijnEdge) {
    m_edgeColor = g_assemblyGraph->getCustomColour(deBruijnEdge);
    m_penStyle = g_assemblyGraph->getCustomStyle(deBruijnEdge);

    remakePath();
}

void GraphicsItemEdge::paint(QPainter * painter, const QStyleOptionGraphicsItem *, QWidget *) {
    double edgeWidth = g_settings->edgeWidth;
    QColor penColour = isSelected() ? g_settings->selectionColour : m_edgeColor;
    QPen edgePen(QBrush(penColour), edgeWidth, m_penStyle, Qt::RoundCap);
    painter->setPen(edgePen);
    painter->drawPath(path());
}

QPainterPath GraphicsItemEdge::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(g_settings->edgeWidth);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path());
}

static void getControlPointLocations(const DeBruijnEdge *edge,
                                     QPointF &startLocation, QPointF &beforeStartLocation,
                                     QPointF &endLocation, QPointF &afterEndLocation) {
    DeBruijnNode * startingNode = edge->getStartingNode();
    DeBruijnNode * endingNode = edge->getEndingNode();

    if (startingNode->hasGraphicsItem()) {
        startLocation = startingNode->getGraphicsItemNode()->getLast();
        beforeStartLocation = startingNode->getGraphicsItemNode()->getSecondLast();
    } else if (startingNode->getReverseComplement()->hasGraphicsItem()) {
        startLocation = startingNode->getReverseComplement()->getGraphicsItemNode()->getFirst();
        beforeStartLocation = startingNode->getReverseComplement()->getGraphicsItemNode()->getSecond();
    }

    if (endingNode->hasGraphicsItem()) {
        endLocation = endingNode->getGraphicsItemNode()->getFirst();
        afterEndLocation = endingNode->getGraphicsItemNode()->getSecond();
    } else if (endingNode->getReverseComplement()->hasGraphicsItem()) {
        endLocation = endingNode->getReverseComplement()->getGraphicsItemNode()->getLast();
        afterEndLocation = endingNode->getReverseComplement()->getGraphicsItemNode()->getSecondLast();
    }
}

static QPointF extendLine(QPointF start, QPointF end, double extensionLength) {
    double extensionRatio = extensionLength / QLineF(start, end).length();
    QPointF difference = end - start;
    difference *= extensionRatio;
    return end + difference;
}

// This function handles the special case of an edge that connects a node
// to itself where the node graphics item has only one line segment.
static void makeSpecialPathConnectingNodeToSelf(QPainterPath &path,
                                                const QPointF &startLocation, const QPointF &beforeStartLocation,
                                                const QPointF &endLocation, const QPointF &afterEndLocation) {
    double extensionLength = g_settings->edgeLength;
    QPointF controlPoint1 = extendLine(beforeStartLocation, startLocation, extensionLength),
            controlPoint2 = extendLine(afterEndLocation, endLocation, extensionLength);

    QLineF nodeLine(startLocation, endLocation);
    QLineF normalUnitLine = nodeLine.normalVector().unitVector();
    QPointF perpendicularShift = (normalUnitLine.p2() - normalUnitLine.p1()) * g_settings->edgeLength;
    QPointF nodeMidPoint = (startLocation + endLocation) / 2.0;

    path.moveTo(startLocation);

    path.cubicTo(controlPoint1, controlPoint1 + perpendicularShift, nodeMidPoint + perpendicularShift);
    path.cubicTo(controlPoint2 + perpendicularShift, controlPoint2, endLocation);
}

// This function handles the special case of an edge that connects a node to its
// reverse complement and is displayed in single mode.
static void makeSpecialPathConnectingNodeToReverseComplement(QPainterPath &path,
                                                             const QPointF &startLocation, const QPointF &beforeStartLocation,
                                                             const QPointF &endLocation, const QPointF &afterEndLocation) {
    double extensionLength = g_settings->edgeLength / 2.0;
    QPointF controlPoint1 = extendLine(beforeStartLocation, startLocation, extensionLength),
            controlPoint2 = extendLine(afterEndLocation, endLocation, extensionLength);

    QPointF startToControl = controlPoint1 - startLocation;
    QPointF pathMidPoint = startLocation + startToControl * 3.0;

    QLineF normalLine = QLineF(controlPoint1, startLocation).normalVector();
    QPointF perpendicularShift = (normalLine.p2() - normalLine.p1()) * 1.5;

    path.moveTo(startLocation);

    path.cubicTo(controlPoint1, pathMidPoint + perpendicularShift, pathMidPoint);
    path.cubicTo(pathMidPoint - perpendicularShift, controlPoint2, endLocation);
}

void GraphicsItemEdge::remakePath() {
    QPointF startLocation, beforeStartLocation, endLocation, afterEndLocation;
    getControlPointLocations(m_deBruijnEdge,
                             startLocation, beforeStartLocation,
                             endLocation, afterEndLocation);

    double edgeDistance = QLineF(startLocation, endLocation).length();

    double extensionLength = g_settings->edgeLength;
    if (extensionLength > edgeDistance / 2.0)
        extensionLength = edgeDistance / 2.0;

    QPointF controlPoint1 = extendLine(beforeStartLocation, startLocation, extensionLength),
            controlPoint2 = extendLine(afterEndLocation, endLocation, extensionLength);

    QPainterPath path;

    // If this edge is connecting a node to itself, and that node
    // is made of only one line segment, then a special path is
    // required, otherwise the edge will be mostly hidden underneath
    // the node.
    DeBruijnNode * startingNode = m_deBruijnEdge->getStartingNode();
    DeBruijnNode * endingNode = m_deBruijnEdge->getEndingNode();
    if (startingNode == endingNode) {
        GraphicsItemNode * graphicsItemNode = startingNode->getGraphicsItemNode();
        if (!graphicsItemNode)
            graphicsItemNode = startingNode->getReverseComplement()->getGraphicsItemNode();
        if (graphicsItemNode && graphicsItemNode->m_linePoints.size() == 2)
            makeSpecialPathConnectingNodeToSelf(path,
                                                startLocation, beforeStartLocation,
                                                endLocation, afterEndLocation);
    } else if (startingNode == endingNode->getReverseComplement() &&
               !g_settings->doubleMode) {
        //If we are in single mode and the edge connects a node to its reverse
        //complement, then we need a special path to make it visible.
        makeSpecialPathConnectingNodeToReverseComplement(path,
                                                         startLocation, beforeStartLocation,
                                                         endLocation, afterEndLocation);
    } else {
        // Otherwise, the path is just a single cubic Bezier curve.
        path.moveTo(startLocation);
        path.cubicTo(controlPoint1, controlPoint2, endLocation);
    }

    setPath(path);
}