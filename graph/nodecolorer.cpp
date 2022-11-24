// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

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
#include "graphicsitemnode.h"

#include "program/globals.h"
#include "program/settings.h"

#define TINYCOLORMAP_WITH_QT5
#include <colormap/tinycolormap.hpp>
#include "parallel_hashmap/phmap.h"

#include <unordered_set>

#include <QApplication>

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
        case CSV_COLUMN:
            return std::make_unique<CSVNodeColorer>(scheme);
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

// This function differs from the above by including all reverse complement
// nodes in the path search.
static std::vector<DeBruijnNode *>
getNodesCommonToAllPaths(std::vector< std::vector <DeBruijnNode *> > &paths,
                         bool includeReverseComplements) {
    std::vector<DeBruijnNode *> commonNodes;

    //If there are no paths, then return the empty vector.
    if (paths.empty())
        return commonNodes;

    //If there is only one path in path, then they are all common nodes
    commonNodes = paths[0];
    if (paths.size() == 1)
        return commonNodes;

    //If there are two or more paths, it's necessary to find the intersection.
    for (size_t i = 1; i < paths.size(); ++i) {
        QApplication::processEvents();
        std::vector<DeBruijnNode *> *path = &paths[i];

        // If we are including reverse complements in the search,
        // then it is necessary to build a new vector that includes
        // reverse complement nodes and then use that vector.
        std::vector<DeBruijnNode *> pathWithReverseComplements;
        if (includeReverseComplements) {
            for (auto *node : *path) {
                pathWithReverseComplements.push_back(node);
                pathWithReverseComplements.push_back(node->getReverseComplement());
            }
            path = &pathWithReverseComplements;
        }

        // Combine the commonNodes vector with the path vector,
        // excluding any repeats.
        std::sort(commonNodes.begin(), commonNodes.end());
        std::sort(path->begin(), path->end());
        std::vector<DeBruijnNode *> newCommonNodes;
        std::set_intersection(commonNodes.begin(), commonNodes.end(), path->begin(), path->end(), std::back_inserter(newCommonNodes));
        commonNodes = newCommonNodes;
    }

    return commonNodes;
}


// This function checks whether this node has any path leading outward that
// unambiguously leads to the given node.
// It checks a number of steps as set by the contiguitySearchSteps setting.
// If includeReverseComplement is true, then this function returns true if
// all paths lead either to the node or its reverse complement node.
static bool doesPathLeadOnlyToNode(DeBruijnNode *sNode, const DeBruijnNode *tNode, bool includeReverseComplement) {
    for (auto *edge : sNode->edges()) {
        bool outgoingEdge = (sNode == edge->getStartingNode());
        if (edge->leadsOnlyToNode(outgoingEdge, g_settings->contiguitySearchSteps, tNode,
                                  { sNode }, includeReverseComplement))
            return true;
    }

    return false;
}

ContiguityStatus ContiguityNodeColorer::getContiguityStatus(const DeBruijnNode* node) const {
    auto it = m_nodeStatuses.find(node);
    if (it == m_nodeStatuses.end())
        return ContiguityStatus::NOT_CONTIGUOUS;

    return it->second;
}

//This function only upgrades a node's status, never downgrades.
void ContiguityNodeColorer::upgradeContiguityStatus(const DeBruijnNode *node,
                                                    ContiguityStatus newStatus) {
    auto &status = m_nodeStatuses[node];
    if (newStatus > status)
        status = newStatus;
}

