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


#include "graphicsitemnode.h"
#include "graphicsitemedge.h"
#include "debruijnnode.h"
#include "debruijnedge.h"
#include "assemblygraph.h"
#include "annotationsmanager.h"

#include "program/globals.h"
#include "program/memory.h"

#include "ui/mygraphicsscene.h"
#include "ui/mygraphicsview.h"

#include <QTransform>
#include <QPainterPathStroker>
#include <QPainter>
#include <QPen>
#include <QMessageBox>
#include <QFontMetrics>
#include <QSize>

#include <set>

#include <cmath>
#include <cstdlib>

//This constructor makes a new GraphicsItemNode by copying the line points of
//the given node.
GraphicsItemNode::GraphicsItemNode(DeBruijnNode * deBruijnNode,
                                   GraphicsItemNode * toCopy,
                                   QGraphicsItem * parent) :
    QGraphicsItem(parent), m_deBruijnNode(deBruijnNode),
    m_grabIndex(toCopy->m_grabIndex),
    m_hasArrow(toCopy->m_hasArrow),
    m_linePoints(toCopy->m_linePoints)
{
    setWidth();
    remakePath();
}

//This constructor makes a new GraphicsItemNode with a specific collection of
//line points.
GraphicsItemNode::GraphicsItemNode(DeBruijnNode * deBruijnNode,
                                   const std::vector<QPointF> &linePoints,
                                   QGraphicsItem * parent) :
    QGraphicsItem(parent), m_deBruijnNode(deBruijnNode),
    m_hasArrow(g_settings->doubleMode),
    m_grabIndex(0)
{
    m_linePoints.assign(linePoints.begin(), linePoints.end());
    setWidth();
    remakePath();
}

static double distance(QPointF p1, QPointF p2) {
    auto xDiff = p1.x() - p2.x();
    auto yDiff = p1.y() - p2.y();
    return sqrt(xDiff * xDiff + yDiff * yDiff);
}

// This function finds the centre point on the path defined by linePoints.
template<class Container>
static QPointF getCentre(const Container &linePoints) {
    if (linePoints.empty())
        return {};
    if (linePoints.size() == 1)
        return linePoints[0];

    double pathLength = 0.0;
    for (size_t i = 0; i < linePoints.size() - 1; ++i)
        pathLength += distance(linePoints[i], linePoints[i+1]);

    double endToCentre = pathLength / 2.0;

    double lengthSoFar = 0.0;
    for (size_t i = 0; i < linePoints.size() - 1; ++i)
    {
        QPointF a = linePoints[i];
        QPointF b = linePoints[i+1];
        double segmentLength = distance(a, b);

        //If this segment will push the distance over halfway, then it
        //contains the centre point.
        if (lengthSoFar + segmentLength >= endToCentre)
        {
            double additionalLengthNeeded = endToCentre - lengthSoFar;
            double fractionOfCurrentSegment = additionalLengthNeeded / segmentLength;
            return (b - a) * fractionOfCurrentSegment + a;
        }

        lengthSoFar += segmentLength;
    }

    //Code should never get here.
    return {};
}


