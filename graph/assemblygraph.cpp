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


#include "assemblygraph.h"
#include "debruijnedge.h"
#include "graph/debruijnnode.h"
#include "parallel_hashmap/phmap.h"
#include "path.h"
#include "io.h"
#include "program/llvm.h"
#include "graphicsitemedge.h"
#include "graphicsitemnode.h"
#include "nodedisplayname.h"
#include "sequenceutils.h"

#include "layout/graphlayoutworker.h"

#include "program/memory.h"
#include "program/globals.h"
#include "program/settings.h"
#include "graph/annotationsmanager.h"

#include "ui/dialogs/myprogressdialog.h"
#include "ui/bandagegraphicsscene.h"

#include <QApplication>
#include <QFile>
#include <QList>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <cmath>
#include <utility>
#include <deque>
#include <unordered_set>

AssemblyGraph::AssemblyGraph()
        : m_sequencesLoadedFromFasta(NOT_READY)
{
    clearGraphInfo();
}

AssemblyGraph::~AssemblyGraph() = default;


template<typename T> double getValueUsingFractionalIndex(const std::vector<T> &v, double index) {
    if (v.size() == 0)
        return 0.0;
    if (v.size() == 1)
        return double(v.front());

    int wholePart = floor(index);

    if (wholePart < 0)
        return double(v.front());
    if (wholePart >= int(v.size()) - 1)
        return double(v.back());

    double fractionalPart = index - wholePart;

    double piece1 = double(v[wholePart]);
    double piece2 = double(v[wholePart+1]);

    return piece1 * (1.0 - fractionalPart) + piece2 * fractionalPart;
}

void AssemblyGraph::cleanUp() {
    m_deBruijnGraphPaths.clear();
    m_deBruijnGraphWalks.clear();

    for (DeBruijnNode *node : nodes())
        delete node;
    m_deBruijnGraphNodes.clear();

    {
        for (DeBruijnEdge *edge : m_deBruijnGraphEdges) {
            delete edge;
            edge = nullptr;
        }
        m_deBruijnGraphEdges.clear();
    }

    m_nodeTags.clear();
    m_edgeTags.clear();
    m_nodeColors.clear();
    m_edgeColors.clear();
    m_edgeStyles.clear();
    m_nodeLabels.clear();
    m_edgeReverseColors.clear();
    m_reverseEdgeGraphics.clear();
    m_negativeNodeGraphics.clear();
    m_overlapKeys.clear();
    m_nodeCSVData.clear();
    m_csvHeaders.clear();
    m_depthTag.clear();
    m_filename.clear();
    m_sequencesLoadedFromFasta = NOT_READY;

    clearGraphInfo();
}

//The function returns a node name, replacing "+" at the end with "-" or
//vice-versa.
static std::string getOppositeNodeName(std::string_view nodeName) {
    std::string opposite(nodeName);
    if (!opposite.empty())
        opposite.back() = opposite.back() == '-' ? '+' : '-';
    return opposite;
}

// One object per link. The ends are stored in the positive orientation
// when the link is created. A later rename can make isPositiveEdge()
// false; the object is still the only link. The opposite direction is
// that same pointer: connects() matches the stored ends and
// rc(stored end) -> rc(stored start). The returned MadeLink::reversed is
// set when the requested ends are that opposite direction.
AssemblyGraph::MadeLink AssemblyGraph::makeLink(OrientedNode from, OrientedNode to,
                                                int overlap, EdgeOverlapType overlapType,
                                                bool allowDistinctOverlap,
                                                std::string_view overlapKey) {
    // A hit is an existing link, not a new one. When allowDistinctOverlap
    // is set, two links between the same ends stay separate when the
    // length, the overlap type, or the overlap key differ.
    // createDeBruijnEdge passes false: any connection of these ends is
    // the link, and a repeat is rejected.
    for (auto *edge : from.edges()) {
        if (!edge->connects(from, to))
            continue;
        if (allowDistinctOverlap) {
            auto keyIt = m_overlapKeys.find(edge);
            auto existingKey = keyIt == m_overlapKeys.end()
                                   ? std::string_view{}
                                   : std::string_view(keyIt->second);
            if (edge->getOverlap() != overlap || edge->getOverlapType() != overlapType ||
                existingKey != overlapKey)
                continue;
        }
        // Requested ends that are not the stored ends are the opposite
        // direction. An own-RC link has only one direction, so
        // reversed stays false.
        bool reversed = edge->getStartingNode() != from || edge->getEndingNode() != to;
        if (edge->isOwnReverseComplement())
            reversed = false;
        return {edge, reversed, false};
    }

    // from == rc(to) and to == rc(from): the opposite direction is this
    // same pair of nodes (A+ -> A-, or a self-loop on a palindromic
    // node). Keep the requested ends. Every other negative request is
    // stored as rc(to) -> rc(from), the positive orientation, and
    // reversed is set because the request ran the other way.
    bool own = from == to.reverse() && to == from.reverse();
    OrientedNode storedFrom = from;
    OrientedNode storedTo = to;
    bool reversed = false;
    if (!own && !DeBruijnEdge::isPositiveOrientation(from, to)) {
        storedFrom = to.reverse();
        storedTo = from.reverse();
        reversed = true;
    }

    auto *edge = new DeBruijnEdge(storedFrom, storedTo);
    m_deBruijnGraphEdges.emplace(edge);
    // One list per node. The reverse orientation is the same object, so
    // a second insert is dropped. Direction comes from the oriented ends.
    storedFrom.node()->addEdge(edge);
    if (storedTo.node() != storedFrom.node())
        storedTo.node()->addEdge(edge);
    edge->setOverlap(overlap);
    edge->setOverlapType(overlapType);
    // An exact overlap is identified by its length. Any other overlap
    // also needs its key, kept off the object.
    if (!overlapKey.empty())
        m_overlapKeys[edge] = std::string(overlapKey);
    return {edge, reversed, true};
}

// One edge object for the link, stored in the positive orientation.
// All four oriented nodes hold that pointer.
void AssemblyGraph::createDeBruijnEdge(std::string_view node1Name, std::string_view node2Name,
                                       int overlap, EdgeOverlapType overlapType) {
    std::string node1Opposite = getOppositeNodeName(node1Name);
    std::string node2Opposite = getOppositeNodeName(node2Name);

    //Quit if any of the nodes don't exist.
    auto node1 = m_deBruijnGraphNodes.find(node1Name);
    auto node2 = m_deBruijnGraphNodes.find(node2Name);
    auto negNode1 = m_deBruijnGraphNodes.find(node1Opposite);
    auto negNode2 = m_deBruijnGraphNodes.find(node2Opposite);
    if (node1 == m_deBruijnGraphNodes.end() ||
        node2 == m_deBruijnGraphNodes.end() ||
        negNode1 == m_deBruijnGraphNodes.end() ||
        negNode2 == m_deBruijnGraphNodes.end())
        return;

    makeLink(orientationOf(node1Name, *node1), orientationOf(node2Name, *node2),
             overlap, overlapType, false);
}

OrientedNode AssemblyGraph::orientedNode(std::string_view signedName) const {
    auto it = m_deBruijnGraphNodes.find(signedName);
    if (it == m_deBruijnGraphNodes.end())
        return {};
    return orientationOf(signedName, *it);
}

GraphicsItemNode *AssemblyGraph::negativeNodeGraphics(const DeBruijnNode *node) const {
    auto it = m_negativeNodeGraphics.find(node);
    return it == m_negativeNodeGraphics.end() ? nullptr : it->second;
}

void AssemblyGraph::setNegativeNodeGraphics(DeBruijnNode *node, GraphicsItemNode *item) {
    if (item == nullptr)
        m_negativeNodeGraphics.erase(node);
    else
        m_negativeNodeGraphics[node] = item;
}

void AssemblyGraph::resetNodes()
{
    for (DeBruijnNode *node : nodes())
        node->resetNode();
    m_negativeNodeGraphics.clear();
}

void AssemblyGraph::resetEdges()
{
    for (DeBruijnEdge *edge : m_deBruijnGraphEdges)
        edge->reset();
    m_reverseEdgeGraphics.clear();
}

void AssemblyGraph::clearEdgeGraphicsItems() {
    for (DeBruijnEdge *edge : m_deBruijnGraphEdges)
        edge->setGraphicsItemEdge(nullptr);
    m_reverseEdgeGraphics.clear();
}

void AssemblyGraph::clearNodeGraphicsItems() {
    for (DeBruijnNode *node : nodes())
        node->setGraphicsItemNode(nullptr);
    m_negativeNodeGraphics.clear();
}


double AssemblyGraph::getMeanDepth(bool drawnNodesOnly)
{
    long double depthSum = 0.0;
    long long totalLength = 0;

    for (OrientedNode positive : canonical_nodes()) {
        auto add = [&](OrientedNode node) {
            if (drawnNodesOnly && node.isNotDrawn())
                return;
            totalLength += node.getLength();
            depthSum += node.getLength() * node.getDepth();
        };
        add(positive);
        // reverse() of a palindrome is that same orientation, so it is counted twice.
        add(positive.reverse());
    }

    if (totalLength == 0)
        return 0.0;
    else
        return depthSum / totalLength;
}


