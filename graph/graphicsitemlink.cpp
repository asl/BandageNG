// Copyright 2024 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage-NG is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage-NG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage-NG.  If not, see <http://www.gnu.org/licenses/>.

#include "graphicsitemlink.h"
#include "graphicsitemedge.h"
#include "graphicsitemnode.h"
#include "debruijnedge.h"
#include "debruijnnode.h"
#include "program/globals.h"
#include "program/settings.h"

GraphicsItemLink::GraphicsItemLink(DeBruijnEdge *deBruijnEdge, const AssemblyGraph &graph,
                                   QGraphicsItem *parent)
        : GraphicsItemEdge(deBruijnEdge, graph, parent) {
    remakePath();
}

static QPointF extendLineOrthogonal(QPointF start, QPointF normal, double extensionLength) {
    return start + normal * extensionLength;
}

static QPointF extendLine(QLineF line, double extensionLength) {
    double extensionRatio = extensionLength / line.length();
    QPointF dP = line.p2() - line.p1();
    return line.p2() + dP * extensionRatio;
}

// Ordinary path is just a single cubic Bezier curve from middle of one segment
// into middle of another segment. The control points are selected orthogonal to
// the segments
static void makeOrdinaryPath(QPainterPath &path,
                             QLineF start, QLineF end) {
    QPointF startLocation = start.center();
    QPointF endLocation = end.center();

    double edgeDistance = QLineF(startLocation, endLocation).length();
    double extensionLength = g_settings->edgeLength;
    if (extensionLength > edgeDistance / 2.0)
        extensionLength = edgeDistance / 2.0;

    // Find normals of start and end that look into each other
    QLineF startNormalLine = start.normalVector().unitVector(),
           endNormalLine   = end.normalVector().unitVector();
    QPointF startNormal(startNormalLine.dx(), startNormalLine.dy()),
            endNormal(endNormalLine.dx(), endNormalLine.dy());
    QPointF dP = endLocation - startLocation;

    // Flip normals, if necessary
    if (QPointF::dotProduct(dP, startNormal) < 0)
        startNormal = -startNormal;
    if (QPointF::dotProduct(dP, endNormal) > 0)
        endNormal = -endNormal;

    QPointF controlPoint1 = extendLineOrthogonal(startLocation, startNormal, extensionLength),
            controlPoint2 = extendLineOrthogonal(endLocation, endNormal, extensionLength);

    path.moveTo(startLocation);
    path.cubicTo(controlPoint1, controlPoint2, endLocation);
}

static void makePathConnectingNodeToSelf(QPainterPath &path,
                                         QLineF start) {
    double edgeDistance = start.length();
    double extensionLength = g_settings->edgeLength;
    if (extensionLength > edgeDistance / 2.0)
        extensionLength = edgeDistance / 2.0;

    QPointF startLocation = start.center();
    QPointF endLocation = extendLine(start, extensionLength);

    QLineF startNormalLine = start.normalVector().unitVector();
    QPointF startNormal(startNormalLine.dx(), startNormalLine.dy());

    path.moveTo(startLocation);
    path.cubicTo(extendLineOrthogonal(startLocation, startNormal, extensionLength),
                 extendLineOrthogonal(endLocation, startNormal, extensionLength),
                 endLocation);
    path.moveTo(endLocation);
    path.cubicTo(extendLineOrthogonal(endLocation, -startNormal, extensionLength),
                 extendLineOrthogonal(startLocation, -startNormal, extensionLength),
                 startLocation);
}

void GraphicsItemLink::remakePath() {
    DeBruijnNode *startingNode = edge()->getStartingNode();
    DeBruijnNode *endingNode = edge()->getEndingNode();

    // Link goes from the median of one node into median of other node.
    QLineF startMidSegment, endMidSegment;

    if (const auto *startingGraphicsItemNode = startingNode->getGraphicsItemNode()) {
        startMidSegment = startingGraphicsItemNode->getMedianSegment();
    } else if (const auto *startingGraphicsItemRcNode =
               startingNode->getReverseComplement()->getGraphicsItemNode()) {
        startMidSegment = startingGraphicsItemRcNode->getMedianSegment();
    }

    if (const auto *endingGraphicsItemNode = endingNode->getGraphicsItemNode()) {
        endMidSegment = endingGraphicsItemNode->getMedianSegment();
    } else if (const auto *endingGraphicsItemRcNode
               = endingNode->getReverseComplement()->getGraphicsItemNode()) {
        endMidSegment = endingGraphicsItemRcNode->getMedianSegment();
    }

    QPainterPath path;

    // If this edge is connecting a node to itself (or a reverse-complement in
    // single mode), then a special path is required
    if (startingNode == endingNode ||
        startingNode == endingNode->getReverseComplement()) {
        makePathConnectingNodeToSelf(path, startMidSegment);
    } else {
        makeOrdinaryPath(path, startMidSegment, endMidSegment);
    }

    setPath(path);
}