void GraphicsItemNode::paint(QPainter * painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    static AnnotationGroup::AnnotationVector emptyAnnotations{};

    //This code lets me see the node's bounding box.
    //I use it for debugging graphics issues.
//    painter->setBrush(Qt::NoBrush);
//    painter->setPen(QPen(Qt::black, 1.0));
//    painter->drawRect(boundingRect());

    QPainterPath outlinePath = shape();

    //Fill the node's colour
    QBrush brush(m_colour);
    painter->fillPath(outlinePath, brush);

    //If the node has an arrow, then it's necessary to use the outline
    //as a clipping path so the colours don't extend past the edge of the
    //node.
    if (m_hasArrow)
        painter->setClipPath(outlinePath);

    for (const auto &annotationGroup : g_annotationsManager->getGroups()) {
        auto annotationSettings = g_settings->annotationsSettings[annotationGroup->id];

        const auto &annotations = annotationGroup->getAnnotations(m_deBruijnNode);
        const auto &revCompAnnotations = g_settings->doubleMode
                                         ? emptyAnnotations
                                         : annotationGroup->getAnnotations(m_deBruijnNode->getReverseComplement());

        for (const auto &annotation : annotations) {
            annotation->drawFigure(*painter, *this, false, annotationSettings.viewsToShow);
        }
        for (const auto &annotation : revCompAnnotations) {
            annotation->drawFigure(*painter, *this, true, annotationSettings.viewsToShow);
        }
    }
    painter->setClipping(false);

    //Draw the node outline
    QColor outlineColour = g_settings->outlineColour;
    double outlineThickness = g_settings->outlineThickness;
    if (isSelected())
    {
        outlineColour = g_settings->selectionColour;
        outlineThickness = g_settings->selectionThickness;
    }
    if (outlineThickness > 0.0)
    {
        outlinePath = outlinePath.simplified();
        QPen outlinePen(QBrush(outlineColour), outlineThickness, Qt::SolidLine,
                        Qt::SquareCap, Qt::RoundJoin);
        painter->setPen(outlinePen);
        painter->drawPath(outlinePath);
    }


    // raw the path highlighting outline, if appropriate
    if (g_memory->pathDialogIsVisible)
        exactPathHighlightNode(painter);

    // Draw the query path, if appropriate
    if (g_memory->queryPathDialogIsVisible)
        queryPathHighlightNode(painter);

    //Draw node labels if there are any to display.
    if (anyNodeDisplayText())
    {
        QStringList nodeText = getNodeText();
        QPainterPath textPath;

        QFontMetrics metrics(g_settings->labelFont);
        double fontHeight = metrics.ascent();

        for (int i = 0; i < nodeText.size(); ++i)
        {
            const QString& text = nodeText.at(i);
            int stepsUntilLast = nodeText.size() - 1 - i;
            double shiftLeft = -metrics.boundingRect(text).width() / 2.0;
            textPath.addText(shiftLeft, -stepsUntilLast * fontHeight, g_settings->labelFont, text);
        }

        std::vector<QPointF> centres;
        if (g_settings->positionTextNodeCentre)
            centres.push_back(getCentre(m_linePoints));
        else
            centres = getCentres();

        for (auto &centre : centres)
            drawTextPathAtLocation(painter, textPath, centre);
    }

    //Draw BLAST hit labels, if appropriate.
    for (const auto &annotationGroup : g_annotationsManager->getGroups()) {
        if (!g_settings->annotationsSettings[annotationGroup->id].showText)
            continue;

        const auto &annotations = annotationGroup->getAnnotations(m_deBruijnNode);
        const auto &revCompAnnotations = g_settings->doubleMode
                                         ? emptyAnnotations
                                         : annotationGroup->getAnnotations(m_deBruijnNode->getReverseComplement());

        for (const auto &annotation : annotations) {
            annotation->drawDescription(*painter, *this, false);
        }
        for (const auto &annotation : revCompAnnotations) {
            annotation->drawDescription(*painter, *this, true);
        }
    }
}


