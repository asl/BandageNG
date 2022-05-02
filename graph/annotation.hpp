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


class IAnnotation {
  public:
    virtual void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const = 0;

    virtual void drawDescription(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const = 0;
};


class CommonAnnotation : public IAnnotation {
  public:
    CommonAnnotation(int64_t start, int64_t end, std::string text)
        : m_start(start), m_end(end), m_text(std::move(text)) {

    }
    void drawDescription(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const override {
        double annotationCenter = (graphicsItemNode.indexToFraction(m_start) + graphicsItemNode.indexToFraction(m_end)) / 2;
        auto textPoint = graphicsItemNode.findLocationOnPath(reverseComplement ? 1 - annotationCenter : annotationCenter);
        auto qStringText = QString::fromStdString(m_text);

        QPainterPath textPath;
        QFontMetrics metrics(g_settings->labelFont);
        double shiftLeft = -metrics.boundingRect(qStringText).width() / 2.0;
        textPath.addText(shiftLeft, 0.0, g_settings->labelFont, qStringText);

        GraphicsItemNode::drawTextPathAtLocation(&painter, textPath, textPoint);
    }

  protected:
    int64_t m_start;
    int64_t m_end;
    std::string m_text;
};


class SolidAnnotation : public CommonAnnotation {
  public:
    SolidAnnotation(int64_t start,
                    int64_t end,
                    std::string text,
                    double widthMultiplier,
                    const QColor &color)
        : CommonAnnotation(start, end, std::move(text)), m_widthMultiplier(widthMultiplier), m_color(color) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const override {
        QPen pen;
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::BevelJoin);

        pen.setWidthF(m_widthMultiplier * graphicsItemNode.m_width);

        pen.setColor(m_color);
        painter.setPen(pen);

        double fractionStart = graphicsItemNode.indexToFraction(m_start);
        double fractionEnd = graphicsItemNode.indexToFraction(m_end);

        if (reverseComplement) {
            fractionStart = 1 - fractionStart;
            fractionEnd = 1 - fractionEnd;
        }
        painter.drawPath(graphicsItemNode.makePartialPath(fractionStart, fractionEnd));
    }

  private:
    double m_widthMultiplier;
    QColor m_color;
};


class RainbowBlastHitAnnotation : public CommonAnnotation {
  public:
    RainbowBlastHitAnnotation(int64_t start,
                              int64_t end,
                              std::string text,
                              double rainbowFractionStart,
                              double rainbowFractionEnd)
        : CommonAnnotation(start, end, std::move(text)),
          m_rainbowFractionStart(rainbowFractionStart),
          m_rainbowFractionEnd(rainbowFractionEnd) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const override {
        double scaledNodeLength = graphicsItemNode.getNodePathLength() * g_absoluteZoom;
        double fractionStart = graphicsItemNode.indexToFraction(m_start);
        double fractionEnd = graphicsItemNode.indexToFraction(m_end);
        double scaledHitLength = (fractionEnd - fractionStart) * scaledNodeLength;
        int partCount = ceil(g_settings->blastRainbowPartsPerQuery * fabs(m_rainbowFractionStart - m_rainbowFractionEnd));

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
            dotColour.setHsvF(static_cast<float>(rainbowFraction * 0.9), 1.0, 1.0);  //times 0.9 to keep the colour from getting too close to red, as that could confuse the end with the start

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

    void drawDescription(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const override {
        double annotationCenter = (graphicsItemNode.indexToFraction(m_start) + graphicsItemNode.indexToFraction(m_end)) / 2;
        auto textPoint = graphicsItemNode.findLocationOnPath(reverseComplement ? 1 - annotationCenter : annotationCenter);
        auto qStringText = QString::fromStdString(m_text);

        QPainterPath textPath;
        QFontMetrics metrics(g_settings->labelFont);
        double shiftLeft = -metrics.boundingRect(qStringText).width() / 2.0;
        textPath.addText(shiftLeft, 0.0, g_settings->labelFont, qStringText);

        GraphicsItemNode::drawTextPathAtLocation(&painter, textPath, textPoint);
    }

  private:
    double m_rainbowFractionStart;
    double m_rainbowFractionEnd;
};

#endif //BANDAGE_GRAPH_ANNOTATION_HPP_