double AssemblyGraph::getMeanDepth(const std::vector<OrientedNode> &nodes)
{
    if (nodes.empty())
        return 0.0;

    if (nodes.size() == 1)
        return nodes[0].getDepth();

    int nodeCount = 0;
    long double depthSum = 0.0;
    long long totalLength = 0;

    for (auto node : nodes)
    {
        ++nodeCount;
        totalLength += node.getLength();
        depthSum += node.getLength() * node.getDepth();
    }

    //If the total length is zero, that means all nodes have a length of zero.
    //In this case, just return the average node depth.
    if (totalLength == 0)
    {
        long double depthSum = 0.0;
        for (auto & node : nodes)
            depthSum += node.getDepth();
        return depthSum / nodes.size();
    }

    return depthSum / totalLength;
}

namespace {
// Characters that live outside the string object. A short string is inside it.
uint64_t stringHeapBytes(const std::string &text) {
    const char *data = text.data();
    const char *object = reinterpret_cast<const char *>(&text);
    if (data >= object && data < object + sizeof(text))
        return 0;
    return text.capacity() + 1;
}

uint64_t utf16Bytes(const QString &text) {
    return uint64_t(text.size()) * sizeof(char16_t);
}

template<class Map>
uint64_t tagMapBytes(const Map &tags) {
    uint64_t bytes = 0;
    for (const auto &entry : tags) {
        bytes += entry.second.capacity() * sizeof(gfa::tag);
        for (const gfa::tag &tag : entry.second)
            if (const auto *text = std::get_if<std::string>(&tag.val))
                bytes += stringHeapBytes(*text);
    }
    return bytes;
}
}

AssemblyGraph::StorageAccount AssemblyGraph::storageAccount() const {
    StorageAccount account;
    account.edges = m_deBruijnGraphEdges.size();
    account.edgeBytes = account.edges * sizeof(DeBruijnEdge);
    account.edgeIndexBytes = account.edges * sizeof(DeBruijnEdge *);

    for (DeBruijnNode *node : nodes()) {
        ++account.nodes;
        const uint64_t nameBytes = node->getNameWithoutSign().size();
        // Both signed keys are the unsigned name plus one sign character.
        account.nodeIndexBytes += 2 * (nameBytes + 1);
        account.nodeNameBytes += node->nameHeapBytes();
        account.sequenceBytes += node->getSequence().allocatedBytes();
        account.incidentEdgeBytes += node->edgeListHeapBytes();
        if (node->sequenceIsMissing())
            ++account.nodesWithoutSequence;
    }
    account.nodeBytes = account.nodes * sizeof(DeBruijnNode);
    account.nodeIndexBytes += m_deBruijnGraphNodes.size() * sizeof(DeBruijnNode *);

    std::string key;
    for (auto it = m_deBruijnGraphPaths.begin(); it != m_deBruijnGraphPaths.end(); ++it) {
        it.key(key);
        const Path &path = it.value();
        account.pathBytes += key.size() + sizeof(Path);
        account.pathBytes += path.nodes().capacity() * sizeof(OrientedNode);
        account.pathBytes += path.edges().capacity() * sizeof(DeBruijnEdge *);
    }
    account.paths = m_deBruijnGraphPaths.size();

    for (auto it = m_deBruijnGraphWalks.begin(); it != m_deBruijnGraphWalks.end(); ++it) {
        it.key(key);
        const Walk &walk = it.value();
        account.walkBytes += key.size() + sizeof(Walk) + stringHeapBytes(walk.sampleId);
        account.walkBytes += walk.walk.nodes().capacity() * sizeof(OrientedNode);
        account.walkBytes += walk.walk.edges().capacity() * sizeof(DeBruijnEdge *);
    }
    account.walks = m_deBruijnGraphWalks.size();

    account.annotationBytes += tagMapBytes(m_nodeTags);
    account.annotationBytes += tagMapBytes(m_edgeTags);
    for (const auto &entry : m_overlapKeys)
        account.annotationBytes += stringHeapBytes(entry.second);
    for (const auto &entry : m_nodeLabels)
        account.annotationBytes += utf16Bytes(entry.second);
    for (const QString &header : m_csvHeaders)
        account.annotationBytes += utf16Bytes(header);
    for (const auto &entry : m_nodeCSVData)
        for (const QString &cell : entry.second)
            account.annotationBytes += utf16Bytes(cell);
    account.annotationBytes += m_nodeColors.size() * sizeof(QColor);
    account.annotationBytes += m_edgeColors.size() * sizeof(QColor);
    account.annotationBytes += m_edgeReverseColors.size() * sizeof(QColor);
    account.annotationBytes += m_edgeStyles.size() * sizeof(EdgeStyle);
    return account;
}

void AssemblyGraph::determineGraphInfo()
{
    m_shortestContig = std::numeric_limits<long long>::max();
    m_longestContig = 0;
    int nodeCount = 0;
    long long totalLength = 0;
    long double depthSum = 0.0;
    long long depthLength = 0;
    std::vector<double> nodeDepths;

    for (OrientedNode node : canonical_nodes()) {
        long long nodeLength = node.getLength();

        if (nodeLength < m_shortestContig)
            m_shortestContig = nodeLength;
        if (nodeLength > m_longestContig)
            m_longestContig = nodeLength;

        // A palindrome is one orientation and is counted twice.
        const int copies = node.isSelfComplement() ? 2 : 1;
        totalLength += nodeLength * copies;
        nodeCount += copies;
        // Both strands have this length and depth, so the mean matches
        // getMeanDepth() without a second walk.
        const long long countedLength = nodeLength * 2;
        depthLength += countedLength;
        depthSum += static_cast<long double>(countedLength) * node.getDepth();

        nodeDepths.push_back(node.getDepth());
        nodeDepths.push_back(node.getDepth());
    }

    // Each stored edge is one link, including after a rename changes
    // which orientation isPositiveEdge() would report.
    m_edgeCount = static_cast<int>(m_deBruijnGraphEdges.size());
    m_nodeCount = nodeCount;
    m_totalLength = totalLength;
    m_meanDepth = depthLength == 0 ? 0.0 : static_cast<double>(depthSum / depthLength);

    std::sort(nodeDepths.begin(), nodeDepths.end());

    double firstQuartileIndex = (nodeDepths.size() - 1) / 4.0;
    double medianIndex = (nodeDepths.size() - 1) / 2.0;
    double thirdQuartileIndex = (nodeDepths.size() - 1) * 3.0 / 4.0;

    m_firstQuartileDepth = getValueUsingFractionalIndex(nodeDepths, firstQuartileIndex);
    m_medianDepth = getValueUsingFractionalIndex(nodeDepths, medianIndex);
    m_thirdQuartileDepth = getValueUsingFractionalIndex(nodeDepths, thirdQuartileIndex);

    //Set the auto node length setting. This is determined by aiming for a
    //target average node length. But if the graph is small, the value will be
    //increased (to avoid having an overly small and simple graph layout).
    double targetDrawnGraphLength = std::max(m_nodeCount * g_settings->meanNodeLength,
                                             g_settings->minTotalGraphLength);
    double megabases = totalLength / 1000000.0;
    if (megabases > 0.0)
        g_settings->autoNodeLengthPerMegabase = targetDrawnGraphLength / megabases;
    else
        g_settings->autoNodeLengthPerMegabase = 10000.0;
}

void AssemblyGraph::clearGraphInfo()
{
    m_nodeCount = 0;
    m_edgeCount = 0;
    m_totalLength = 0;
    m_shortestContig = 0;
    m_longestContig = 0;

    m_meanDepth = 0.0;
    m_firstQuartileDepth = 0.0;
    m_medianDepth = 0.0;
    m_thirdQuartileDepth = 0.0;
}

/* Load data from CSV and add to deBruijnGraphNodes
 *
 * @param filename  the full path of the file to be loaded
 * @param *columns  will contain the names of each column after loading data
 *                  (to add these to the GUI)
 * @param *errormsg if not empty, message to be displayed to user containing warning
 *                  or other information
 * @returns         true/false if loading data worked
 */