void GraphicsItemNode::drawTextPathAtLocation(QPainter * painter, const QPainterPath &textPath, QPointF centre)
{
    QRectF textBoundingRect = textPath.boundingRect();
    double textHeight = textBoundingRect.height();
    QPointF offset(0.0, textHeight / 2.0);

    double zoom = g_absoluteZoom;
    if (zoom == 0.0)
        zoom = 1.0;

    double zoomAdjustment = 1.0 / (1.0 + ((zoom - 1.0) * g_settings->textZoomScaleFactor));
    double inverseZoomAdjustment = 1.0 / zoomAdjustment;

    painter->translate(centre);
    painter->rotate(-g_graphicsView->getRotation());
    painter->scale(zoomAdjustment, zoomAdjustment);
    painter->translate(offset);

    if (g_settings->textOutline)
    {
        painter->setPen(QPen(g_settings->textOutlineColour,
                             g_settings->textOutlineThickness * 2.0,
                             Qt::SolidLine,
                             Qt::SquareCap,
                             Qt::RoundJoin));
        painter->drawPath(textPath);
    }

    painter->fillPath(textPath, QBrush(g_settings->textColour));
    painter->translate(-offset);
    painter->scale(inverseZoomAdjustment, inverseZoomAdjustment);
    painter->rotate(g_graphicsView->getRotation());
    painter->translate(-centre);
}

QPainterPath GraphicsItemNode::shape() const
{
    //If there is only one segment, and it is shorter than half its
    //width, then the arrow head will not be made with 45 degree
    //angles, but rather whatever angle is made by going from the
    //end to the back corners (the final node will be a triangle).
    if (m_hasArrow && m_linePoints.size() == 2 &&
        distance(getLast(), getSecondLast()) < m_width / 2.0)
    {
        QLineF backline = QLineF(getSecondLast(), getLast()).normalVector();
        backline.setLength(m_width / 2.0);
        QPointF backVector = backline.p2() - backline.p1();
        QPainterPath trianglePath;
        trianglePath.moveTo(getLast());
        trianglePath.lineTo(getSecondLast() + backVector);
        trianglePath.lineTo(getSecondLast() - backVector);
        trianglePath.lineTo(getLast());
        return trianglePath;
    }

    //Create a path that outlines the main node shape.
    QPainterPathStroker stroker;
    stroker.setWidth(m_width);
    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath mainNodePath = stroker.createStroke(m_path);

    if (!m_hasArrow)
        return mainNodePath;

    //If the node has an arrow head, subtract the part of its
    //final segment to give it a pointy end.
    //NOTE: THIS APPROACH CAN LEAD TO WEIRD EFFECTS WHEN THE NODE'S
    //POINTY END OVERLAPS WITH ANOTHER PART OF THE NODE.  PERHAPS THERE
    //IS A BETTER WAY TO MAKE ARROWHEADS?
    QLineF frontLine = QLineF(getLast(), getSecondLast()).normalVector();
    frontLine.setLength(m_width / 2.0);
    QPointF frontVector = frontLine.p2() - frontLine.p1();
    QLineF arrowheadLine(getLast(), getSecondLast());
    arrowheadLine.setLength(1.42 * (m_width / 2.0));
    arrowheadLine.setAngle(arrowheadLine.angle() + 45.0);
    QPointF arrow1 = arrowheadLine.p2();
    arrowheadLine.setAngle(arrowheadLine.angle() - 90.0);
    QPointF arrow2 = arrowheadLine.p2();
    QLineF lastSegmentLine(getSecondLast(), getLast());
    lastSegmentLine.setLength(0.01);
    QPointF additionalForwardBit = lastSegmentLine.p2() - lastSegmentLine.p1();
    QPainterPath subtractionPath;
    subtractionPath.moveTo(getLast());
    subtractionPath.lineTo(arrow1);
    subtractionPath.lineTo(getLast() + frontVector + additionalForwardBit);
    subtractionPath.lineTo(getLast() - frontVector + additionalForwardBit);
    subtractionPath.lineTo(arrow2);
    subtractionPath.lineTo(getLast());

    QPainterPath mainNodePathTmp = mainNodePath.subtracted(subtractionPath);

    QLineF backLine = QLineF(getFirst(), getSecond()).normalVector();
    backLine.setLength(m_width / 2.0);
    QPointF backVector = backLine.p2() - backLine.p1();
    QLineF arrowBackLine(getSecond(), getFirst());
    arrowBackLine.setLength(m_width / 2.0);
    QPointF arrowBackVector = arrowBackLine.p2() - arrowBackLine.p1();
    QPainterPath addedPath;
    addedPath.moveTo(getFirst());
    addedPath.lineTo(getFirst() + backVector + arrowBackVector);
    addedPath.lineTo(getFirst() + backVector);
    addedPath.lineTo(getFirst() - backVector);
    addedPath.lineTo(getFirst() - backVector + arrowBackVector);
    addedPath.lineTo(getFirst());
    mainNodePathTmp.addPath(addedPath);

    return mainNodePathTmp;
}


