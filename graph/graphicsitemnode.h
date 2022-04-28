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


#ifndef GRAPHICSITEMNODE_H
#define GRAPHICSITEMNODE_H

#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include "ogdf/basic/GraphAttributes.h"
#include <vector>
#include <QPointF>
#include <QColor>
#include <QFont>
#include <QString>
#include <QPainterPath>
#include <QStringList>

class DeBruijnNode;
class Path;

class GraphicsItemNode : public QGraphicsItem
{
public:
    GraphicsItemNode(DeBruijnNode * deBruijnNode,
                     ogdf::GraphAttributes * graphAttributes,
                     QGraphicsItem * parent = nullptr);
    GraphicsItemNode(DeBruijnNode * deBruijnNode,
                     GraphicsItemNode * toCopy,
                     QGraphicsItem * parent = nullptr);
    GraphicsItemNode(DeBruijnNode * deBruijnNode,
                     std::vector<QPointF> linePoints,
                     QGraphicsItem * parent = nullptr);

    DeBruijnNode * m_deBruijnNode;
    double m_width;
    bool m_hasArrow;
    std::vector<QPointF> m_linePoints;
    size_t m_grabIndex;
    QColor m_colour;
    QPainterPath m_path;

    static double distance(QPointF p1, QPointF p2);
    static QSize getNodeTextSize(const QString& text);
    static QPointF findIntermediatePoint(QPointF p1, QPointF p2, double p1Value,
                                         double p2Value, double targetValue);
    static double getNodeWidth(double depthRelativeToMeanDrawnDepth,
                               double depthPower,
                               double depthEffectOnWidth,
                               double averageNodeWidth);
    static void drawTextPathAtLocation(QPainter *painter, const QPainterPath& textPath, QPointF centre);

    void mousePressEvent(QGraphicsSceneMouseEvent * event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent * event) override;
    void paint(QPainter * painter, const QStyleOptionGraphicsItem *, QWidget *) override;
    QPainterPath shape() const override;
    void shiftPoints(QPointF difference);
    void remakePath();
    bool usePositiveNodeColour() const;
    QPointF getFirst() const {return m_linePoints[0];}
    QPointF getSecond() const {return m_linePoints[1];}
    QPointF getLast() const {return m_linePoints[m_linePoints.size()-1];}
    QPointF getSecondLast() const {return m_linePoints[m_linePoints.size()-2];}
    std::vector<QPointF> getCentres() const;
    QPointF getCentre(std::vector<QPointF> linePoints) const;
    void setNodeColour();
    QStringList getNodeText() const;
    QColor getDepthColour() const;
    void setWidth();
    QPainterPath makePartialPath(double startFraction, double endFraction);
    double getNodePathLength();
    QPointF findLocationOnPath(double fraction);
    QRectF boundingRect() const override;
    void shiftPointsLeft();
    void shiftPointsRight();
    void fixEdgePaths(std::vector<GraphicsItemNode *> * nodes = 0) const;
    double indexToFraction(int64_t pos) const;

private:
    static void pathHighlightNode3(QPainter * painter, QPainterPath highlightPath);
    static bool anyNodeDisplayText();

    void exactPathHighlightNode(QPainter * painter);
    void queryPathHighlightNode(QPainter * painter);
    void pathHighlightNode2(QPainter * painter, DeBruijnNode * node, bool reverse, Path * path);
    QPainterPath buildPartialHighlightPath(double startFraction, double endFraction, bool reverse);
    void shiftPointSideways(bool left);
};

#endif // GRAPHICSITEMNODE_H