bool AssemblyGraph::loadCSV(const QString &filename, QStringList *columns, QString *errormsg, bool *coloursLoaded) {
    clearAllCsvData();

    QFile inputFile(filename);
    if (!inputFile.open(QIODevice::ReadOnly)) {
        *errormsg = "Unable to read from specified file.";
        return false;
    }
    QTextStream in(&inputFile);
    QString line = in.readLine();

    // guess at separator; this assumes that any tab in the first line means
    // we have a tab separated file
    QString sep = "\t";
    if (line.split(sep).length() == 1) {
        sep = ",";
        if (line.split(sep).length() == 1) {
            *errormsg = "Neither tab nor comma in first line. Please check file format.";
            return false;
        }
    }

    unsigned unmatchedNodes = 0; // keep a counter for lines in file that can't be matched to nodes

    QStringList headers = utils::splitCsv(line, sep);
    if (headers.size() < 2) {
        *errormsg = "Not enough CSV headers: at least two required.";
        return false;
    }
    headers.pop_front();

    // Check to see if any of the columns holds colour data.
    int colourCol = -1;
    for (size_t i = 0; i < headers.size(); ++i) {
        QString header = headers[i].toLower();
        if (header == "colour" || header == "color") {
            colourCol = i;
            *coloursLoaded = true;
            break;
        }
    }

    *columns = m_csvHeaders = headers;
    size_t columnCount = headers.size();

    QMap<QString, QColor> colourCategories;
    std::vector<QColor> presetColours = getPresetColours();
    while (!in.atEnd()) {
        QApplication::processEvents();

        QStringList cols = utils::splitCsv(in.readLine(), sep);
        QString nodeName(cols[0]);

        std::vector<OrientedNode> nodes;
        // See if this is a path name
        // Match using unique prefix of path name. This allows us to load segmented SPAdes
        // scaffold paths (e.g. NODE_1_foo_1) and assign CSV data to all of them
        for (auto range = m_deBruijnGraphPaths.equal_prefix_range(nodeName.toStdString());
             range.first != range.second; ++range.first) {
            for (OrientedNode node : (*range.first).nodes()) {
                nodes.emplace_back(node);
                if (!g_settings->doubleMode)
                    nodes.emplace_back(node.reverse());
            }
        }

        // Just node name. The row names one orientation.
        if (nodes.empty()) {
            OrientedNode node = orientedNode(getNodeNameFromString(nodeName).toStdString());
            if (node)
                nodes.emplace_back(node);
        }

        if (nodes.empty()) {
            unmatchedNodes += 1;
            continue;
        }

        // Get rid of the node name - no need to save that.
        cols.pop_front();

        // Get rid of any extra data that doesn't have a header.
        cols.resize(columnCount);

        for (OrientedNode node: nodes)
            setCsvData(node, cols);

        // If one of the columns holds colour data, get the colour from that one.
        // Acceptable colour formats: 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a
        // standard colour name (e.g. skyblue).
        // If the colour value is something other than one of these, a colour will be assigned to the value.  That way
        // categorical names can be used and automatically given colours.
        if (colourCol == -1 || cols.size() <= colourCol)
            continue;

        QString colourString = cols[colourCol];
        if (colourString.isEmpty())
            continue;

        QColor colour(colourString);
        if (!colour.isValid()) {
            auto existing = colourCategories.find(colourString);
            if (existing == colourCategories.end()) {
                QColor presetColor = presetColours[colourCategories.size() % presetColours.size()];
                existing = colourCategories.insert(colourString, presetColor);
            }
            colour = existing.value();
        }

        for (OrientedNode node: nodes)
            setCustomColour(node, colour);
    }

    if (unmatchedNodes)
        *errormsg = "There were " + QString::number(unmatchedNodes) + " unmatched entries in the CSV.";

    return true;
}


//This function extracts a node name from a string.
//The string may be in this Bandage format:
//        NODE_6+_length_50434_cov_42.3615
//Or in a number of variations of that format.
//If the node name it finds does not end in a '+' or '-', it will add '+'.
QString AssemblyGraph::getNodeNameFromString(QString string) const
{
    // First check for the most obvious case, where the string is already a node name.
    if (m_deBruijnGraphNodes.count(string.toStdString()))
        return string;
    if (m_deBruijnGraphNodes.count((string + "+").toStdString()))
        return string + "+";

    QStringList parts = string.split("_");
    if (parts.empty())
        return "";

    if (parts[0] == "NODE")
        parts.pop_front();
    if (parts.empty())
        return "";

    QString nodeName;

    //This checks for the standard Bandage format where the node name does
    //not have any underscores.
    if (parts.size() == 5 && parts[1] == "length")
        nodeName = parts[0];

    //This checks for the simple case of nothing but a node name.
    else if (parts.size() == 1)
        nodeName = parts[0];

    //If the code got here, then it is likely that the node name contains
    //underscores.  Grab everything in the string up until we encounter
    //"length".
    else
    {
        for (auto & part : parts)
        {
            if (part == "length")
                break;
            if (nodeName.length() > 0)
                nodeName += "_";
            nodeName += part;
        }
    }

    int nameLength = nodeName.length();
    if (nameLength == 0)
        return "";

    QChar lastChar = nodeName.at(nameLength - 1);
    if (lastChar == '+' || lastChar == '-')
        return nodeName;
    else
        return nodeName + "+";
}

static void releaseGraphUsers() {
    g_memory->clearGraphSpecificMemory();
    g_settings->nodeColorer->reset();
}

bandage::Expected<GraphLoadInfo> AssemblyGraph::loadGraphFromFile(const QString& filename) {
    cleanUp();

    auto builder = io::AssemblyGraphBuilder::get(filename);
    if (!builder) {
        releaseGraphUsers();
        return bandage::createStringError("graph format not recognised: " + filename.toStdString());
    }

    builder->treatJumpsAsLinks(g_settings->jumpsAsLinks);
    if (auto E = builder->build(*this)) {
        cleanUp();
        releaseGraphUsers();
        return std::move(E);
    }

    determineGraphInfo();

    // FIXME: get rid of this!
    releaseGraphUsers();

    return GraphLoadInfo{builder->hasComplexOverlaps()};
}


//The startingNodes and nodeDistance parameters are only used if the graph scope
//is not WHOLE_GRAPH.
void AssemblyGraph::markNodesToDraw(const graph::Scope &scope,
                                    const std::vector<OrientedNode> &startingNodes) {
    if (scope.graphScope() == WHOLE_GRAPH) {
        for (OrientedNode positive : canonical_nodes()) {
            // Single mode draws the positive orientation. Double mode draws both.
            positive.setAsDrawn();
            if (g_settings->doubleMode && !positive.isSelfComplement())
                positive.reverse().setAsDrawn();
        }
    } else if (scope.graphScope() == AROUND_COMPONENT) {
        auto nodeToDraw = [](OrientedNode node) {
            if (!g_settings->doubleMode && node.negative())
                return node.reverse();
            return node;
        };

        std::unordered_set<OrientedNode> seen;
        std::vector<OrientedNode> queue;
        queue.reserve(startingNodes.size() * 2);

        for (OrientedNode node : startingNodes) {
            OrientedNode drawn = nodeToDraw(node);
            drawn.setAsDrawn();
            drawn.setAsSpecial();

            auto enqueue = [&](OrientedNode n) {
                if (n && seen.insert(n).second)
                    queue.push_back(n);
            };
            enqueue(node);
            enqueue(node.reverse());
        }

        while (!queue.empty()) {
            OrientedNode node = queue.back();
            queue.pop_back();
            nodeToDraw(node).setAsDrawn();

            for (auto link : incidentEdges(node)) {
                OrientedNode other = link.otherNode();
                if (!other)
                    continue;
                if (seen.insert(other).second)
                    queue.push_back(other);
                if (seen.insert(other.reverse()).second)
                    queue.push_back(other.reverse());
            }
        }
    } else {
        for (OrientedNode node : startingNodes) {
            // Single mode draws the positive orientation.
            if (!g_settings->doubleMode && node.negative())
                node = node.reverse();

            node.setAsDrawn();
            node.setAsSpecial();
            node.labelNeighbouringNodesAsDrawn(scope.distance());
        }
    }

    // Then loop through each edge determining its drawn status
    for (DeBruijnEdge *edge : m_deBruijnGraphEdges)
        edge->determineIfDrawn();
}

static QStringList removeNullStringsFromList(const QStringList& in) {
    QStringList out;

    for (const auto& string : in) {
        if (string.length() > 0)
            out.push_back(string);
    }
    return out;
}

bool AssemblyGraph::checkIfStringHasNodes(QString nodesString) {
    nodesString = nodesString.simplified();
    QStringList nodesList = nodesString.split(",");
    nodesList = removeNullStringsFromList(nodesList);
    return (nodesList.empty());
}

std::string AssemblyGraph::generateNodesNotFoundErrorMessage(const std::vector<QString> &nodesNotInGraph, bool exact) {
    std::string errorMessage = exact ? "The following nodes are not in the graph:\n"
                                      : "The following queries do not match any nodes in the graph:\n";

    for (size_t i = 0; i < nodesNotInGraph.size(); ++i) {
        if (i != 0)
            errorMessage += ", ";
        errorMessage += nodesNotInGraph[i].toStdString();
    }
    errorMessage += "\n";

    return errorMessage;
}


std::vector<OrientedNode>
AssemblyGraph::getNodesFromStringList(QString nodeNamesString, bool exactMatch,
                                      std::vector<QString> *nodesNotInGraph) const {
    nodeNamesString = nodeNamesString.simplified();
    QStringList nodesList = nodeNamesString.split(",");

    if (exactMatch)
        return getNodesFromListExact(nodesList, nodesNotInGraph);
    else
        return getNodesFromListPartial(nodesList, nodesNotInGraph);
}

std::pair<OrientedNode, OrientedNode> AssemblyGraph::getNodes(const QString& nodeName) const {
    // If the node name ends in +/-, then we assume the user was specifying the exact
    // orientation.  If the node name does not end in +/-, then we assume the
    // user is asking for either orientation.
    QChar lastChar = nodeName.back();
    if (lastChar == '+' || lastChar == '-')
        return {orientedNode(nodeName.toStdString()), {}};

    return {orientedNode((nodeName + "+").toStdString()),
            orientedNode((nodeName + "-").toStdString())};
}