void GraphicsItemNode::mousePressEvent(QGraphicsSceneMouseEvent * event)
{
    m_grabIndex = 0;
    QPointF grabPoint = event->pos();

    double closestPointDistance = distance(grabPoint, m_linePoints[0]);
    for (size_t i = 1; i < m_linePoints.size(); ++i)
    {
        double pointDistance = distance(grabPoint, m_linePoints[i]);
        if (pointDistance < closestPointDistance)
        {
            closestPointDistance = pointDistance;
            m_grabIndex = i;
        }
    }
}


//When this node graphics item is moved, each of the connected edge
//graphics items will need to be adjusted accordingly.
void GraphicsItemNode::mouseMoveEvent(QGraphicsSceneMouseEvent * event)
{
    QPointF difference = event->pos() - event->lastPos();

    //If this node is selected, then move all of the other selected nodes too.
    //If it is not selected, then only move this node.
    std::vector<GraphicsItemNode *> nodesToMove;
    auto *graphicsScene = dynamic_cast<MyGraphicsScene *>(scene());
    if (isSelected())
        nodesToMove = graphicsScene->getSelectedGraphicsItemNodes();
    else
        nodesToMove.push_back(this);

    for (auto &node : nodesToMove)
    {
        node->shiftPoints(difference);
        node->remakePath();
    }
    graphicsScene->possiblyExpandSceneRectangle(&nodesToMove);

    fixEdgePaths(&nodesToMove);
}


//This function remakes edge paths.  If nodes is passed, it will remake the
//edge paths for all of the nodes.  If nodes isn't passed, then it will just
//do it for this node.
void GraphicsItemNode::fixEdgePaths(std::vector<GraphicsItemNode *> * nodes) const
{
    std::set<DeBruijnEdge *> edgesToFix;

    if (nodes == nullptr)
    {
        for (auto *edge : m_deBruijnNode->edges())
            edgesToFix.insert(edge);
    }
    else
    {
        for (auto &graphicNode : *nodes)
        {
            DeBruijnNode * node = graphicNode->m_deBruijnNode;
            for (auto *edge : node->edges())
                edgesToFix.insert(edge);
        }
    }

    for (auto *deBruijnEdge : edgesToFix)
    {
        GraphicsItemEdge * graphicsItemEdge = deBruijnEdge->getGraphicsItemEdge();

        //If this edge has a graphics item, adjust it now.
        if (graphicsItemEdge != nullptr)
            graphicsItemEdge->calculateAndSetPath();

        // If this edge does not have a graphics item, then perhaps its
        // reverse complement does.  Only do this check if the graph was drawn
        // on single mode.
        else if (!g_settings->doubleMode)
        {
            graphicsItemEdge = deBruijnEdge->getReverseComplement()->getGraphicsItemEdge();
            if (graphicsItemEdge != nullptr)
                graphicsItemEdge->calculateAndSetPath();
        }
    }
}


