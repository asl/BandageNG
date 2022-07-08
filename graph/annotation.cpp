// Copyright 2022 Anton Korobeynikov
// Copyright 2022 Andrei Zakharov

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
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#include "annotation.h"
#include "graphicsitemnode.h"
#include "program/settings.h"

#include <QPen>
#include <QPainter>

void SolidView::drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
                           int64_t end) const {
    QPen pen;
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::BevelJoin);

    pen.setWidthF(m_widthMultiplier * graphicsItemNode.m_width);

    pen.setColor(m_color);
    painter.setPen(pen);

    double fractionStart = graphicsItemNode.indexToFraction(start);
    double fractionEnd = graphicsItemNode.indexToFraction(end + 1);

    if (reverseComplement) {
        fractionStart = 1 - fractionStart;
        fractionEnd = 1 - fractionEnd;
    }
    painter.drawPath(graphicsItemNode.makePartialPath(fractionStart, fractionEnd));
}

void RainbowBlastHitView::drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement,
                                     int64_t start, int64_t end) const {

    double scaledNodeLength = graphicsItemNode.getNodePathLength() * g_absoluteZoom;
    double fractionStart = graphicsItemNode.indexToFraction(start);
    double fractionEnd = graphicsItemNode.indexToFraction(end + 1);
    double scaledHitLength = (fractionEnd - fractionStart) * scaledNodeLength;
    int partCount = ceil(
            g_settings->blastRainbowPartsPerQuery * fabs(m_rainbowFractionStart - m_rainbowFractionEnd));

    //If there are way more parts than the scaled hit length, that means
    //that a single part will be much less than a pixel in length.  This
    //isn't desirable, so reduce the partCount in these cases.
    if (partCount > scaledHitLength * 2.0)
        partCount = int(scaledHitLength * 2.0);

    double nodeSpacing = (fractionEnd - fractionStart) / partCount;
    double rainbowSpacing = (m_rainbowFractionEnd - m_rainbowFractionStart) / partCount;

    double nodeFraction = fractionStart;
    double rainbowFraction = m_rainbowFractionStart;

    QPen pen;
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::BevelJoin);

    pen.setWidthF(graphicsItemNode.m_width);

    for (int i = 0; i < partCount; ++i) {
        QColor dotColour;
        dotColour.setHsvF(static_cast<float>(rainbowFraction * 0.9), 1.0,
                          1.0);  //times 0.9 to keep the colour from getting too close to red, as that could confuse the end with the start

        double nextFraction = nodeFraction + nodeSpacing;
        double fromFraction = reverseComplement ? 1 - nodeFraction : nodeFraction;
        double toFraction = reverseComplement ? 1 - nextFraction : nextFraction;

        pen.setColor(dotColour);
        painter.setPen(pen);
        painter.drawPath(graphicsItemNode.makePartialPath(fromFraction, toFraction));

        nodeFraction = nextFraction;
        rainbowFraction += rainbowSpacing;
    }
}

void Annotation::drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement,
                            const std::set<ViewId> &viewsToShow) const {
    for (auto view_id : viewsToShow) {
        m_views[view_id]->drawFigure(painter, graphicsItemNode, reverseComplement, m_start, m_end);
    }
}

void Annotation::drawDescription(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const {
    double annotationCenter =
            (graphicsItemNode.indexToFraction(m_start) + graphicsItemNode.indexToFraction(m_end)) / 2;
    auto textPoint = graphicsItemNode.findLocationOnPath(
            reverseComplement ? 1 - annotationCenter : annotationCenter);
    auto qStringText = QString::fromStdString(m_text);

    QPainterPath textPath;
    QFontMetrics metrics(g_settings->labelFont);
    double shiftLeft = -metrics.boundingRect(qStringText).width() / 2.0;
    textPath.addText(shiftLeft, 0.0, g_settings->labelFont, qStringText);

    GraphicsItemNode::drawTextPathAtLocation(&painter, textPath, textPoint);
}

BedBlockView::BedBlockView(double widthMultiplier, const QColor &color, const std::vector<bed::Block> &blocks)
        : m_widthMultiplier(widthMultiplier), m_color(color) {
    m_blocks.reserve(blocks.size());
    for (const auto &block: blocks) {
        m_blocks.emplace_back(widthMultiplier, color, block.start, block.end);
    }
}

void BedBlockView::drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
                              int64_t end) const {
    for (const auto &block: m_blocks) {
        block.drawFigure(painter, graphicsItemNode, reverseComplement, start, end);
    }
}