//Given a list of node names (as strings), this function will return all nodes which match
//those names exactly.  The last +/- on the end of the node name is optional - if missing
//both + and - nodes will be returned.
std::vector<OrientedNode> AssemblyGraph::getNodesFromListExact(const QStringList& nodesList,
                                                             std::vector<QString> *nodesNotInGraph) const {
    std::vector<OrientedNode> result;
    for (const auto &entry : nodesList) {
        QString nodeName = entry.simplified();
        if (nodeName.isEmpty())
            continue;

        auto [posNode, negNode] = getNodes(nodeName);
        if (!posNode && !negNode && nodesNotInGraph)
            nodesNotInGraph->push_back(nodeName);

        if (posNode)
            result.push_back(posNode);
        if (negNode)
            result.push_back(negNode);
    }

    return result;
}

std::vector<OrientedNode> AssemblyGraph::getNodesFromListPartial(const QStringList& nodesList,
                                                               std::vector<QString> *nodesNotInGraph) const {
    std::vector<OrientedNode> result;

    for (const auto &name : nodesList) {
        QString queryName = name.simplified();
        if (queryName.isEmpty())
            continue;

        bool found = false;
        for (OrientedNode node : oriented_nodes()) {
            if (!nodeDisplayName(node).contains(queryName))
                continue;
            found = true;
            result.push_back(node);
        }

        if (!found && nodesNotInGraph)
            nodesNotInGraph->push_back(queryName);
    }

    return result;
}

std::vector<OrientedNode> AssemblyGraph::getNodesInDepthRange(double min, double max) const {
    std::vector<OrientedNode> returnVector;

    for (OrientedNode node : oriented_nodes()) {
        if (!node.node()->isInDepthRange(min, max))
            continue;
        returnVector.push_back(node);
    }
    return returnVector;
}

void AssemblyGraph::setAllEdgesExactOverlap(int overlap) {
    for (DeBruijnEdge *edge : m_deBruijnGraphEdges) {
        edge->setExactOverlap(overlap);
    }
}



void AssemblyGraph::autoDetermineAllEdgesExactOverlap()
{
    int edgeCount = int(m_deBruijnGraphEdges.size());
    if (edgeCount == 0)
        return;

    //Determine the overlap for each edge.
    for (DeBruijnEdge *edge : m_deBruijnGraphEdges) {
        edge->autoDetermineExactOverlap();
    }

    //The expectation here is that most overlaps will be
    //the same or from a small subset of possible sizes.
    //Edges with an overlap that do not match the most common
    //overlap(s) are suspected of having their overlap
    //misidentified.  They are therefore rechecked using the
    //common ones.
    std::vector<int> overlapCounts = makeOverlapCountVector();

    //Sort the overlaps in order of decreasing numbers of edges.
    //I.e. the first overlap size in the vector will be the most
    //common overlap, the second will be the second most common,
    //etc.
    std::vector<int> sortedOverlaps;
    int overlapsSoFar = 0;
    double fractionOverlapsFound = 0.0;
    while (fractionOverlapsFound < 1.0)
    {
        int mostCommonOverlap = 0;
        int mostCommonOverlapCount = 0;

        //Find the overlap size with the most instances.
        for (size_t i = 0; i < overlapCounts.size(); ++i)
        {
            if (overlapCounts[i] > mostCommonOverlapCount)
            {
                mostCommonOverlap = int(i);
                mostCommonOverlapCount = overlapCounts[i];
            }
        }

        //Add that overlap to the common collection and remove it from the counts.
        sortedOverlaps.push_back(mostCommonOverlap);
        overlapsSoFar += mostCommonOverlapCount;
        fractionOverlapsFound = double(overlapsSoFar) / edgeCount;
        overlapCounts[mostCommonOverlap] = 0;
    }

    //For each edge, see if one of the more common overlaps also works.
    //If so, use that instead.
    for (DeBruijnEdge *edge : m_deBruijnGraphEdges) {
        for (int sortedOverlap : sortedOverlaps)
        {
            if (edge->getOverlap() == sortedOverlap)
                break;
            else if (edge->testExactOverlap(sortedOverlap))
            {
                edge->setOverlap(sortedOverlap);
                break;
            }
        }
    }
}


//This function produces a vector for which the values are the number
//of edges that have an overlap of the index length.
//E.g. if overlapVector[61] = 123, that means that 123 edges have an
//overlap of 61.
std::vector<int> AssemblyGraph::makeOverlapCountVector()
{
    std::vector<int> overlapCounts;

    for (const DeBruijnEdge *edge : m_deBruijnGraphEdges) {
        int overlap = edge->getOverlap();

        //Add the overlap to the count vector
        if (int(overlapCounts.size()) < overlap + 1)
            overlapCounts.resize(overlap + 1, 0);
        ++overlapCounts[overlap];
    }

    return overlapCounts;
}

std::string AssemblyGraph::getUniqueNodeName(std::string_view baseName) const {
    std::string base(baseName);
    if (!m_deBruijnGraphNodes.count(base + "+"))
        return base;

    int suffix = 1;
    while (true) {
        ++suffix;
        std::string potentialUniqueName = base + "_" + std::to_string(suffix);
        if (!m_deBruijnGraphNodes.count(potentialUniqueName + "+"))
            return potentialUniqueName;
    }

    assert(false && "unreachable");
    return {};
}


void AssemblyGraph::recalculateAllNodeWidths(double averageNodeWidth,
                                             double depthPower, double depthEffectOnWidth) {
    double meanDrawnDepth = getMeanDepth(true);

    for (OrientedNode positive : canonical_nodes()) {
        auto setWidth = [&](OrientedNode node) {
            if (GraphicsItemNode *graphicsItemNode = node.getGraphicsItemNode())
                graphicsItemNode->setWidth(meanDrawnDepth == 0 ? 1.0 : node.getDepth() / meanDrawnDepth,
                                           averageNodeWidth,
                                           depthPower, depthEffectOnWidth);
        };
        setWidth(positive);
        if (!positive.isSelfComplement())
            setWidth(positive.reverse());
    }
}


int AssemblyGraph::getDrawnNodeCount() const {
    int nodeCount = 0;
    for (OrientedNode positive : canonical_nodes()) {
        nodeCount += positive.isDrawn();
        // reverse() of a palindrome is that same orientation, so it is counted twice.
        nodeCount += positive.reverse().isDrawn();
    }

    return nodeCount;
}


void AssemblyGraph::eraseNodeRecords(const DeBruijnNode *node) {
    OrientedNode positive(const_cast<DeBruijnNode *>(node), false);
    m_nodeColors.erase(positive);
    m_nodeLabels.erase(positive);
    m_nodeCSVData.erase(positive);
    if (!node->isSelfComplement()) {
        OrientedNode negative = positive.reverse();
        m_nodeColors.erase(negative);
        m_nodeLabels.erase(negative);
        m_nodeCSVData.erase(negative);
    }
    m_nodeTags.erase(node);
    m_negativeNodeGraphics.erase(node);
}

void AssemblyGraph::eraseEdgeRecords(const DeBruijnEdge *edge) {
    m_edgeColors.erase(edge);
    m_edgeReverseColors.erase(edge);
    m_edgeStyles.erase(edge);
    m_edgeTags.erase(edge);
    m_reverseEdgeGraphics.erase(edge);
    m_overlapKeys.erase(edge);
}

void AssemblyGraph::deleteNodes(const std::vector<OrientedNode> &nodes)
{
    phmap::flat_hash_set<DeBruijnNode *> selected;
    for (OrientedNode node : nodes) {
        if (!node)
            continue;
        selected.insert(node.node());
    }

    // One edge object is stored on both endpoint nodes.
    phmap::flat_hash_set<DeBruijnEdge *> edgesToDelete;
    for (DeBruijnNode *node : selected)
        for (DeBruijnEdge *edge : node->edges())
            edgesToDelete.insert(edge);

    deleteEdgeSet(edgesToDelete);

    for (DeBruijnNode *node : selected) {
        eraseNodeRecords(node);
        const std::string base(node->getNameWithoutSign());
        m_deBruijnGraphNodes.erase(base + "+");
        m_deBruijnGraphNodes.erase(base + "-");
        delete node;
    }
}

void AssemblyGraph::rekeySelfComplementRecords(DeBruijnNode *node) {
    if (node == nullptr || node->isSelfComplement())
        return;

    OrientedNode negative(node, true);
    OrientedNode positive(node, false);
    auto lift = [&](auto &map) {
        auto it = map.find(negative);
        if (it == map.end())
            return;
        auto value = std::move(it->second);
        map.erase(negative);
        if (map.find(positive) == map.end())
            map.emplace(positive, std::move(value));
    };
    lift(m_nodeColors);
    lift(m_nodeLabels);
    lift(m_nodeCSVData);

    if (!g_annotationsManager)
        return;
    for (const auto &group : g_annotationsManager->getGroups())
        lift(group->annotationMap);
}