void GraphicsItemNode::shiftPoints(QPointF difference)
{
    prepareGeometryChange();

    if (g_settings->nodeDragging == NO_DRAGGING)
        return;

    else if (isSelected()) //Move all pieces for selected nodes
    {
        for (auto &m_linePoint : m_linePoints)
            m_linePoint += difference;
    }

    else if (g_settings->nodeDragging == ONE_PIECE)
        m_linePoints[m_grabIndex] += difference;

    else if (g_settings->nodeDragging == NEARBY_PIECES)
    {
        for (size_t i = 0; i < m_linePoints.size(); ++i)
        {
            int indexDistance = abs(int(i) - int(m_grabIndex));
            double dragStrength = pow(2.0, -1.0 * pow(double(indexDistance), 1.8) / g_settings->dragStrength); //constants chosen for dropoff of drag strength
            m_linePoints[i] += difference * dragStrength;
        }
    }
}

void GraphicsItemNode::remakePath()
{
    QPainterPath path;

    path.moveTo(m_linePoints[0]);
    for (size_t i = 1; i < m_linePoints.size(); ++i)
        path.lineTo(m_linePoints[i]);

    m_path = path;
}

static QPointF findIntermediatePoint(QPointF p1, QPointF p2, double p1Value, double p2Value, double targetValue) {
    QPointF difference = p2 - p1;
    double fraction = (targetValue - p1Value) / (p2Value - p1Value);
    return difference * fraction + p1;
}

QPainterPath GraphicsItemNode::makePartialPath(double startFraction, double endFraction)
{
    if (endFraction < startFraction)
        std::swap(startFraction, endFraction);

    double totalLength = getNodePathLength();

    QPainterPath path;
    bool pathStarted = false;
    double lengthSoFar = 0.0;
    for (size_t i = 0; i < m_linePoints.size() - 1; ++i)
    {
        QPointF point1 = m_linePoints[i];
        QPointF point2 = m_linePoints[i + 1];
        QLineF line(point1, point2);

        double point1Fraction = lengthSoFar / totalLength;
        lengthSoFar += line.length();
        double point2Fraction = lengthSoFar / totalLength;

        //If the path hasn't yet begun and this segment is before
        //the starting fraction, do nothing.
        if (!pathStarted && point2Fraction < startFraction)
            continue;

        //If the path hasn't yet begun but this segment covers the starting
        //fraction, start the path now.
        if (!pathStarted && point2Fraction >= startFraction)
        {
            pathStarted = true;
            path.moveTo(findIntermediatePoint(point1, point2, point1Fraction, point2Fraction, startFraction));
        }

        //If the path is in progress and this segment hasn't yet reached the end,
        //just continue the path.
        if (pathStarted && point2Fraction < endFraction)
            path.lineTo(point2);

        //If the path is in progress and this segment passes the end, finish the line.
        if (pathStarted && point2Fraction >= endFraction)
        {
            path.lineTo(findIntermediatePoint(point1, point2, point1Fraction, point2Fraction, endFraction));
            return path;
        }
    }

    return path;
}


double GraphicsItemNode::getNodePathLength()
{
    double totalLength = 0.0;
    for (size_t i = 0; i < m_linePoints.size() - 1; ++i)
    {
        QLineF line(m_linePoints[i], m_linePoints[i + 1]);
        totalLength += line.length();
    }
    return totalLength;
}

//This function will find the point that is a certain fraction of the way along the node's path.
QPointF GraphicsItemNode::findLocationOnPath(double fraction)
{
    double totalLength = getNodePathLength();

    double lengthSoFar = 0.0;
    for (size_t i = 0; i < m_linePoints.size() - 1; ++i)
    {
        QPointF point1 = m_linePoints[i];
        QPointF point2 = m_linePoints[i + 1];
        QLineF line(point1, point2);

        double point1Fraction = lengthSoFar / totalLength;
        lengthSoFar += line.length();
        double point2Fraction = lengthSoFar / totalLength;

        //If point2 hasn't yet reached the target, do nothing.
        if (point2Fraction < fraction)
            continue;

        //If the path hasn't yet begun but this segment covers the starting
        //fraction, start the path now.
        if (point2Fraction >= fraction)
            return findIntermediatePoint(point1, point2, point1Fraction, point2Fraction, fraction);
    }

    //The code shouldn't get here, as the target point should have been found in the above loop.
    return {};
}

