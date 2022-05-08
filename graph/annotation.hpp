#ifndef BANDAGE_GRAPH_ANNOTATION_HPP_
#define BANDAGE_GRAPH_ANNOTATION_HPP_

#include <utility>

#include <QColor>
#include <QPainterPath>
#include <QPainter>

#include "blast/blasthit.h"
#include "blast/blastquery.h"
#include "graphicsitemnode.h"
#include "program/settings.h"


class IAnnotationView {
public:
    virtual void
    drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
               int64_t end) const = 0;

    virtual QString getTypeName() const = 0;
};


class SolidView : public IAnnotationView {
public:
    SolidView(double widthMultiplier, const QColor &color) : m_widthMultiplier(widthMultiplier), m_color(color) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
                    int64_t end) const override {
        QPen pen;
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::BevelJoin);

        pen.setWidthF(m_widthMultiplier * graphicsItemNode.m_width);

        pen.setColor(m_color);
        painter.setPen(pen);

        double fractionStart = graphicsItemNode.indexToFraction(start);
        double fractionEnd = graphicsItemNode.indexToFraction(end);

        if (reverseComplement) {
            fractionStart = 1 - fractionStart;
            fractionEnd = 1 - fractionEnd;
        }
        painter.drawPath(graphicsItemNode.makePartialPath(fractionStart, fractionEnd));
    }

    QString getTypeName() const override {
        return "Solid";
    }

private:
    const double m_widthMultiplier;
    const QColor m_color;
};


class RainbowBlastHitView : public IAnnotationView {
public:
    RainbowBlastHitView(double rainbowFractionStart, double rainbowFractionEnd)
            : m_rainbowFractionStart(rainbowFractionStart), m_rainbowFractionEnd(rainbowFractionEnd) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
                    int64_t end) const override {

        double scaledNodeLength = graphicsItemNode.getNodePathLength() * g_absoluteZoom;
        double fractionStart = graphicsItemNode.indexToFraction(start);
        double fractionEnd = graphicsItemNode.indexToFraction(end);
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

    QString getTypeName() const override {
        return "Rainbow";
    }

private:
    double m_rainbowFractionStart;
    double m_rainbowFractionEnd;
};


class Annotation {
public:
    Annotation(int64_t start, int64_t end, std::string text) : m_start(start), m_end(end), m_text(std::move(text)) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement,
                    const std::set<ViewId> &viewsToShow) const {
        for (auto view_id : viewsToShow) {
            m_views[view_id]->drawFigure(painter, graphicsItemNode, reverseComplement, m_start, m_end);
        }
    }

    void drawDescription(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const {
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

    void addView(std::unique_ptr<IAnnotationView> view) {
        m_views.emplace_back(std::move(view));
    }

    const std::vector<std::unique_ptr<IAnnotationView>> &getViews() const {
        return m_views;
    }

private:
    int64_t m_start;
    int64_t m_end;
    std::string m_text;
    std::vector<std::unique_ptr<IAnnotationView>> m_views;
};

#endif //BANDAGE_GRAPH_ANNOTATION_HPP_