static void swapEdgeDirectionColours(AssemblyGraph &graph, const DeBruijnEdge *edge) {
    auto forward = graph.m_edgeColors.find(edge);
    auto reverse = graph.m_edgeReverseColors.find(edge);
    const bool hasForward = forward != graph.m_edgeColors.end();
    const bool hasReverse = reverse != graph.m_edgeReverseColors.end();
    QColor forwardColour = hasForward ? forward->second : QColor();
    QColor reverseColour = hasReverse ? reverse->second : QColor();
    if (hasForward)
        graph.m_edgeColors.erase(edge);
    if (hasReverse)
        graph.m_edgeReverseColors.erase(edge);
    if (hasReverse)
        graph.m_edgeColors.emplace(edge, reverseColour);
    if (hasForward)
        graph.m_edgeReverseColors.emplace(edge, forwardColour);
}

static void flipStoredEdgeDirection(AssemblyGraph &graph, DeBruijnEdge *edge) {
    OrientedNode start = edge->getStartingNode();
    OrientedNode end = edge->getEndingNode();
    edge->setStartingNode(end.reverse());
    edge->setEndingNode(start.reverse());
    swapEdgeDirectionColours(graph, edge);
}

static void copyEdgeRecordsIfEmpty(AssemblyGraph &graph, DeBruijnEdge *kept, const DeBruijnEdge *dropped) {
    if (!graph.hasCustomColour(kept, false) && graph.hasCustomColour(dropped, false))
        graph.setCustomColour(kept, graph.getCustomColour(dropped, false), false);
    if (!graph.hasCustomColour(kept, true) && graph.hasCustomColour(dropped, true))
        graph.setCustomColour(kept, graph.getCustomColour(dropped, true), true);
    if (!graph.hasCustomStyle(kept) && graph.hasCustomStyle(dropped))
        graph.setCustomStyle(kept, graph.getCustomStyle(dropped));
    if (graph.m_edgeTags.find(kept) == graph.m_edgeTags.end()) {
        auto tags = graph.m_edgeTags.find(dropped);
        if (tags != graph.m_edgeTags.end()) {
            // Copy before emplace. The source vector lives in this map, and
            // inserting the new key can rehash it.
            auto copied = tags->second;
            graph.m_edgeTags.emplace(kept, std::move(copied));
        }
    }
}

void AssemblyGraph::collapseSelfComplementEdges(DeBruijnNode *node) {
    if (node == nullptr || !node->isSelfComplement())
        return;

    OrientedNode canonical(node, false);
    std::vector<DeBruijnEdge *> incident(node->edgeBegin(), node->edgeEnd());
    for (DeBruijnEdge *edge : incident) {
        if (edge->getStartingNode().node() == node)
            edge->setStartingNode(canonical);
        if (edge->getEndingNode().node() == node)
            edge->setEndingNode(canonical);
    }
    for (DeBruijnEdge *edge : incident) {
        if (!edge->isOwnReverseComplement() && !edge->isPositiveEdge())
            flipStoredEdgeDirection(*this, edge);
    }

    auto overlapKey = [this](const DeBruijnEdge *edge) {
        auto keyIt = m_overlapKeys.find(edge);
        return keyIt == m_overlapKeys.end() ? std::string() : keyIt->second;
    };
    auto sameLink = [&](const DeBruijnEdge *edge, const DeBruijnEdge *other) {
        return edge->connects(other->getStartingNode(), other->getEndingNode()) &&
               edge->getOverlap() == other->getOverlap() &&
               edge->getOverlapType() == other->getOverlapType() &&
               overlapKey(edge) == overlapKey(other);
    };

    std::vector<std::pair<DeBruijnEdge *, DeBruijnEdge *>> replaced;
    phmap::flat_hash_set<DeBruijnEdge *> drop;
    for (size_t i = 0; i < incident.size(); ++i) {
        if (drop.contains(incident[i]))
            continue;
        for (size_t j = i + 1; j < incident.size(); ++j) {
            if (drop.contains(incident[j]))
                continue;
            if (!sameLink(incident[i], incident[j]))
                continue;
            copyEdgeRecordsIfEmpty(*this, incident[i], incident[j]);
            replaced.emplace_back(incident[j], incident[i]);
            drop.insert(incident[j]);
        }
    }
    for (auto it = m_deBruijnGraphPaths.begin(); it != m_deBruijnGraphPaths.end(); ++it) {
        for (const auto &pair : replaced)
            it.value().replaceEdge(pair.first, pair.second);
    }
    for (auto it = m_deBruijnGraphWalks.begin(); it != m_deBruijnGraphWalks.end(); ++it) {
        for (const auto &pair : replaced)
            it.value().walk.replaceEdge(pair.first, pair.second);
    }
    if (!drop.empty())
        deleteEdgeSet(drop);
}

void AssemblyGraph::deleteEdges(const std::vector<DeBruijnEdge *> &edges)
{
    deleteEdgeSet(phmap::flat_hash_set<DeBruijnEdge *>(edges.begin(), edges.end()));
}

void AssemblyGraph::deleteEdgeSet(const phmap::flat_hash_set<DeBruijnEdge *> &edges)
{
    for (DeBruijnEdge *edge : edges) {
        OrientedNode startingNode = edge->getStartingNode();
        OrientedNode endingNode = edge->getEndingNode();

        eraseEdgeRecords(edge);
        m_deBruijnGraphEdges.erase(edge);
        startingNode.node()->removeEdge(edge);
        if (endingNode.node() != startingNode.node())
            endingNode.node()->removeEdge(edge);

        delete edge;
    }
}

struct DirectedLink {
    std::string otherName;
    int overlap = 0;
    EdgeOverlapType type = UNKNOWN_OVERLAP;

    DirectedLink(std::string otherName, int overlap, EdgeOverlapType type)
            : otherName(std::move(otherName)), overlap(overlap), type(type) {}
};

// Copy one direction's links before createDeBruijnEdge. That call attaches
// the new edge to every end it touches. On a self-loop or hairpin one of
// those ends is the node being iterated, so its edge list grows under the
// incident iterator. The iterator holds pointers into the list, and the list
// moves when it outgrows its inline buffer.
//
// Outgoing and incoming are copied separately. Outgoing edges are created
// first, and one of them can be an incoming edge of the node copied next. A
// circular merge depends on that: the link from the new node back to the front
// is in the incoming copy, and creating it leaves a loop on the merged node.
template <typename Range>
static std::vector<DirectedLink> snapshotLinks(Range links) {
    std::vector<DirectedLink> copied;
    for (auto link : links)
        copied.emplace_back(link.otherNode().getName(), link.overlap(), link.overlapType());
    return copied;
}

//This function assumes it is receiving a positive node.  It will duplicate both
//the positive and negative node in the pair.  It divided their depth in
//two, giving half to each node.
void AssemblyGraph::duplicateNodePair(OrientedNode node, BandageGraphicsScene *scene)
{
    OrientedNode original = node.getCanonical();
    OrientedNode originalReverse = original.reverse();

    std::string newNodeBaseName = getNewNodeName(original.getName());
    std::string newPosNodeName = newNodeBaseName + "+";
    std::string newNegNodeName = newNodeBaseName + "-";

    double newDepth = original.getDepth() / 2.0;

    auto *createdNode = new DeBruijnNode(newNodeBaseName, float(newDepth), original.node()->getSequence());
    OrientedNode created(createdNode, false);
    OrientedNode createdReverse = created.reverse();

    setCustomColour(created, getCustomColour(original));
    setCustomLabel(created, getCustomLabel(original));
    setCsvData(created, getAllCsvData(original));
    if (!createdNode->isSelfComplement()) {
        setCustomColour(createdReverse, getCustomColour(originalReverse));
        setCustomLabel(createdReverse, getCustomLabel(originalReverse));
        setCsvData(createdReverse, getAllCsvData(originalReverse));
    }

    m_deBruijnGraphNodes.emplace(newPosNodeName, createdNode);
    m_deBruijnGraphNodes.emplace(newNegNodeName, createdNode);

    // Copy each direction before creating its edges. Incoming is second, so a
    // self-loop's new edge is part of that copy. See snapshotLinks.
    for (const auto &link : snapshotLinks(outgoingEdges(original)))
        createDeBruijnEdge(newPosNodeName, link.otherName, link.overlap, link.type);

    for (const auto &link : snapshotLinks(incomingEdges(original)))
        createDeBruijnEdge(link.otherName, newPosNodeName, link.overlap, link.type);

    original.setDepth(newDepth);

    scene->duplicateGraphicsNode(original, created, *this);
    if (!createdNode->isSelfComplement())
        scene->duplicateGraphicsNode(originalReverse, createdReverse, *this);
}

std::string AssemblyGraph::getNewNodeName(std::string_view oldNodeName) const
{
    if (!oldNodeName.empty() && (oldNodeName.back() == '+' || oldNodeName.back() == '-'))
        oldNodeName.remove_suffix(1);

    std::string newNodeNameBase(oldNodeName);
    newNodeNameBase += "_copy";
    std::string newNodeName = newNodeNameBase;

    int suffix = 1;
    while (m_deBruijnGraphNodes.count(newNodeName + "+"))
    {
        ++suffix;
        newNodeName = newNodeNameBase + std::to_string(suffix);
    }

    return newNodeName;
}