bool GraphicsItemNode::usePositiveNodeColour() const
{
    return !m_hasArrow || m_deBruijnNode->isPositiveNode();
}


//This function returns the nodes' visible centres.  If the entire node is visible,
//then there is just one visible centre.  If none of the node is visible, then
//there are no visible centres.  If multiple parts of the node are visible, then there
//are multiple visible centres.
std::vector<QPointF> GraphicsItemNode::getCentres() const
{
    std::vector<QPointF> centres;
    std::vector<QPointF> currentRun;

    QPointF lastP;
    bool lastPointVisible = false;

    for (size_t i = 0; i < m_linePoints.size(); ++i)
    {
        QPointF p = m_linePoints[i];
        bool pVisible = g_graphicsView->isPointVisible(p);

        //If this point is visible, but the last wasn't, a new run is started.
        if (pVisible && !lastPointVisible)
        {
            //If this is not the first point, then we need to find the intermediate
            //point that lies on the visible boundary and start the path with that.
            if (i > 0)
                currentRun.push_back(g_graphicsView->findIntersectionWithViewportBoundary(QLineF(p, lastP)));
            currentRun.push_back(p);
        }

        //If th last point is visible and this one is too, add it to the current run.
        else if (pVisible && lastPointVisible)
            currentRun.push_back(p);

        //If the last point is visible and this one isn't, then a run has ended.
        else if (!pVisible && lastPointVisible)
        {
            //We need to find the intermediate point that is on the visible boundary.
            currentRun.push_back(g_graphicsView->findIntersectionWithViewportBoundary(QLineF(p, lastP)));

            centres.push_back(getCentre(currentRun));
            currentRun.clear();
        }

        //If neither this point nor the last were visible, we still need to check whether
        //the line segment between them is.  If so, then then this may be a case where
        //we are really zoomed in (and so line segments are large compared to the scene rect).
        else if (i > 0 && !pVisible && !lastPointVisible)
        {
            bool success;
            QLineF v = g_graphicsView->findVisiblePartOfLine(QLineF(lastP, p), &success);
            if (success)
            {
                QPointF vCentre = QPointF((v.p1().x() + v.p2().x()) / 2.0, (v.p1().y() + v.p2().y()) / 2.0);
                centres.push_back(vCentre);
            }
        }

        lastPointVisible = pVisible;
        lastP = p;
    }

    //If there is a current run, add its centre
    if (!currentRun.empty())
        centres.push_back(getCentre(currentRun));

    return centres;
}

QStringList GraphicsItemNode::getNodeText() const
{
    QStringList nodeText;

    if (g_settings->displayNodeCustomLabels)
        nodeText << g_assemblyGraph->getCustomLabelForDisplay(m_deBruijnNode);
    if (g_settings->displayNodeNames)
    {
        QString nodeName = m_deBruijnNode->getName();
        if (!g_settings->doubleMode)
            nodeName.chop(1);
        nodeText << nodeName;
    }
    if (g_settings->displayNodeLengths)
        nodeText << formatIntForDisplay(m_deBruijnNode->getLength()) + " bp";
    if (g_settings->displayNodeDepth)
        nodeText << formatDepthForDisplay(m_deBruijnNode->getDepth());
    if (g_settings->displayNodeCsvData) {
        auto data = g_assemblyGraph->getCsvLine(m_deBruijnNode, g_settings->displayNodeCsvDataCol);
        if (data)
            nodeText << *data;
    }
    return nodeText;
}


QSize GraphicsItemNode::getNodeTextSize(const QString& text)
{
    QFontMetrics fontMetrics(g_settings->labelFont);
    return fontMetrics.size(0, text);
}

