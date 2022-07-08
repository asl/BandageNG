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

#pragma once

#include "io/bed.h"

#include <utility>
#include <set>

using AnnotationGroupId = int;
using ViewId = int;

class QPainter;
class GraphicsItemNode;

class IAnnotationView {
public:
    virtual void
    drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
               int64_t end) const = 0;

    [[nodiscard]] virtual QString getTypeName() const = 0;

    virtual ~IAnnotationView() = default;
};


class SolidView : public IAnnotationView {
public:
    SolidView(double widthMultiplier, const QColor &color) : m_widthMultiplier(widthMultiplier), m_color(color) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
                    int64_t end) const override;

    [[nodiscard]] QString getTypeName() const override {
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
                    int64_t end) const override;

    [[nodiscard]] QString getTypeName() const override {
        return "Rainbow";
    }

private:
    double m_rainbowFractionStart;
    double m_rainbowFractionEnd;
};


class BedThickView : public SolidView {
public:
    BedThickView(double widthMultiplier, const QColor &color, int64_t mThickStart, int64_t mThickEnd) : SolidView(
            widthMultiplier, color), m_thickStart(mThickStart), m_thickEnd(mThickEnd) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t,
                    int64_t) const override {
        SolidView::drawFigure(painter, graphicsItemNode, reverseComplement, m_thickStart, m_thickEnd);
    }

    [[nodiscard]] QString getTypeName() const override {
        return "BED Thick";
    }

private:
    int64_t m_thickStart;
    int64_t m_thickEnd;
};


class BedBlockView : public IAnnotationView {
public:
    BedBlockView(double widthMultiplier, const QColor &color, const std::vector<bed::Block> &blocks);

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement, int64_t start,
                    int64_t end) const override;

    [[nodiscard]] QString getTypeName() const override {
        return "BED Blocks";
    }

private:
    const double m_widthMultiplier;
    const QColor m_color;
    std::vector<BedThickView> m_blocks;
};


class Annotation {
public:
    Annotation(int64_t start, int64_t end, std::string text) : m_start(start), m_end(end), m_text(std::move(text)) {}

    void drawFigure(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement,
                    const std::set<ViewId> &viewsToShow) const;

    void drawDescription(QPainter &painter, GraphicsItemNode &graphicsItemNode, bool reverseComplement) const;

    void addView(std::unique_ptr<IAnnotationView> view) {
        m_views.emplace_back(std::move(view));
    }

    [[nodiscard]] const std::vector<std::unique_ptr<IAnnotationView>> &getViews() const {
        return m_views;
    }

private:
    int64_t m_start;
    int64_t m_end;
    std::string m_text;
    std::vector<std::unique_ptr<IAnnotationView>> m_views;
};