static bool canAddNodeToStartOfMergeList(OrientedNode firstNode,
                                         OrientedNode potentialNode) {
    auto entering = incomingEdges(firstNode);
    auto leaving = outgoingEdges(potentialNode);
    if (!bandage::hasSingleElement(entering) ||
        !bandage::hasSingleElement(leaving))
        return false;
    return entering.begin()->otherNode() == potentialNode &&
           leaving.begin()->otherNode() == firstNode;
}


static bool canAddNodeToEndOfMergeList(OrientedNode lastNode,
                                       OrientedNode potentialNode) {
    auto leaving = outgoingEdges(lastNode);
    auto entering = incomingEdges(potentialNode);
    if (!bandage::hasSingleElement(leaving) ||
        !bandage::hasSingleElement(entering))
        return false;
    return leaving.begin()->otherNode() == potentialNode &&
           entering.begin()->otherNode() == lastNode;
}

static void mergeGraphicsNodes(const std::vector<OrientedNode> &originalNodes,
                               const std::vector<OrientedNode> &revCompOriginalNodes,
                               OrientedNode newNode,
                               AssemblyGraph &graph,
                               BandageGraphicsScene *scene);

//This function will merge the given nodes, if possible.  Nodes can only be
//merged if they are in a simple, unbranching path with no extra edges.  If the
//merge is successful, it returns true, otherwise false.
bool AssemblyGraph::mergeNodes(QList<OrientedNode> nodes, BandageGraphicsScene *scene) {
    if (nodes.empty())
        return true;

    //We now need to sort the nodes into merge order.
    std::deque<OrientedNode> mergeList;
    mergeList.push_back(nodes[0]);
    nodes.pop_front();

    bool addedNode;
    do {
        addedNode = false;
        for (int i = 0; i < nodes.size(); ++i) {
            OrientedNode potentialNextNode = nodes[i];

            //Check if the node can be added to the end of the list.
            if (canAddNodeToEndOfMergeList(mergeList.back(), potentialNextNode)) {
                mergeList.push_back(potentialNextNode);
                nodes.removeAt(i);
                addedNode = true;
                break;
            }

            //Check if the node can be added to the front of the list.
            if (canAddNodeToStartOfMergeList(mergeList.front(), potentialNextNode)) {
                mergeList.push_front(potentialNextNode);
                nodes.removeAt(i);
                addedNode = true;
                break;
            }

            //If neither of those worked, then we should try the node's reverse
            //complement.
            OrientedNode potentialNextNodeRevComp = potentialNextNode.reverse();
            if (canAddNodeToEndOfMergeList(mergeList.back(), potentialNextNodeRevComp)) {
                mergeList.push_back(potentialNextNodeRevComp);
                nodes.removeAt(i);
                addedNode = true;
                break;
            }
            if (canAddNodeToStartOfMergeList(mergeList.front(), potentialNextNodeRevComp)) {
                mergeList.push_front(potentialNextNodeRevComp);
                nodes.removeAt(i);
                addedNode = true;
                break;
            }
        }

        if (nodes.empty())
            break;
    } while (addedNode);

    //If there are still nodes left in the first list, then they don't form a
    //nice simple path and the merge won't work.
    if (!nodes.empty())
        return false;

    std::vector<OrientedNode> orderedList(mergeList.begin(), mergeList.end());
    Path posPath = Path::makeFromOrderedNodes(orderedList, false);
    Sequence mergedNodePosSequence{posPath.getPathSequence()};
    if (mergedNodePosSequence.size() > DeBruijnNode::MaxLength)
        return false;

    std::vector<OrientedNode> revCompOrderedList;
    for (auto it = orderedList.rbegin(); it != orderedList.rend(); ++it)
        revCompOrderedList.push_back(it->reverse());

    std::string newNodeBaseName;
    for (int i = 0; i < int(orderedList.size()); ++i) {
        newNodeBaseName += orderedList[i].getNameWithoutSign();
        if (i < int(orderedList.size()) - 1)
            newNodeBaseName += "_";
    }
    newNodeBaseName = getUniqueNodeName(newNodeBaseName);
    std::string newPosNodeName = newNodeBaseName + "+";
    std::string newNegNodeName = newNodeBaseName + "-";

    double mergedNodeDepth = getMeanDepth(orderedList);

    auto *mergedNode = new DeBruijnNode(newNodeBaseName, float(mergedNodeDepth), mergedNodePosSequence);
    OrientedNode newPosNode(mergedNode, false);

    m_deBruijnGraphNodes.emplace(newPosNodeName, mergedNode);
    m_deBruijnGraphNodes.emplace(newNegNodeName, mergedNode);

    // Copy each direction before creating its edges. Incoming is second, so the
    // edge from the new node back to the front closes the merged node. See snapshotLinks.
    for (const auto &link : snapshotLinks(outgoingEdges(orderedList.back())))
        createDeBruijnEdge(newPosNodeName, link.otherName, link.overlap, link.type);

    for (const auto &link : snapshotLinks(incomingEdges(orderedList.front())))
        createDeBruijnEdge(link.otherName, newPosNodeName, link.overlap, link.type);

    mergeGraphicsNodes(orderedList, revCompOrderedList, newPosNode,
                       *this, scene);

    deleteNodes(orderedList);

    recalculateAllNodeWidths(g_settings->averageNodeWidth,
                             g_settings->depthPower, g_settings->depthEffectOnWidth);

    return true;
}


static bool mergeGraphicsNodes2(const std::vector<OrientedNode> &originalNodes,
                                OrientedNode newNode,
                                AssemblyGraph &graph,
                                BandageGraphicsScene *scene) {
    bool success = true;
    std::vector<QPointF> linePoints;

    for (OrientedNode node : originalNodes) {
        // Single mode keeps the graphics item on the positive orientation.
        bool opposite = false;
        if (!g_settings->doubleMode && node.negative()) {
            node = node.reverse();
            opposite = true;
        }

        GraphicsItemNode *originalGraphicsItemNode = node.getGraphicsItemNode();
        if (originalGraphicsItemNode == nullptr) {
            success = false;
            break;
        }

        const auto& originalLinePoints = originalGraphicsItemNode->m_linePoints;

        // Add the original line points to the new line point collection. If we
        // are working with an opposite orientation, then reverse the order.
        if (opposite) {
            for (size_t j = originalLinePoints.size(); j > 0; --j)
                linePoints.push_back(originalLinePoints[j-1]);
        } else {
            for (auto originalLinePoint : originalLinePoints)
                linePoints.push_back(originalLinePoint);
        }
    }

    if (success) {
        // We pass dummy width here, as node widths will be recalculated later
        auto *newGraphicsItemNode = new GraphicsItemNode(newNode, 0, linePoints);

        newNode.setGraphicsItemNode(newGraphicsItemNode);
        newGraphicsItemNode->setFlag(QGraphicsItem::ItemIsSelectable);
        newGraphicsItemNode->setFlag(QGraphicsItem::ItemIsMovable);
        newGraphicsItemNode->setNodeColour(g_settings->nodeColorer->get(newGraphicsItemNode));

        scene->addItem(newGraphicsItemNode);

        for (DeBruijnEdge *newEdge : newNode.edges())
            scene->addIncidentEdgeGraphics(newEdge, graph);
    }
    return success;
}

static void mergeGraphicsNodes(const std::vector<OrientedNode> &originalNodes,
                               const std::vector<OrientedNode> &revCompOriginalNodes,
                               OrientedNode newNode,
                               AssemblyGraph &graph,
                               BandageGraphicsScene *scene) {
    bool success = mergeGraphicsNodes2(originalNodes, newNode, graph, scene);
    if (success)
        newNode.setAsDrawn();

    OrientedNode newRevComp = newNode.reverse();
    if (g_settings->doubleMode && !newNode.isSelfComplement()) {
        bool revCompSuccess = mergeGraphicsNodes2(revCompOriginalNodes, newRevComp, graph, scene);
        if (revCompSuccess)
            newRevComp.setAsDrawn();
    }

    BandageGraphicsScene::removeGraphicsItemNodes(originalNodes, true);
}