void GraphicsItemNode::setWidth()
{
    m_width = getNodeWidth(m_deBruijnNode->getDepthRelativeToMeanDrawnDepth(), g_settings->depthPower,
                           g_settings->depthEffectOnWidth, g_settings->averageNodeWidth);
    if (m_width < 0.0)
        m_width = 0.0;
}



//The bounding rectangle of a node has to be a little bit bigger than
//the node's path, because of the outline.  The selection outline is
//the largest outline we can expect, so use that to define the bounding
//rectangle.
QRectF GraphicsItemNode::boundingRect() const
{
    double extraSize = g_settings->selectionThickness / 2.0;
    QRectF bound = shape().boundingRect();

    bound.setTop(bound.top() - extraSize);
    bound.setBottom(bound.bottom() + extraSize);
    bound.setLeft(bound.left() - extraSize);
    bound.setRight(bound.right() + extraSize);

    return bound;
}


double GraphicsItemNode::getNodeWidth(double depthRelativeToMeanDrawnDepth, double depthPower,
                                      double depthEffectOnWidth, double averageNodeWidth)
{
    if (depthRelativeToMeanDrawnDepth < 0.0)
        depthRelativeToMeanDrawnDepth = 0.0;
    double widthRelativeToAverage = (pow(depthRelativeToMeanDrawnDepth, depthPower) - 1.0) * depthEffectOnWidth + 1.0;
    return averageNodeWidth * widthRelativeToAverage;
}



//This function shifts all the node's points to the left (relative to its
//direction).  This is used in double mode to prevent nodes from displaying
//directly on top of their complement nodes.
void GraphicsItemNode::shiftPointsLeft()
{
    shiftPointSideways(true);
}

void GraphicsItemNode::shiftPointsRight()
{
    shiftPointSideways(false);
}

void GraphicsItemNode::shiftPointSideways(bool left)
{
    prepareGeometryChange();

    //The collection of line points should be at least
    //two large.  But just to be safe, quit now if it
    //is not.
    size_t linePointsSize = m_linePoints.size();
    if (linePointsSize < 2)
        return;

    //Shift by a quarter of the segment length.  This should make
    //nodes one half segment length separated from their complements.
    double shiftDistance = g_settings->doubleModeNodeSeparation;

    for (size_t i = 0; i < linePointsSize; ++i)
    {
        QPointF point = m_linePoints[i];
        QLineF nodeDirection;

        //If the point is on the end, then determine the node direction
        //using this point and its adjacent point.
        if (i == 0)
        {
            QPointF nextPoint = m_linePoints[i+1];
            nodeDirection = QLineF(point, nextPoint);
        }
        else if (i == linePointsSize - 1)
        {
            QPointF previousPoint = m_linePoints[i-1];
            nodeDirection = QLineF(previousPoint, point);
        }

        // If the point is in the middle, then determine the node direction
        //using both adjacent points.
        else
        {
            QPointF previousPoint = m_linePoints[i-1];
            QPointF nextPoint = m_linePoints[i+1];
            nodeDirection = QLineF(previousPoint, nextPoint);
        }

        QLineF shiftLine = nodeDirection.normalVector().unitVector();
        shiftLine.setLength(shiftDistance);

        QPointF shiftVector;
        if (left)
            shiftVector = shiftLine.p2() - shiftLine.p1();
        else
            shiftVector = shiftLine.p1() - shiftLine.p2();
        QPointF newPoint = point + shiftVector;
        m_linePoints[i] = newPoint;
    }

    remakePath();
}


double GraphicsItemNode::indexToFraction(int64_t pos) const {
    return static_cast<double>(pos) / m_deBruijnNode->getLength();
}


//This function outlines and shades the appropriate part of a node if it is
//in the user-specified path.
void GraphicsItemNode::exactPathHighlightNode(QPainter * painter)
{
    if (g_memory->userSpecifiedPath.containsNode(m_deBruijnNode))
        pathHighlightNode2(painter, m_deBruijnNode, false, &g_memory->userSpecifiedPath);

    if (!g_settings->doubleMode &&
            g_memory->userSpecifiedPath.containsNode(m_deBruijnNode->getReverseComplement()))
        pathHighlightNode2(painter, m_deBruijnNode->getReverseComplement(), true, &g_memory->userSpecifiedPath);
}


