// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#include "nodecolorer.h"
#include "assemblygraph.h"
#include "debruijnnode.h"
#include "program/globals.h"

INodeColorer::INodeColorer(NodeColorScheme scheme)
    : m_graph(g_assemblyGraph), m_scheme(scheme) {}


std::pair<QColor, QColor> INodeColorer::get(const GraphicsItemNode *node,
                                            const GraphicsItemNode *rcNode) {
    QColor posColor = this->get(node);
    QColor negColor = rcNode ? this->get(rcNode) : posColor;

    return { posColor, negColor };
}

class DepthNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Color by depth"; };
};

class UniformNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Uniform color"; };
};

class RandomNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] std::pair<QColor, QColor> get(const GraphicsItemNode *node,
                                                const GraphicsItemNode *rcNode) override;
    [[nodiscard]] const char* name() const override { return "Random colors"; };
};

class GrayNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Gray colors"; };
};

class CustomNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Custom colors"; };
};

class ContiguityNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Color by contiguity"; };
};

std::unique_ptr<INodeColorer> INodeColorer::create(NodeColorScheme scheme) {
    switch (scheme) {
        case UNIFORM_COLOURS:
            return std::make_unique<UniformNodeColorer>(scheme);
        case RANDOM_COLOURS:
            return std::make_unique<RandomNodeColorer>(scheme);
        case DEPTH_COLOUR:
            return std::make_unique<DepthNodeColorer>(scheme);
        case CONTIGUITY_COLOUR:
            return std::make_unique<ContiguityNodeColorer>(scheme);
        case CUSTOM_COLOURS:
            return std::make_unique<CustomNodeColorer>(scheme);
        case GRAY_COLOR:
            return std::make_unique<GrayNodeColorer>(scheme);
    }

    return nullptr;
}

static QColor interpolateRgb(QColor from, QColor to, float fraction) {
    float rDiff = to.redF() - from.redF();
    float gDiff = to.greenF() - from.greenF();
    float bDiff = to.blueF() - from.blueF();
    float aDiff = to.alphaF() - from.alphaF();

    return QColor::fromRgbF(from.redF() + rDiff * fraction,
                            from.greenF() + gDiff * fraction,
                            from.blueF() + bDiff * fraction,
                            from.alphaF() + aDiff * fraction);
}

QColor DepthNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;
    double depth = deBruijnNode->getDepth();
    double lowValue;
    double highValue;
    if (g_settings->autoDepthValue) {
        lowValue = m_graph->m_firstQuartileDepth;
        highValue = m_graph->m_thirdQuartileDepth;
    } else {
        lowValue = g_settings->lowDepthValue;
        highValue = g_settings->highDepthValue;
    }

    if (depth <= lowValue)
        return g_settings->lowDepthColour;
    if (depth >= highValue)
        return g_settings->highDepthColour;

    float fraction = (depth - lowValue) / (highValue - lowValue);
    return interpolateRgb(g_settings->lowDepthColour, g_settings->highDepthColour, fraction);
}

QColor UniformNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;

    if (deBruijnNode->isSpecialNode())
        return g_settings->uniformNodeSpecialColour;
    else if (node->usePositiveNodeColour())
        return g_settings->uniformPositiveNodeColour;
    else
        return g_settings->uniformNegativeNodeColour;
}

QColor RandomNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;

    // Make a colour with a random hue.
    int hue = rand() % 360;
    QColor posColour;
    posColour.setHsl(hue,
                     g_settings->randomColourPositiveSaturation,
                     g_settings->randomColourPositiveLightness);
    posColour.setAlpha(g_settings->randomColourPositiveOpacity);

    QColor negColour;
    negColour.setHsl(hue,
                     g_settings->randomColourNegativeSaturation,
                     g_settings->randomColourNegativeLightness);
    negColour.setAlpha(g_settings->randomColourNegativeOpacity);

    if (!deBruijnNode->isPositiveNode())
        std::swap(posColour, negColour);

    return posColour;
}

std::pair<QColor, QColor> RandomNodeColorer::get(const GraphicsItemNode *node, const GraphicsItemNode *rcNode) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;

    // Make a colour with a random hue.  Assign a colour to both this node and
    // its complement so their hue matches.
    int hue = rand() % 360;
    QColor posColour;
    posColour.setHsl(hue,
                     g_settings->randomColourPositiveSaturation,
                     g_settings->randomColourPositiveLightness);
    posColour.setAlpha(g_settings->randomColourPositiveOpacity);

    QColor negColour;
    negColour.setHsl(hue,
                     g_settings->randomColourNegativeSaturation,
                     g_settings->randomColourNegativeLightness);
    negColour.setAlpha(g_settings->randomColourNegativeOpacity);

    if (!deBruijnNode->isPositiveNode())
        std::swap(posColour, negColour);

    return { posColour, negColour };
}

QColor GrayNodeColorer::get(const GraphicsItemNode *node) {
    return g_settings->grayColor;
}

QColor CustomNodeColorer::get(const GraphicsItemNode *node) {
    return m_graph->getCustomColourForDisplay(node->m_deBruijnNode);;
}

QColor ContiguityNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;

    // For single nodes, display the colour of whichever of the
    // twin nodes has the greatest contiguity status.
    ContiguityStatus contiguityStatus = deBruijnNode->getContiguityStatus();
    if (!node->m_hasArrow) {
        ContiguityStatus twinContiguityStatus = deBruijnNode->getReverseComplement()->getContiguityStatus();
        if (twinContiguityStatus < contiguityStatus)
            contiguityStatus = twinContiguityStatus;
    }

    switch (contiguityStatus) {
        case STARTING:
            return g_settings->contiguityStartingColour;
        case CONTIGUOUS_STRAND_SPECIFIC:
            return g_settings->contiguousStrandSpecificColour;
        case CONTIGUOUS_EITHER_STRAND:
            return g_settings->contiguousEitherStrandColour;
        case MAYBE_CONTIGUOUS:
            return g_settings->maybeContiguousColour;
        default: //NOT_CONTIGUOUS
            return g_settings->notContiguousColour;
    }
}