//This function simplifies the graph by merging all possible nodes in a simple
//line.  It returns the number of merges that it did.
//It gets a pointer to the progress dialog as well so it can check to see if the
//user has cancelled the merge.
int AssemblyGraph::mergeAllPossible(BandageGraphicsScene *scene,
                                    MyProgressDialog *progressDialog)
{
    //Create a set of all nodes.
    QSet<OrientedNode> uncheckedNodes;
    QList<OrientedNode> orientations;
    for (OrientedNode node : oriented_nodes()) {
        uncheckedNodes.insert(node);
        orientations.push_back(node);
    }

    //Create a list of all merges to be done.
    QList<QList<OrientedNode>> allMerges;
    for (OrientedNode node : orientations) {

        //If the current node isn't checked, then we will find the longest
        //possible mergable sequence containing this node.
        if (uncheckedNodes.contains(node))
        {
            QList<OrientedNode> nodesToMerge;
            nodesToMerge.push_back(node);

            uncheckedNodes.remove(node);
            uncheckedNodes.remove(node.reverse());

            //Extend forward as much as possible.
            bool extended;
            do
            {
                extended = false;
                OrientedNode last = nodesToMerge.back();
                auto outgoing = outgoingEdges(last);
                if (bandage::hasSingleElement(outgoing))
                {
                    auto potentialEdge = *outgoing.begin();
                    OrientedNode potentialNode = potentialEdge.otherNode();
                    auto edgesEnteringPotentialNode = incomingEdges(potentialNode);
                    if (bandage::hasSingleElement(edgesEnteringPotentialNode) &&
                            edgesEnteringPotentialNode.begin()->edge() == potentialEdge.edge() &&
                            !nodesToMerge.contains(potentialNode) &&
                            uncheckedNodes.contains(potentialNode))
                    {
                        nodesToMerge.push_back(potentialNode);
                        uncheckedNodes.remove(potentialNode);
                        uncheckedNodes.remove(potentialNode.reverse());
                        extended = true;
                    }
                }
            } while (extended);

            //Extend backward as much as possible.
            do
            {
                extended = false;
                OrientedNode first = nodesToMerge.front();
                auto incoming = incomingEdges(first);
                if (bandage::hasSingleElement(incoming))
                {
                    auto potentialEdge = *incoming.begin();
                    OrientedNode potentialNode = potentialEdge.otherNode();
                    auto edgesLeavingPotentialNode = outgoingEdges(potentialNode);
                    if (bandage::hasSingleElement(edgesLeavingPotentialNode) &&
                            edgesLeavingPotentialNode.begin()->edge() == potentialEdge.edge() &&
                            !nodesToMerge.contains(potentialNode) &&
                            uncheckedNodes.contains(potentialNode))
                    {
                        nodesToMerge.push_front(potentialNode);
                        uncheckedNodes.remove(potentialNode);
                        uncheckedNodes.remove(potentialNode.reverse());
                        extended = true;
                    }
                }
            } while (extended);

            if (nodesToMerge.size() > 1)
                allMerges.push_back(nodesToMerge);
        }
    }

    //Now do the actual merges.
    QApplication::processEvents();
    emit setMergeTotalCount(allMerges.size());
    for (int i = 0; i < allMerges.size(); ++i)
    {
        if (progressDialog != nullptr && progressDialog->wasCancelled())
            break;

        mergeNodes(allMerges[i], scene);
        emit setMergeCompletedCount(i+1);
        QApplication::processEvents();
    }

    recalculateAllNodeWidths(g_settings->averageNodeWidth,
                             g_settings->depthPower, g_settings->depthEffectOnWidth);

    return allMerges.size();
}

bool AssemblyGraph::hasCustomColour(OrientedNode node) const {
    auto it = m_nodeColors.find(node);
    return it != m_nodeColors.end() && it->second.isValid();
}

bool AssemblyGraph::hasCustomColour(const DeBruijnEdge *edge, bool reverse) const {
    const auto &colors = reverse ? m_edgeReverseColors : m_edgeColors;
    auto it = colors.find(edge);
    return it != colors.end() && it->second.isValid();
}

bool AssemblyGraph::hasCustomStyle(const DeBruijnEdge *edge) const {
    return m_edgeStyles.contains(edge);
}

QColor AssemblyGraph::getCustomColour(OrientedNode node) const {
    auto it = m_nodeColors.find(node);
    return it == m_nodeColors.end() ? QColor() : it->second;
}

QColor AssemblyGraph::getCustomColour(const DeBruijnEdge *edge, bool reverse) const {
    const auto &colors = reverse ? m_edgeReverseColors : m_edgeColors;
    auto it = colors.find(edge);
    return it == colors.end() ? g_settings->edgeColour : it->second;
}

AssemblyGraph::EdgeStyle AssemblyGraph::getCustomStyle(const DeBruijnEdge *edge) const {
    auto it = m_edgeStyles.find(edge);
    return it == m_edgeStyles.end() ? AssemblyGraph::EdgeStyle() : it->second;
}

void AssemblyGraph::setCustomColour(OrientedNode node, QColor color) {
    m_nodeColors[node] = color;
}

void AssemblyGraph::setCustomColour(const DeBruijnEdge *edge, QColor color, bool reverse) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    if (reverse)
        m_edgeReverseColors[edge] = color;
    else
        m_edgeColors[edge] = color;
}

GraphicsItemEdge *AssemblyGraph::reverseGraphicsItem(const DeBruijnEdge *edge) const {
    auto it = m_reverseEdgeGraphics.find(edge);
    return it == m_reverseEdgeGraphics.end() ? nullptr : it->second;
}

void AssemblyGraph::setReverseGraphicsItem(const DeBruijnEdge *edge, GraphicsItemEdge *item) {
    if (item == nullptr)
        m_reverseEdgeGraphics.erase(edge);
    else
        m_reverseEdgeGraphics[edge] = item;
}

GraphicsItemEdge *AssemblyGraph::takeGraphicsItem(DeBruijnEdge *edge, bool reversed) {
    GraphicsItemEdge *item = reversed ? reverseGraphicsItem(edge) : edge->getGraphicsItemEdge();
    if (reversed)
        setReverseGraphicsItem(edge, nullptr);
    else
        edge->setGraphicsItemEdge(nullptr);
    return item;
}

void AssemblyGraph::setCustomStyle(const DeBruijnEdge *edge, Qt::PenStyle lineStyle) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    m_edgeStyles[edge].lineStyle = lineStyle;
}

AssemblyGraph::EdgeStyle::EdgeStyle()
 : width(0), lineStyle(Qt::SolidLine) {}

void AssemblyGraph::setCustomStyle(const DeBruijnEdge *edge, float width) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    m_edgeStyles[edge].width = width;
}

void AssemblyGraph::setCustomStyle(const DeBruijnEdge *edge, EdgeStyle style) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    m_edgeStyles[edge] = style;
}

QString AssemblyGraph::getCustomLabel(OrientedNode node) const {
    auto it = m_nodeLabels.find(node);
    return it == m_nodeLabels.end() ? QString() : it->second;
}

void AssemblyGraph::setCustomLabel(OrientedNode node, QString newLabel) {
    newLabel.replace("\t", "    ");
    m_nodeLabels[node] = newLabel;
}

void AssemblyGraph::clearAllCsvData() {
    m_csvHeaders.clear();
    m_nodeCSVData.clear();
}

bool AssemblyGraph::hasCsvData(OrientedNode node) const {
    auto it = m_nodeCSVData.find(node);
    return it != m_nodeCSVData.end() && !it->second.isEmpty();
}

QStringList AssemblyGraph::getAllCsvData(OrientedNode node) const {
    auto it = m_nodeCSVData.find(node);
    return it == m_nodeCSVData.end() ? QStringList() : it->second;
}

std::optional<QString> AssemblyGraph::getCsvLine(OrientedNode node, int i) const {
    auto it = m_nodeCSVData.find(node);
    if (it == m_nodeCSVData.end() ||
        i >= it->second.length())
        return "";

    return it->second[i];
}

void AssemblyGraph::setCsvData(OrientedNode node, QStringList csvData) {
    m_nodeCSVData[node] = std::move(csvData);
}

void AssemblyGraph::clearCsvData(OrientedNode node) {
    m_nodeCSVData[node].clear();
}

//This function changes the name of a node pair.  The new and old names are
//both assumed to not include the +/- at the end.
void AssemblyGraph::changeNodeName(const QString& oldName, const QString& newName)
{
    if (checkNodeNameValidity(newName) != NODE_NAME_OKAY)
        return;

    const std::string posOldNodeName = oldName.toStdString() + "+";
    const std::string negOldNodeName = oldName.toStdString() + "-";

    if (!m_deBruijnGraphNodes.count(posOldNodeName))
        return;
    if (!m_deBruijnGraphNodes.count(negOldNodeName))
        return;

    DeBruijnNode *node = m_deBruijnGraphNodes[posOldNodeName];

    m_deBruijnGraphNodes.erase(posOldNodeName);
    m_deBruijnGraphNodes.erase(negOldNodeName);

    const std::string unsignedName = newName.toStdString();
    node->setName(unsignedName);

    m_deBruijnGraphNodes.emplace(unsignedName + "+", node);
    m_deBruijnGraphNodes.emplace(unsignedName + "-", node);
}



//This function checks whether a new node name is okay.  It takes a node name
//without a +/- at the end.
NodeNameStatus AssemblyGraph::checkNodeNameValidity(const QString& nodeName) const
{
    if (nodeName.isEmpty())
        return NODE_NAME_EMPTY;

    if (nodeName.contains('\t'))
        return NODE_NAME_CONTAINS_TAB;

    if (nodeName.contains('\n'))
        return NODE_NAME_CONTAINS_NEWLINE;

    if (nodeName.contains(','))
        return NODE_NAME_CONTAINS_COMMA;

    if (nodeName.contains(' '))
        return NODE_NAME_CONTAINS_SPACE;

    if (nodeName.endsWith('+') || nodeName.endsWith('-'))
        return NODE_NAME_ENDS_WITH_SIGN;

    if (m_deBruijnGraphNodes.count((nodeName + "+").toStdString()))
        return NODE_NAME_TAKEN;

    return NODE_NAME_OKAY;
}



