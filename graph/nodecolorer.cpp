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
#include "nodecolorers.h"
#include "assemblygraph.h"
#include "debruijnnode.h"
#include "program/globals.h"

#define TINYCOLORMAP_WITH_QT5
#include <colormap/tinycolormap.hpp>
#include "parallel_hashmap/phmap.h"

#include <unordered_set>

INodeColorer::INodeColorer(NodeColorScheme scheme)
    : m_graph(g_assemblyGraph), m_scheme(scheme) {
}

std::pair<QColor, QColor> INodeColorer::get(const GraphicsItemNode *node,
                                            const GraphicsItemNode *rcNode) {
    QColor posColor = this->get(node);
    QColor negColor = rcNode ? this->get(rcNode) : posColor;

    return { posColor, negColor };
}

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
        case GC_CONTENT:
            return std::make_unique<GCNodeColorer>(scheme);
        case TAG_VALUE:
            return std::make_unique<TagValueNodeColorer>(scheme);
    }

    return nullptr;
}

QColor DepthNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;
    double depth = deBruijnNode->getDepth();

    double lowValue = g_settings->lowDepthValue, highValue = g_settings->highDepthValue;
    if (g_settings->autoDepthValue) {
        lowValue = m_graph->m_firstQuartileDepth;
        highValue = m_graph->m_thirdQuartileDepth;
    }

    float fraction = (depth - lowValue) / (highValue - lowValue);
    return tinycolormap::GetColor(fraction, colorMap(g_settings->colorMap)).ConvertToQColor();
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

QColor GCNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;
    float lowValue = 0.2, highValue = 0.8, value = deBruijnNode->getGC();
    float fraction = (value - lowValue) / (highValue - lowValue);
    return tinycolormap::GetColor(fraction, colorMap(g_settings->colorMap)).ConvertToQColor();
}

QColor TagValueNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;

    auto tags = g_assemblyGraph->m_nodeTags.find(deBruijnNode);
    if (tags != g_assemblyGraph->m_nodeTags.end()) {
        if (auto tag = gfa::getTag(m_tagName.c_str(), tags->second)) {
            std::stringstream stream;
            stream << *tag;
            return m_allTags.at(stream.str());
        }
    }

    return {};
}

void TagValueNodeColorer::reset() {
    m_allTags.clear();
    m_tagNames.clear();

    // Collect all tags and their corresponding values
    for (const auto &entry : g_assemblyGraph->m_nodeTags) {
        for (const auto &tag: entry.second) {
            std::stringstream stream;
            stream << tag;
            std::string tagWithValue = stream.str();
            std::string tagName{std::string_view(tag.name, 2)};
            m_allTags.insert(tagWithValue, QColor());
            m_tagNames.insert(tagName);
        }
    }

    // Assign colors
    for (const auto &tagName : m_tagNames) {
        auto tagRange = m_allTags.equal_prefix_range(tagName);
        size_t sz = std::distance(tagRange.first, tagRange.second), i = 0;
        for (auto it = tagRange.first; it != tagRange.second; ++it) {
            *it = tinycolormap::GetColor(double(i) / double(sz),
                                         colorMap(g_settings->colorMap)).ConvertToQColor();
            i += 1;
        }
    }

    if (!m_tagNames.empty())
        m_tagName = *m_tagNames.begin();
}