//This function outlines and shades the appropriate part of a node if it is
//in the user-specified path.
void GraphicsItemNode::queryPathHighlightNode(QPainter * painter)
{
    if (g_memory->queryPaths.empty())
        return;

    for (auto &queryPath : g_memory->queryPaths)
    {
        Path * path = &queryPath;
        if (path->containsNode(m_deBruijnNode))
            pathHighlightNode2(painter, m_deBruijnNode, false, path);

        if (!g_settings->doubleMode &&
                path->containsNode(m_deBruijnNode->getReverseComplement()))
            pathHighlightNode2(painter, m_deBruijnNode->getReverseComplement(), true, path);
    }
}


void GraphicsItemNode::pathHighlightNode2(QPainter * painter,
                                          DeBruijnNode * node,
                                          bool reverse,
                                          Path * path)
{
    int numberOfTimesInMiddle = path->numberOfOccurrencesInMiddleOfPath(node);
    for (int i = 0; i < numberOfTimesInMiddle; ++i)
        pathHighlightNode3(painter, shape());

    bool isStartingNode = path->isStartingNode(node);
    bool isEndingNode = path->isEndingNode(node);

    //If this is the only node in the path, then we limit the highlighting to the appropriate region.
    if (isStartingNode && isEndingNode && path->getNodeCount() == 1)
    {
        pathHighlightNode3(painter, buildPartialHighlightPath(path->getStartFraction(), path->getEndFraction(), reverse));
        return;
    }

    if (isStartingNode)
        pathHighlightNode3(painter, buildPartialHighlightPath(path->getStartFraction(), 1.0, reverse));

    if (isEndingNode)
        pathHighlightNode3(painter, buildPartialHighlightPath(0.0, path->getEndFraction(), reverse));
}


void GraphicsItemNode::pathHighlightNode3(QPainter * painter,
                                          QPainterPath highlightPath)
{
    QBrush shadingBrush(g_settings->pathHighlightShadingColour);
    painter->fillPath(highlightPath, shadingBrush);

    highlightPath = highlightPath.simplified();
    QPen outlinePen(QBrush(g_settings->pathHighlightOutlineColour),
                    g_settings->selectionThickness, Qt::SolidLine,
                    Qt::SquareCap, Qt::RoundJoin);
    painter->setPen(outlinePen);
    painter->drawPath(highlightPath);
}


QPainterPath GraphicsItemNode::buildPartialHighlightPath(double startFraction,
                                                         double endFraction,
                                                         bool reverse)
{
    if (reverse)
    {
        startFraction = 1.0 - startFraction;
        endFraction = 1.0 - endFraction;
        std::swap(startFraction, endFraction);
    }

    QPainterPath partialPath = makePartialPath(startFraction,
                                               endFraction);

    QPainterPathStroker stroker;

    //If the node has an arrow, we need a path intersection with the
    //shape to make sure the arrowhead is part of the path.  Adding a bit
    //to the width seems to help with the intersection.
    if (m_hasArrow)
        stroker.setWidth(m_width + 0.1);
    else
        stroker.setWidth(m_width);

    stroker.setCapStyle(Qt::FlatCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath highlightPath = stroker.createStroke(partialPath);

    if (m_hasArrow)
        highlightPath = highlightPath.intersected(shape());

    return highlightPath;
}


bool GraphicsItemNode::anyNodeDisplayText()
{
    return g_settings->displayNodeCustomLabels ||
            g_settings->displayNodeNames ||
            g_settings->displayNodeLengths ||
            g_settings->displayNodeDepth ||
            g_settings->displayNodeCsvData;
}