void AssemblyGraph::changeNodeDepth(const std::vector<OrientedNode> &nodes,
                                    double newDepth)
{
    if (nodes.empty())
        return;

    for (OrientedNode node : nodes)
        node.setDepth(newDepth);

    //If this graph does not already have a depthTag, give it a depthTag of KC
    //so the depth info will be saved.
    if (m_depthTag == "")
        m_depthTag = "KC";
}


//This function returns the number of dead ends in the graph.
//It looks only at positive nodes, which can have 0, 1 or 2 dead ends each.
//This value therefore varies between zero and twice the node count (specifically
//the positive node count).
unsigned AssemblyGraph::getDeadEndCount() const
{
    int deadEndCount = 0;

    for (OrientedNode positive : canonical_nodes()) {
        const int times = positive.isSelfComplement() ? 2 : 1;
        deadEndCount += times * positive.getDeadEndCount();
    }

    return deadEndCount;
}



void AssemblyGraph::getNodeStats(int *n50, int *shortestNode, int *firstQuartile, int *median, int *thirdQuartile, int *longestNode) const
{
    if (m_totalLength == 0.0)
        return;

    std::vector<int> nodeLengths;
    for (OrientedNode node : canonical_nodes()) {
        const int length = int(node.getLength());
        nodeLengths.insert(nodeLengths.end(), node.isSelfComplement() ? 2 : 1, length);
    }

    if (nodeLengths.empty())
        return;

    std::sort(nodeLengths.begin(), nodeLengths.end());

    *shortestNode = nodeLengths.front();
    *longestNode = nodeLengths.back();

    double firstQuartileIndex = (nodeLengths.size() - 1) / 4.0;
    double medianIndex = (nodeLengths.size() - 1) / 2.0;
    double thirdQuartileIndex = (nodeLengths.size() - 1) * 3.0 / 4.0;

    *firstQuartile = round(getValueUsingFractionalIndex(nodeLengths, firstQuartileIndex));
    *median = round(getValueUsingFractionalIndex(nodeLengths, medianIndex));
    *thirdQuartile = round(getValueUsingFractionalIndex(nodeLengths, thirdQuartileIndex));

    double halfTotalLength = m_totalLength / 2.0;
    long long totalSoFar = 0;
    for (int i = int(nodeLengths.size()) - 1; i >= 0 ; --i)
    {
        totalSoFar += nodeLengths[i];
        if (totalSoFar >= halfTotalLength)
        {
            *n50 = nodeLengths[i];
            break;
        }
    }
}



//This function uses an algorithm adapted from: http://math.hws.edu/eck/cs327_s04/chapter9.pdf
void AssemblyGraph::getGraphComponentCountAndLargestComponentSize(int *componentCount, int *largestComponentLength) const
{
    *componentCount = 0;
    *largestComponentLength = 0;

    QSet<OrientedNode> visitedNodes;
    QList<QList<OrientedNode>> connectedComponents;

    // Positive orientations. A palindrome's two keys are the same orientation.
    for (OrientedNode v : canonical_nodes()) {

        if (!visitedNodes.contains(v))
        {
            QList<OrientedNode> connectedComponent;

            QQueue<OrientedNode> q;
            q.enqueue(v);
            visitedNodes.insert(v);

            while (!q.isEmpty())
            {
                OrientedNode w = q.dequeue();
                connectedComponent.push_back(w);

                for (OrientedNode k : w.getAllConnectedPositiveNodes())
                {
                    if (!visitedNodes.contains(k))
                    {
                        visitedNodes.insert(k);
                        q.enqueue(k);
                    }
                }
            }

            connectedComponents.push_back(connectedComponent);
        }
    }

    //Now that the list of connected components is built, we look for the
    //largest one (as measured by total node length).
    *componentCount = connectedComponents.size();
    for (int i = 0; i < *componentCount; ++i)
    {
        int componentLength = 0;
        for (auto & j : connectedComponents[i])
            componentLength += j.getLength();

        if (componentLength > *largestComponentLength)
            *largestComponentLength = componentLength;
    }
}

bool compareNodeDepth(OrientedNode a, OrientedNode b) {return (a.getDepth() < b.getDepth());}

//This function takes a node list sorted by depth and a target index (in terms of
//the whole sequence length).  It returns the depth at that index.
static double findDepthAtIndex(QList<OrientedNode> * nodeList, long long targetIndex)
{
    long long lengthSoFar = 0;
    for (auto node : *nodeList)
    {
        lengthSoFar += node.getLength();
        long long currentIndex = lengthSoFar - 1;

        if (currentIndex >= targetIndex)
            return node.getDepth();
    }
    return 0.0;
}


double AssemblyGraph::getMedianDepthByBase() const
{
    if (m_totalLength == 0)
        return 0.0;

    //Make a list of all nodes.
    long long totalLength = 0;
    QList<OrientedNode> nodeList;
    for (OrientedNode positive : canonical_nodes()) {
        const int times = positive.isSelfComplement() ? 2 : 1;
        for (int i = 0; i < times; ++i)
            nodeList.push_back(positive);
        totalLength += positive.getLength() * times;
    }

    //If there is only one node, then its depth is the median.
    if (nodeList.size() == 1)
        return nodeList[0].getDepth();

    //Sort the node list from low to high depth.
    std::sort(nodeList.begin(), nodeList.end(), compareNodeDepth);

    if (totalLength % 2 == 0) //Even total length
    {
        long long medianIndex2 = totalLength / 2;
        long long medianIndex1 = medianIndex2 - 1;
        double depth1 = findDepthAtIndex(&nodeList, medianIndex1);
        double depth2 = findDepthAtIndex(&nodeList, medianIndex2);
        return (depth1 + depth2) / 2.0;
    }
    else //Odd total length
    {
        long long medianIndex = (totalLength - 1) / 2;
        return findDepthAtIndex(&nodeList, medianIndex);
    }
}


long long AssemblyGraph::getEstimatedSequenceLength(double medianDepthByBase) const
{
    long long estimatedSequenceLength = 0;
    if (medianDepthByBase == 0.0)
        return 0;

    for (OrientedNode positive : canonical_nodes()) {
        int nodeLength = int(positive.getLengthWithoutTrailingOverlap());
        double relativeDepth = positive.getDepth() / medianDepthByBase;
        int closestIntegerDepth = round(relativeDepth);
        int lengthAdjustedForDepth = nodeLength * closestIntegerDepth;
        estimatedSequenceLength += lengthAdjustedForDepth * (positive.isSelfComplement() ? 2 : 1);
    }

    return estimatedSequenceLength;
}



long long AssemblyGraph::getTotalLengthMinusEdgeOverlaps() const
{
    long long totalLength = 0;
    for (OrientedNode positive : canonical_nodes()) {
        int maxOverlap = 0;
        for (const DeBruijnEdge *edge : positive.edges())
            maxOverlap = std::max(maxOverlap, edge->getOverlap());
        totalLength += (static_cast<long long>(positive.getLength()) - maxOverlap) *
                       (positive.isSelfComplement() ? 2 : 1);
    }

    return totalLength;
}


QPair<int, int> AssemblyGraph::getOverlapRange() const
{
    int smallestOverlap = std::numeric_limits<int>::max();
    int largestOverlap = 0;
    for (const DeBruijnEdge *edge : m_deBruijnGraphEdges) {
        int overlap = edge->getOverlap();
        if (overlap < smallestOverlap)
            smallestOverlap = overlap;
        if (overlap > largestOverlap)
            largestOverlap = overlap;
    }
    if (smallestOverlap == std::numeric_limits<int>::max())
        smallestOverlap = 0;
    return { smallestOverlap, largestOverlap };
}

long long AssemblyGraph::getTotalLengthOrphanedNodes() const {
    long long total = 0;
    for (OrientedNode positive : canonical_nodes()) {
        if (positive.getDeadEndCount() == 2)
            total += positive.getLength() * (positive.isSelfComplement() ? 2 : 1);
    }
    return total;
}

QStringList AssemblyGraph::getCustomLabelForDisplay(OrientedNode node) const {
    QStringList customLabelLines;
    QString label = getCustomLabel(node);
    if (!label.isEmpty()) {
        QStringList labelLines = label.split("\n");
        for (auto & labelLine : labelLines)
            customLabelLines << labelLine;
    }

    if (!g_settings->doubleMode && node && !node.isSelfComplement() &&
        !getCustomLabel(node.reverse()).isEmpty()) {
        QStringList labelLines2 = getCustomLabel(node.reverse()).split("\n");
        for (auto & i : labelLines2)
            customLabelLines << i;
    }
    return customLabelLines;
}


QColor AssemblyGraph::getCustomColourForDisplay(OrientedNode node) const {
    if (hasCustomColour(node))
        return getCustomColour(node);

    if (!g_settings->doubleMode && node && !node.isSelfComplement() &&
        hasCustomColour(node.reverse()))
        return getCustomColour(node.reverse());
    return g_settings->defaultCustomNodeColour;
}