// This function determines the contiguity of nodes relative to this one.
// It has two steps:
// -First, for each edge leaving this node, all paths outward are found.
//  Any nodes in any path are MAYBE_CONTIGUOUS, and nodes in all of the
//  paths are CONTIGUOUS.
// -Second, it is necessary to check in the opposite direction - for each
//  of the MAYBE_CONTIGUOUS nodes, do they have a path that unambiguously
//  leads to this node?  If so, then they are CONTIGUOUS.
void ContiguityNodeColorer::determineContiguity(DeBruijnNode* node) {
    upgradeContiguityStatus(node, STARTING);

    //A set is used to store all nodes found in the paths, as the nodes
    //that show up as MAYBE_CONTIGUOUS will have their paths checked
    //to this node.
    std::set<DeBruijnNode *> allCheckedNodes;

    //For each path leaving this node, find all possible paths
    //outward.  Nodes in any of the paths for an edge are
    //MAYBE_CONTIGUOUS.  Nodes in all of the paths for an edge
    //are CONTIGUOUS.
    for (auto edge : node->edges()) {
        bool outgoingEdge = (node == edge->getStartingNode());

        std::vector<std::vector<DeBruijnNode *>> allPaths;
        edge->tracePaths(outgoingEdge, g_settings->contiguitySearchSteps, allPaths, node);

        // Set all nodes in the paths as MAYBE_CONTIGUOUS
        for (auto &path : allPaths) {
            QApplication::processEvents();
            for (auto *pNode : path) {
                upgradeContiguityStatus(pNode, MAYBE_CONTIGUOUS);
                allCheckedNodes.insert(node);
            }
        }

        // Set all common nodes as CONTIGUOUS_STRAND_SPECIFIC
        for (auto *pNode : getNodesCommonToAllPaths(allPaths, false))
            upgradeContiguityStatus(pNode, CONTIGUOUS_STRAND_SPECIFIC);

        // Set all common nodes (when including reverse complement nodes)
        // as CONTIGUOUS_EITHER_STRAND
        for (auto *pNode : getNodesCommonToAllPaths(allPaths, true)) {
            upgradeContiguityStatus(pNode, CONTIGUOUS_EITHER_STRAND);
            upgradeContiguityStatus(pNode->getReverseComplement(), CONTIGUOUS_EITHER_STRAND);
        }
    }

    //For each node that was checked, then we check to see if any
    //of its paths leads unambiuously back to the starting node (this node).
    for (auto *cNode : allCheckedNodes) {
        QApplication::processEvents();
        ContiguityStatus status = getContiguityStatus(cNode);

        //First check without reverse complement target for
        //strand-specific contiguity.
        if (status != CONTIGUOUS_STRAND_SPECIFIC &&
            doesPathLeadOnlyToNode(cNode, node, false))
            upgradeContiguityStatus(cNode, CONTIGUOUS_STRAND_SPECIFIC);

        //Now check including the reverse complement target for
        //either strand contiguity.
        if (status != CONTIGUOUS_STRAND_SPECIFIC &&
            status != CONTIGUOUS_EITHER_STRAND &&
            doesPathLeadOnlyToNode(cNode, node, true)) {
            upgradeContiguityStatus(cNode, CONTIGUOUS_EITHER_STRAND);
            upgradeContiguityStatus(cNode->getReverseComplement(), CONTIGUOUS_EITHER_STRAND);
        }
    }
}


QColor ContiguityNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;

    // For single nodes, display the colour of whichever of the
    // twin nodes has the greatest contiguity status.
    ContiguityStatus contiguityStatus = getContiguityStatus(deBruijnNode);
    if (!node->m_hasArrow) {
        ContiguityStatus twinContiguityStatus = getContiguityStatus(deBruijnNode->getReverseComplement());
        if (twinContiguityStatus > contiguityStatus)
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

    auto tags = m_graph->m_nodeTags.find(deBruijnNode);
    if (tags != m_graph->m_nodeTags.end()) {
        if (auto tag = gfa::getTag(m_tagName.c_str(), tags->second)) {
            std::stringstream stream;
            stream << *tag;
            return m_allTags.at(stream.str());
        }
    }

    return m_graph->getCustomColourForDisplay(deBruijnNode);
}

void TagValueNodeColorer::reset() {
    m_allTags.clear();
    m_tagNames.clear();

    // Collect all tags and their corresponding values
    for (const auto &entry : m_graph->m_nodeTags) {
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

QColor CSVNodeColorer::get(const GraphicsItemNode *node) {
    const DeBruijnNode *deBruijnNode = node->m_deBruijnNode;

    auto val = m_graph->getCsvLine(deBruijnNode, m_colIdx);
    if (!val || m_colIdx >= m_colors.size())
        return m_graph->getCustomColourForDisplay(deBruijnNode);

    const auto &cols = m_colors[m_colIdx];
    auto col = cols.find(val->toStdString());
    if (col == cols.end())
        return m_graph->getCustomColourForDisplay(deBruijnNode);

    return *col;;
}

void CSVNodeColorer::reset() {
    m_colors.clear();

    size_t columns = m_graph->m_csvHeaders.size();
    m_colors.resize(columns);
    // Store all unique values into a map
    for (const auto &entry : m_graph->m_nodeCSVData) {
        const auto &row = entry.second;
        for (size_t i = 0; i < row.size() && i < columns; ++i) {
            const auto &cell = row[i];
            m_colors[i].insert(cell.toStdString(), QColor(cell));
        }
    }

    // Assign all invalid colors according to colormap
    for (auto &column : m_colors) {
        size_t invalid = 0;
        for (const auto &entry : column)
            invalid += !entry.isValid();
        size_t i = 0;
        for (auto &entry : column) {
            if (entry.isValid())
                continue;
            entry = tinycolormap::GetColor(double(i) / double(invalid),
                                           colorMap(g_settings->colorMap)).ConvertToQColor();
            i += 1;
        }
    }
}
