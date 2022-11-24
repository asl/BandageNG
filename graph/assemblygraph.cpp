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
#include "path.h"
#include "io.h"
#include "graphicsitemedge.h"
#include "graphicsitemnode.h"
#include "sequenceutils.h"

#include "layout/graphlayoutworker.h"

#include "program/memory.h"
#include "program/globals.h"
#include "program/settings.h"

#include "ui/dialogs/myprogressdialog.h"
#include "ui/bandagegraphicsscene.h"

#include <QApplication>
#include <QFile>
#include <QList>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>
#include <cmath>
#include <utility>
#include <deque>

AssemblyGraph::AssemblyGraph()
        : m_kmer(0),
          m_sequencesLoadedFromFasta(NOT_READY)
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
    {
        for (auto &entry : m_deBruijnGraphPaths) {
            delete entry;
        }
        m_deBruijnGraphPaths.clear();
    }


    {
        for (auto &entry : m_deBruijnGraphNodes) {
            delete entry;
        }
        m_deBruijnGraphNodes.clear();
    }

    {
        for (auto &entry : m_deBruijnGraphEdges) {
            delete entry.second;
        }
        m_deBruijnGraphEdges.clear();
    }

    m_nodeTags.clear();
    m_edgeTags.clear();
    m_nodeColors.clear();
    m_nodeLabels.clear();
    m_nodeCSVData.clear();
    
    clearGraphInfo();
}

//The function returns a node name, replacing "+" at the end with "-" or
//vice-versa.
static QString getOppositeNodeName(QString nodeName) {
    QChar lastChar = nodeName.at(nodeName.size() - 1);
    nodeName.chop(1);

    if (lastChar == '-')
        return nodeName + "+";
    else
        return nodeName + "-";
}

//This function makes a double edge: in one direction for the given nodes
//and the opposite direction for their reverse complements.  It adds the
//new edges to the vector here and to the nodes themselves.
void AssemblyGraph::createDeBruijnEdge(const QString& node1Name, const QString& node2Name,
                                       int overlap, EdgeOverlapType overlapType)
{
    QString node1Opposite = getOppositeNodeName(node1Name);
    QString node2Opposite = getOppositeNodeName(node2Name);

    //Quit if any of the nodes don't exist.
    auto node1 = m_deBruijnGraphNodes.find(node1Name.toStdString());
    auto node2 = m_deBruijnGraphNodes.find(node2Name.toStdString());
    auto negNode1 = m_deBruijnGraphNodes.find(node1Opposite.toStdString());
    auto negNode2 = m_deBruijnGraphNodes.find(node2Opposite.toStdString());
    if (node1 == m_deBruijnGraphNodes.end() ||
        node2 == m_deBruijnGraphNodes.end() ||
        negNode1 == m_deBruijnGraphNodes.end() ||
        negNode2 == m_deBruijnGraphNodes.end())
        return;

    //Quit if the edge already exists
    for (const auto *edge : (*node1)->edges()) {
        if (edge->getStartingNode() == *node1 &&
            edge->getEndingNode() == *node2)
            return;
    }

    //Usually, an edge has a different pair, but it is possible
    //for an edge to be its own pair.
    bool isOwnPair = (*node1 == *negNode2 && *node2 == *negNode1);

    auto * forwardEdge = new DeBruijnEdge(*node1, *node2);
    DeBruijnEdge * backwardEdge;

    if (isOwnPair)
        backwardEdge = forwardEdge;
    else
        backwardEdge = new DeBruijnEdge(*negNode2, *negNode1);

    forwardEdge->setReverseComplement(backwardEdge);
    backwardEdge->setReverseComplement(forwardEdge);

    forwardEdge->setOverlap(overlap);
    backwardEdge->setOverlap(overlap);
    forwardEdge->setOverlapType(overlapType);
    backwardEdge->setOverlapType(overlapType);

    m_deBruijnGraphEdges.emplace(std::make_pair(forwardEdge->getStartingNode(), forwardEdge->getEndingNode()), forwardEdge);
    if (!isOwnPair)
        m_deBruijnGraphEdges.emplace(std::make_pair(backwardEdge->getStartingNode(), backwardEdge->getEndingNode()), backwardEdge);

    (*node1)->addEdge(forwardEdge);
    (*node2)->addEdge(forwardEdge);
    (*negNode1)->addEdge(backwardEdge);
    (*negNode2)->addEdge(backwardEdge);
}

void AssemblyGraph::resetNodes()
{
    for (auto &entry : m_deBruijnGraphNodes)
        entry->resetNode();
}

void AssemblyGraph::resetEdges()
{
    for (auto &entry : m_deBruijnGraphEdges) {
        entry.second->reset();
    }
}


double AssemblyGraph::getMeanDepth(bool drawnNodesOnly)
{
    long double depthSum = 0.0;
    long long totalLength = 0;

    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;

        if (drawnNodesOnly && node->isNotDrawn())
            continue;

        totalLength += node->getLength();
        depthSum += node->getLength() * node->getDepth();
    }

    if (totalLength == 0)
        return 0.0;
    else
        return depthSum / totalLength;
}


double AssemblyGraph::getMeanDepth(const std::vector<DeBruijnNode *> &nodes)
{
    if (nodes.empty())
        return 0.0;

    if (nodes.size() == 1)
        return nodes[0]->getDepth();

    int nodeCount = 0;
    long double depthSum = 0.0;
    long long totalLength = 0;

    for (auto node : nodes)
    {
        ++nodeCount;
        totalLength += node->getLength();
        depthSum += node->getLength() * node->getDepth();
    }

    //If the total length is zero, that means all nodes have a length of zero.
    //In this case, just return the average node depth.
    if (totalLength == 0)
    {
        long double depthSum = 0.0;
        for (auto & node : nodes)
            depthSum += node->getDepth();
        return depthSum / nodes.size();
    }

    return depthSum / totalLength;
}


void AssemblyGraph::determineGraphInfo()
{
    m_shortestContig = std::numeric_limits<long long>::max();
    m_longestContig = 0;
    int nodeCount = 0;
    long long totalLength = 0;
    std::vector<double> nodeDepths;

    for (auto &entry : m_deBruijnGraphNodes) {
        long long nodeLength = entry->getLength();

        if (nodeLength < m_shortestContig)
            m_shortestContig = nodeLength;
        if (nodeLength > m_longestContig)
            m_longestContig = nodeLength;

        //Only add up the length for positive nodes
        if (entry->isPositiveNode())
        {
            totalLength += nodeLength;
            ++nodeCount;
        }

        nodeDepths.push_back(entry->getDepth());
    }

    //Count up the edges that will be shown in single mode (i.e. positive
    //edges).
    int edgeCount = 0;
    for (auto &entry : m_deBruijnGraphEdges) {
        DeBruijnEdge * edge = entry.second;
        if (edge->isPositiveEdge())
            ++edgeCount;
    }

    m_nodeCount = nodeCount;
    m_edgeCount = edgeCount;
    m_totalLength = totalLength;
    m_meanDepth = getMeanDepth();
    m_pathCount = m_deBruijnGraphPaths.size();

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

    //Check to see if any of the columns holds colour data.
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
        
        std::vector<DeBruijnNode *> nodes;
        // See if this is a path name
        {
            auto pathIt = m_deBruijnGraphPaths.find(nodeName.toStdString());
            if (pathIt != m_deBruijnGraphPaths.end()) {
                for (auto *node : (*pathIt)->nodes()) {
                    nodes.emplace_back(node);
                    if (!g_settings->doubleMode)
                        nodes.emplace_back(node->getReverseComplement());
                }
            }
        }

        // Just node name
        if (nodes.empty()) {
            auto nodeIt = m_deBruijnGraphNodes.find(getNodeNameFromString(nodeName).toStdString());
            if (nodeIt != m_deBruijnGraphNodes.end())
                nodes.emplace_back(*nodeIt);
        }

        if (nodes.empty()) {
            unmatchedNodes += 1;
            continue;
        }

        // Get rid of the node name - no need to save that.
        cols.pop_front();

        // Get rid of any extra data that doesn't have a header.
        cols.resize(columnCount);

        for (auto *node: nodes)
            setCsvData(node, cols);

        // If one of the columns holds colour data, get the colour from that one.
        // Acceptable colour formats: 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a
        // standard colour name (e.g. skyblue).
        // If the colour value is something other than one of these, a colour will be assigned to the value.  That way
        // categorical names can be used and automatically given colours.
        if (colourCol == -1 || cols.size() <= colourCol)
            continue;

        QString colourString = cols[colourCol];
        QColor colour(colourString);
        if (!colour.isValid()) {
            QColor presetColor = presetColours[colourCategories.size() % presetColours.size()];
            auto colorCat = colourCategories.insert(colourString, presetColor);
            colour = colorCat.value();
        }

        for (auto *node: nodes)
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

// Returns true if successful, false if not.
bool AssemblyGraph::loadGraphFromFile(const QString& filename) {
    cleanUp();
    
    auto builder = io::AssemblyGraphBuilder::get(filename);
    if (!builder)
        return false;
    
    try {
        builder->build(*this);
    } catch (...) {
        return false;
    }

    determineGraphInfo();

    // FIXME: get rid of this!
    g_memory->clearGraphSpecificMemory();
    g_settings->nodeColorer->reset();

    return true;
}


//The startingNodes and nodeDistance parameters are only used if the graph scope
//is not WHOLE_GRAPH.
void AssemblyGraph::markNodesToDraw(const graph::Scope &scope,
                                    const std::vector<DeBruijnNode *>& startingNodes) {
    if (scope.graphScope() == WHOLE_GRAPH) {
        for (auto &entry : m_deBruijnGraphNodes) {
            //If double mode is off, only positive nodes are drawn.  If it's
            //on, all nodes are drawn.
            if (entry->isPositiveNode() || g_settings->doubleMode)
                entry->setAsDrawn();
        }
    } else {
        for (auto *node : startingNodes) {
            //If we are in single mode, make sure that each node is positive.
            if (!g_settings->doubleMode && node->isNegativeNode())
                node = node->getReverseComplement();

            node->setAsDrawn();
            node->setAsSpecial();
            node->labelNeighbouringNodesAsDrawn(scope.distance());
        }
    }

    // Then loop through each edge determining its drawn status
    for (auto &entry : m_deBruijnGraphEdges)
        entry.second->determineIfDrawn();
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

QString AssemblyGraph::generateNodesNotFoundErrorMessage(std::vector<QString> nodesNotInGraph, bool exact) {
    QString errorMessage;
    if (exact)
        errorMessage += "The following nodes are not in the graph:\n";
    else
        errorMessage += "The following queries do not match any nodes in the graph:\n";

    for (size_t i = 0; i < nodesNotInGraph.size(); ++i) {
        errorMessage += nodesNotInGraph[i];
        if (i != nodesNotInGraph.size() - 1)
            errorMessage += ", ";
    }
    errorMessage += "\n";

    return errorMessage;
}


std::vector<DeBruijnNode *> AssemblyGraph::getNodesFromString(QString nodeNamesString, bool exactMatch, std::vector<QString> * nodesNotInGraph) const
{
    nodeNamesString = nodeNamesString.simplified();
    QStringList nodesList = nodeNamesString.split(",");

    if (exactMatch)
        return getNodesFromListExact(nodesList, nodesNotInGraph);
    else
        return getNodesFromListPartial(nodesList, nodesNotInGraph);
}


//Given a list of node names (as strings), this function will return all nodes which match
//those names exactly.  The last +/- on the end of the node name is optional - if missing
//both + and - nodes will be returned.
std::vector<DeBruijnNode *> AssemblyGraph::getNodesFromListExact(const QStringList& nodesList,
                                                                 std::vector<QString> * nodesNotInGraph) const
{
    std::vector<DeBruijnNode *> returnVector;

    for (const auto & i : nodesList)
    {
        QString nodeName = i.simplified();
        if (nodeName == "")
            continue;

        //If the node name ends in +/-, then we assume the user was specifying the exact
        //node in the pair.  If the node name does not end in +/-, then we assume the
        //user is asking for either node in the pair.
        QChar lastChar = nodeName.at(nodeName.length() - 1);
        if (lastChar == '+' || lastChar == '-')
        {
            if (m_deBruijnGraphNodes.count(nodeName.toStdString()))
                returnVector.push_back(m_deBruijnGraphNodes.at(nodeName.toStdString()));
            else if (nodesNotInGraph != nullptr)
                nodesNotInGraph->push_back(i.trimmed());
        }
        else
        {
            QString posNodeName = nodeName + "+";
            QString negNodeName = nodeName + "-";

            bool posNodeFound = false;
            if (m_deBruijnGraphNodes.count(posNodeName.toStdString()))
            {
                returnVector.push_back(m_deBruijnGraphNodes.at(posNodeName.toStdString()));
                posNodeFound = true;
            }

            bool negNodeFound = false;
            if (m_deBruijnGraphNodes.count(negNodeName.toStdString()))
            {
                returnVector.push_back(m_deBruijnGraphNodes.at(negNodeName.toStdString()));
                negNodeFound = true;
            }

            if (!posNodeFound && !negNodeFound && nodesNotInGraph != nullptr)
                nodesNotInGraph->push_back(i.trimmed());
        }
    }

    return returnVector;
}

std::vector<DeBruijnNode *> AssemblyGraph::getNodesFromListPartial(const QStringList& nodesList,
                                                                   std::vector<QString> * nodesNotInGraph) const
{
    std::vector<DeBruijnNode *> returnVector;

    for (const auto & i : nodesList)
    {
        QString queryName = i.simplified();
        if (queryName == "")
            continue;

        bool found = false;
        for (auto &entry : m_deBruijnGraphNodes) {
            QString nodeName = entry->getName();

            if (nodeName.contains(queryName))
            {
                found = true;
                returnVector.push_back(entry);
            }
        }

        if (!found && nodesNotInGraph != nullptr)
            nodesNotInGraph->push_back(queryName.trimmed());
    }

    return returnVector;
}

std::vector<DeBruijnNode *> AssemblyGraph::getNodesInDepthRange(double min, double max) const {
    std::vector<DeBruijnNode *> returnVector;

    for (auto *node : m_deBruijnGraphNodes) {
        if (node->isInDepthRange(min, max))
            returnVector.push_back(node);
    }
    return returnVector;
}

void AssemblyGraph::setAllEdgesExactOverlap(int overlap) {
    for (auto &entry : m_deBruijnGraphEdges) {
        entry.second->setExactOverlap(overlap);
    }
}



void AssemblyGraph::autoDetermineAllEdgesExactOverlap()
{
    int edgeCount = int(m_deBruijnGraphEdges.size());
    if (edgeCount == 0)
        return;

    //Determine the overlap for each edge.
    for (auto &entry : m_deBruijnGraphEdges) {
        entry.second->autoDetermineExactOverlap();
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
    for (auto &entry : m_deBruijnGraphEdges) {
        DeBruijnEdge * edge = entry.second;
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

    for (auto &entry : m_deBruijnGraphEdges) {
        int overlap = entry.second->getOverlap();

        //Add the overlap to the count vector
        if (int(overlapCounts.size()) < overlap + 1)
            overlapCounts.resize(overlap + 1, 0);
        ++overlapCounts[overlap];
    }

    return overlapCounts;
}

QString AssemblyGraph::getUniqueNodeName(QString baseName) const {
    //If the base name is untaken, then that's it!
    if (!m_deBruijnGraphNodes.count((baseName + "+").toStdString()))
        return baseName;

    int suffix = 1;
    while (true) {
        ++suffix;
        QString potentialUniqueName = baseName + "_" + QString::number(suffix);
        if (!m_deBruijnGraphNodes.count((potentialUniqueName + "+").toStdString()))
            return potentialUniqueName;
    }

    //Code should never get here.
    return baseName;
}


void AssemblyGraph::recalculateAllNodeWidths(double averageNodeWidth,
                                             double depthPower, double depthEffectOnWidth) {
    double meanDrawnDepth = getMeanDepth(true);

    for (auto *node : m_deBruijnGraphNodes) {
        if (GraphicsItemNode * graphicsItemNode = node->getGraphicsItemNode())
            graphicsItemNode->setWidth(meanDrawnDepth== 0 ? 1.0 : node->getDepth() / meanDrawnDepth,
                                       averageNodeWidth,
                                       depthPower, depthEffectOnWidth);
    }
}


int AssemblyGraph::getDrawnNodeCount() const {
    int nodeCount = 0;
    for (auto *node : m_deBruijnGraphNodes)
        nodeCount += node->isDrawn();

    return nodeCount;
}


void AssemblyGraph::deleteNodes(const std::vector<DeBruijnNode *> &nodes)
{
    //Build a list of nodes to delete.
    QSet<DeBruijnNode *> nodesToDelete;
    for (auto *node : nodes) {
        nodesToDelete.insert(node);
        nodesToDelete.insert(node->getReverseComplement());
    }

    //Build a list of edges to delete.
    std::vector<DeBruijnEdge *> edgesToDelete;
    for (auto *node : nodesToDelete) {
        for (auto *edge : node->edges()) {
            bool alreadyAdded = std::find(edgesToDelete.begin(), edgesToDelete.end(), edge) != edgesToDelete.end();
            if (!alreadyAdded)
                edgesToDelete.push_back(edge);
        }
    }

    // Remove the edges from the graph,
    deleteEdges(edgesToDelete);

    // Remove the nodes from the graph.
    for (auto *node : nodesToDelete)
        m_deBruijnGraphNodes.erase(node->getName().toStdString());

    for (auto *node : nodesToDelete)
        delete node;
}

void AssemblyGraph::deleteEdges(const std::vector<DeBruijnEdge *> &edges)
{
    //Build a list of edges to delete.
    QSet<DeBruijnEdge *> edgesToDelete;
    for (auto *edge : edges) {
        edgesToDelete.insert(edge);
        edgesToDelete.insert(edge->getReverseComplement());
    }

    //Remove the edges from the graph,
    for (auto edge : edgesToDelete) {
        DeBruijnNode * startingNode = edge->getStartingNode();
        DeBruijnNode * endingNode = edge->getEndingNode();

        m_deBruijnGraphEdges.erase(QPair<DeBruijnNode*, DeBruijnNode*>(startingNode, endingNode));
        startingNode->removeEdge(edge);
        endingNode->removeEdge(edge);

        delete edge;
    }
}

//This function assumes it is receiving a positive node.  It will duplicate both
//the positive and negative node in the pair.  It divided their depth in
//two, giving half to each node.
void AssemblyGraph::duplicateNodePair(DeBruijnNode * node, BandageGraphicsScene * scene)
{
    DeBruijnNode * originalPosNode = node;
    DeBruijnNode * originalNegNode = node->getReverseComplement();

    QString newNodeBaseName = getNewNodeName(originalPosNode->getName());
    QString newPosNodeName = newNodeBaseName + "+";
    QString newNegNodeName = newNodeBaseName + "-";

    double newDepth = node->getDepth() / 2.0;

    //Create the new nodes.
    auto * newPosNode = new DeBruijnNode(newPosNodeName, newDepth, originalPosNode->getSequence());
    auto * newNegNode = new DeBruijnNode(newNegNodeName, newDepth, originalNegNode->getSequence());
    newPosNode->setReverseComplement(newNegNode);
    newNegNode->setReverseComplement(newPosNode);

    //Copy over additional stuff from the original nodes.
    setCustomColour(newPosNode, getCustomColour(originalPosNode));
    setCustomColour(newNegNode, getCustomColour(originalNegNode));
    setCustomLabel(newPosNode, getCustomLabel(originalPosNode));
    setCustomLabel(newNegNode, getCustomLabel(originalNegNode));
    setCsvData(newPosNode, getAllCsvData(originalPosNode));
    setCsvData(newNegNode, getAllCsvData(originalNegNode));

    m_deBruijnGraphNodes.emplace(newPosNodeName.toStdString(), newPosNode);
    m_deBruijnGraphNodes.emplace(newNegNodeName.toStdString(), newNegNode);

    std::vector<DeBruijnEdge *> leavingEdges = originalPosNode->getLeavingEdges();
    for (auto *edge : leavingEdges) {
        DeBruijnNode * downstreamNode = edge->getEndingNode();
        createDeBruijnEdge(newPosNodeName, downstreamNode->getName(),
                           edge->getOverlap(), edge->getOverlapType());
    }

    std::vector<DeBruijnEdge *> enteringEdges = originalPosNode->getEnteringEdges();
    for (auto *edge : enteringEdges) {
        DeBruijnNode * upstreamNode = edge->getStartingNode();
        createDeBruijnEdge(upstreamNode->getName(), newPosNodeName,
                           edge->getOverlap(), edge->getOverlapType());
    }

    originalPosNode->setDepth(newDepth);
    originalNegNode->setDepth(newDepth);

    scene->duplicateGraphicsNode(originalPosNode, newPosNode);
    scene->duplicateGraphicsNode(originalNegNode, newNegNode);
}

QString AssemblyGraph::getNewNodeName(QString oldNodeName) const
{
    oldNodeName.chop(1); //Remove trailing +/-

    QString newNodeNameBase = oldNodeName + "_copy";
    QString newNodeName = newNodeNameBase;

    int suffix = 1;
    while (m_deBruijnGraphNodes.count((newNodeName + "+").toStdString()))
    {
        ++suffix;
        newNodeName = newNodeNameBase + QString::number(suffix);
    }

    return newNodeName;
}

static bool canAddNodeToStartOfMergeList(const DeBruijnNode *firstNode,
                                         const DeBruijnNode *potentialNode) {
    std::vector<DeBruijnEdge *> edgesEnteringFirstNode = firstNode->getEnteringEdges();
    std::vector<DeBruijnEdge *> edgesLeavingPotentialNode = potentialNode->getLeavingEdges();
    return (edgesEnteringFirstNode.size() == 1 &&
            edgesLeavingPotentialNode.size() == 1 &&
            edgesEnteringFirstNode[0]->getStartingNode() == potentialNode &&
            edgesLeavingPotentialNode[0]->getEndingNode() == firstNode);
}


static bool canAddNodeToEndOfMergeList(const DeBruijnNode *lastNode,
                                       const DeBruijnNode *potentialNode) {
    std::vector<DeBruijnEdge *> edgesLeavingLastNode = lastNode->getLeavingEdges();
    std::vector<DeBruijnEdge *> edgesEnteringPotentialNode = potentialNode->getEnteringEdges();
    return (edgesLeavingLastNode.size() == 1 &&
            edgesEnteringPotentialNode.size() == 1 &&
            edgesLeavingLastNode[0]->getEndingNode() == potentialNode &&
            edgesEnteringPotentialNode[0]->getStartingNode() == lastNode);
}

static void mergeGraphicsNodes(const std::vector<DeBruijnNode *> &originalNodes,
                               const std::vector<DeBruijnNode *> &revCompOriginalNodes,
                               DeBruijnNode * newNode,
                               BandageGraphicsScene * scene);

//This function will merge the given nodes, if possible.  Nodes can only be
//merged if they are in a simple, unbranching path with no extra edges.  If the
//merge is successful, it returns true, otherwise false.
bool AssemblyGraph::mergeNodes(QList<DeBruijnNode *> nodes, BandageGraphicsScene * scene) {
    if (nodes.empty())
        return true;

    //We now need to sort the nodes into merge order.
    std::deque<DeBruijnNode *> mergeList;
    mergeList.push_back(nodes[0]);
    nodes.pop_front();

    bool addedNode;
    do {
        addedNode = false;
        for (int i = 0; i < nodes.size(); ++i) {
            DeBruijnNode * potentialNextNode = nodes[i];

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
            DeBruijnNode * potentialNextNodeRevComp = potentialNextNode->getReverseComplement();
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

    std::vector<DeBruijnNode*> orderedList(mergeList.begin(), mergeList.end());
    Path posPath = Path::makeFromOrderedNodes(orderedList, false);
    Sequence mergedNodePosSequence{posPath.getPathSequence()};

    std::vector<DeBruijnNode*> revCompOrderedList;
    for (auto it = orderedList.rbegin(); it != orderedList.rend(); ++it)
        revCompOrderedList.push_back((*it)->getReverseComplement());

    Path negPath = Path::makeFromOrderedNodes(revCompOrderedList, false);
    Sequence mergedNodeNegSequence{negPath.getPathSequence()};

    QString newNodeBaseName;
    for (int i = 0; i < orderedList.size(); ++i) {
        newNodeBaseName += orderedList[i]->getNameWithoutSign();
        if (i < orderedList.size() - 1)
            newNodeBaseName += "_";
    }
    newNodeBaseName = getUniqueNodeName(newNodeBaseName);
    QString newPosNodeName = newNodeBaseName + "+";
    QString newNegNodeName = newNodeBaseName + "-";

    double mergedNodeDepth = getMeanDepth(orderedList);

    auto newPosNode = new DeBruijnNode(newPosNodeName, mergedNodeDepth, mergedNodePosSequence);
    auto newNegNode = new DeBruijnNode(newNegNodeName, mergedNodeDepth, mergedNodeNegSequence);

    newPosNode->setReverseComplement(newNegNode);
    newNegNode->setReverseComplement(newPosNode);

    m_deBruijnGraphNodes.emplace(newPosNodeName.toStdString(), newPosNode);
    m_deBruijnGraphNodes.emplace(newNegNodeName.toStdString(), newNegNode);

    for (auto *leavingEdge : orderedList.back()->getLeavingEdges())
        createDeBruijnEdge(newPosNodeName, leavingEdge->getEndingNode()->getName(), leavingEdge->getOverlap(),
                           leavingEdge->getOverlapType());

    for (auto *enteringEdge : orderedList.front()->getEnteringEdges())
        createDeBruijnEdge(enteringEdge->getStartingNode()->getName(), newPosNodeName, enteringEdge->getOverlap(),
                           enteringEdge->getOverlapType());

    mergeGraphicsNodes(orderedList, revCompOrderedList, newPosNode, scene);

    deleteNodes(orderedList);

    recalculateAllNodeWidths(g_settings->averageNodeWidth,
                             g_settings->depthPower, g_settings->depthEffectOnWidth);

    return true;
}


static bool mergeGraphicsNodes2(const std::vector<DeBruijnNode *> &originalNodes,
                                DeBruijnNode * newNode,
                                BandageGraphicsScene * scene) {
    bool success = true;
    std::vector<QPointF> linePoints;

    for (auto *node : originalNodes) {
        //If we are in single mode, then we should check for a GraphicsItemNode only
        //in the positive nodes.
        bool opposite = false;
        if (!g_settings->doubleMode && node->isNegativeNode()) {
            node = node->getReverseComplement();
            opposite = true;
        }

        GraphicsItemNode * originalGraphicsItemNode = node->getGraphicsItemNode();
        if (originalGraphicsItemNode == nullptr) {
            success = false;
            break;
        }

        const auto& originalLinePoints = originalGraphicsItemNode->m_linePoints;

        //Add the original line points to the new line point collection.  If we
        //are working with an opposite node, then we need to reverse the order.
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
        auto * newGraphicsItemNode = new GraphicsItemNode(newNode, 0, linePoints);

        newNode->setGraphicsItemNode(newGraphicsItemNode);
        newGraphicsItemNode->setFlag(QGraphicsItem::ItemIsSelectable);
        newGraphicsItemNode->setFlag(QGraphicsItem::ItemIsMovable);
        newGraphicsItemNode->setNodeColour(g_settings->nodeColorer->get(newGraphicsItemNode));

        scene->addItem(newGraphicsItemNode);

        for (auto *newEdge : newNode->edges()) {
            auto * graphicsItemEdge = new GraphicsItemEdge(newEdge);
            graphicsItemEdge->setZValue(-1.0);
            newEdge->setGraphicsItemEdge(graphicsItemEdge);
            graphicsItemEdge->setFlag(QGraphicsItem::ItemIsSelectable);
            scene->addItem(graphicsItemEdge);
        }
    }
    return success;
}

static void mergeGraphicsNodes(const std::vector<DeBruijnNode *> &originalNodes,
                               const std::vector<DeBruijnNode *> &revCompOriginalNodes,
                               DeBruijnNode * newNode,
                               BandageGraphicsScene * scene) {
    bool success = mergeGraphicsNodes2(originalNodes, newNode, scene);
    if (success)
        newNode->setAsDrawn();

    if (g_settings->doubleMode) {
        DeBruijnNode * newRevComp = newNode->getReverseComplement();
        bool revCompSuccess = mergeGraphicsNodes2(revCompOriginalNodes, newRevComp, scene);
        if (revCompSuccess)
            newRevComp->setAsDrawn();
    }

    BandageGraphicsScene::removeGraphicsItemNodes(originalNodes, true);
}

//This function simplifies the graph by merging all possible nodes in a simple
//line.  It returns the number of merges that it did.
//It gets a pointer to the progress dialog as well so it can check to see if the
//user has cancelled the merge.
int AssemblyGraph::mergeAllPossible(BandageGraphicsScene * scene,
                                    MyProgressDialog * progressDialog)
{
    //Create a set of all nodes.
    QSet<DeBruijnNode *> uncheckedNodes;
    for (auto &entry : m_deBruijnGraphNodes) {
        uncheckedNodes.insert(entry);
    }

    //Create a list of all merges to be done.
    QList< QList<DeBruijnNode *> > allMerges;
    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;

        //If the current node isn't checked, then we will find the longest
        //possible mergable sequence containing this node.
        if (uncheckedNodes.contains(node))
        {
            QList<DeBruijnNode *> nodesToMerge;
            nodesToMerge.push_back(node);

            uncheckedNodes.remove(node);
            uncheckedNodes.remove(node->getReverseComplement());

            //Extend forward as much as possible.
            bool extended;
            do
            {
                extended = false;
                DeBruijnNode * last = nodesToMerge.back();
                std::vector<DeBruijnEdge *> outgoingEdges = last->getLeavingEdges();
                if (outgoingEdges.size() == 1)
                {
                    DeBruijnEdge * potentialEdge = outgoingEdges[0];
                    DeBruijnNode * potentialNode = potentialEdge->getEndingNode();
                    std::vector<DeBruijnEdge *> edgesEnteringPotentialNode = potentialNode->getEnteringEdges();
                    if (edgesEnteringPotentialNode.size() == 1 &&
                            edgesEnteringPotentialNode[0] == potentialEdge &&
                            !nodesToMerge.contains(potentialNode) &&
                            uncheckedNodes.contains(potentialNode))
                    {
                        nodesToMerge.push_back(potentialNode);
                        uncheckedNodes.remove(potentialNode);
                        uncheckedNodes.remove(potentialNode->getReverseComplement());
                        extended = true;
                    }
                }
            } while (extended);

            //Extend backward as much as possible.
            do
            {
                extended = false;
                DeBruijnNode * first = nodesToMerge.front();
                std::vector<DeBruijnEdge *> incomingEdges = first->getEnteringEdges();
                if (incomingEdges.size() == 1)
                {
                    DeBruijnEdge * potentialEdge = incomingEdges[0];
                    DeBruijnNode * potentialNode = potentialEdge->getStartingNode();
                    std::vector<DeBruijnEdge *> edgesLeavingPotentialNode = potentialNode->getLeavingEdges();
                    if (edgesLeavingPotentialNode.size() == 1 &&
                            edgesLeavingPotentialNode[0] == potentialEdge &&
                            !nodesToMerge.contains(potentialNode) &&
                            uncheckedNodes.contains(potentialNode))
                    {
                        nodesToMerge.push_front(potentialNode);
                        uncheckedNodes.remove(potentialNode);
                        uncheckedNodes.remove(potentialNode->getReverseComplement());
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

bool AssemblyGraph::hasCustomColour(const DeBruijnNode* node) const {
    auto it = m_nodeColors.find(node);
    return it != m_nodeColors.end() && it->second.isValid();
}

bool AssemblyGraph::hasCustomColour(const DeBruijnEdge* edge) const {
    auto it = m_edgeColors.find(edge);
    return it != m_edgeColors.end() && it->second.isValid();
}

bool AssemblyGraph::hasCustomStyle(const DeBruijnEdge* edge) const {
    return m_edgeStyles.contains(edge);
}

QColor AssemblyGraph::getCustomColour(const DeBruijnNode* node) const {
    auto it = m_nodeColors.find(node);
    return it == m_nodeColors.end() ? QColor() : it->second;
}

QColor AssemblyGraph::getCustomColour(const DeBruijnEdge* edge) const {
    auto it = m_edgeColors.find(edge);
    return it == m_edgeColors.end() ? g_settings->edgeColour : it->second;
}

AssemblyGraph::EdgeStyle AssemblyGraph::getCustomStyle(const DeBruijnEdge* edge) const {
    auto it = m_edgeStyles.find(edge);
    return it == m_edgeStyles.end() ? AssemblyGraph::EdgeStyle() : it->second;
}

void AssemblyGraph::setCustomColour(const DeBruijnNode* node, QColor color) {
    m_nodeColors[node] = color;
}

void AssemblyGraph::setCustomColour(const DeBruijnEdge* edge, QColor color) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    m_edgeColors[edge] = color;
}

void AssemblyGraph::setCustomStyle(const DeBruijnEdge* edge, Qt::PenStyle lineStyle) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    m_edgeStyles[edge].lineStyle = lineStyle;
}

AssemblyGraph::EdgeStyle::EdgeStyle()
 : width(float(g_settings->edgeWidth)), lineStyle(Qt::SolidLine) {}

void AssemblyGraph::setCustomStyle(const DeBruijnEdge* edge, float width) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    m_edgeStyles[edge].width = width;
}

void AssemblyGraph::setCustomStyle(const DeBruijnEdge* edge, EdgeStyle style) {
    // To simplify upstream code we ignore null edges
    if (edge == nullptr)
        return;
    m_edgeStyles[edge] = style;
}

QString AssemblyGraph::getCustomLabel(const DeBruijnNode* node) const {
    auto it = m_nodeLabels.find(node);
    return it == m_nodeLabels.end() ? QString() : it->second;
}

void AssemblyGraph::setCustomLabel(const DeBruijnNode* node, QString newLabel) {
    newLabel.replace("\t", "    ");
    m_nodeLabels[node] = newLabel;
}

void AssemblyGraph::clearAllCsvData() {
    m_csvHeaders.clear();
    for (auto &entry : m_deBruijnGraphNodes) {
        clearCsvData(entry);
    }
}

bool AssemblyGraph::hasCsvData(const DeBruijnNode* node) const {
    auto it = m_nodeCSVData.find(node);
    return it != m_nodeCSVData.end() && !it->second.isEmpty();
}

QStringList AssemblyGraph::getAllCsvData(const DeBruijnNode *node) const {
    auto it = m_nodeCSVData.find(node);
    return it == m_nodeCSVData.end() ? QStringList() : it->second;
}

std::optional<QString> AssemblyGraph::getCsvLine(const DeBruijnNode *node, int i) const {
    auto it = m_nodeCSVData.find(node);
    if (it == m_nodeCSVData.end() ||
        i >= it->second.length())
        return "";

    return it->second[i];
}

void AssemblyGraph::setCsvData(const DeBruijnNode* node, QStringList csvData) {
    m_nodeCSVData[node] = std::move(csvData);
}

void AssemblyGraph::clearCsvData(const DeBruijnNode* node) {
    m_nodeCSVData[node].clear();
}

//This function changes the name of a node pair.  The new and old names are
//both assumed to not include the +/- at the end.
void AssemblyGraph::changeNodeName(const QString& oldName, const QString& newName)
{
    if (checkNodeNameValidity(newName) != NODE_NAME_OKAY)
        return;

    QString posOldNodeName = oldName + "+";
    QString negOldNodeName = oldName + "-";

    if (!m_deBruijnGraphNodes.count(posOldNodeName.toStdString()))
        return;
    if (!m_deBruijnGraphNodes.count(negOldNodeName.toStdString()))
        return;

    DeBruijnNode * posNode = m_deBruijnGraphNodes[posOldNodeName.toStdString()];
    DeBruijnNode * negNode = m_deBruijnGraphNodes[negOldNodeName.toStdString()];

    m_deBruijnGraphNodes.erase(posOldNodeName.toStdString());
    m_deBruijnGraphNodes.erase(negOldNodeName.toStdString());

    QString posNewNodeName = newName + "+";
    QString negNewNodeName = newName + "-";

    posNode->setName(posNewNodeName);
    negNode->setName(negNewNodeName);

    m_deBruijnGraphNodes.emplace(posNewNodeName.toStdString(), posNode);
    m_deBruijnGraphNodes.emplace(negNewNodeName.toStdString(), negNode);
}



//This function checks whether a new node name is okay.  It takes a node name
//without a +/- at the end.
NodeNameStatus AssemblyGraph::checkNodeNameValidity(const QString& nodeName) const
{
    if (nodeName.contains('\t'))
        return NODE_NAME_CONTAINS_TAB;

    if (nodeName.contains('\n'))
        return NODE_NAME_CONTAINS_NEWLINE;

    if (nodeName.contains(','))
        return NODE_NAME_CONTAINS_COMMA;

    if (nodeName.contains(' '))
        return NODE_NAME_CONTAINS_SPACE;

    if (m_deBruijnGraphNodes.count((nodeName + "+").toStdString()))
        return NODE_NAME_TAKEN;

    return NODE_NAME_OKAY;
}



void AssemblyGraph::changeNodeDepth(const std::vector<DeBruijnNode *> &nodes,
                                    double newDepth)
{
    if (nodes.empty())
        return;

    for (auto node : nodes) {
        node->setDepth(newDepth);
        node->getReverseComplement()->setDepth(newDepth);
    }

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

    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;
        if (node->isPositiveNode())
            deadEndCount += node->getDeadEndCount();
    }

    return deadEndCount;
}



void AssemblyGraph::getNodeStats(int * n50, int * shortestNode, int * firstQuartile, int * median, int * thirdQuartile, int * longestNode) const
{
    if (m_totalLength == 0.0)
        return;

    std::vector<int> nodeLengths;
    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;
        if (node->isPositiveNode())
            nodeLengths.push_back(node->getLength());
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
void AssemblyGraph::getGraphComponentCountAndLargestComponentSize(int * componentCount, int * largestComponentLength) const
{
    *componentCount = 0;
    *largestComponentLength = 0;

    QSet<DeBruijnNode *> visitedNodes;
    QList< QList<DeBruijnNode *> > connectedComponents;

    //Loop through all positive nodes.
    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * v = entry;
        if (v->isNegativeNode())
            continue;

        //If the node has not yet been visited, then it must be the start of a new connected component.
        if (!visitedNodes.contains(v))
        {
            QList<DeBruijnNode *> connectedComponent;

            QQueue<DeBruijnNode *> q;
            q.enqueue(v);
            visitedNodes.insert(v);

            while (!q.isEmpty())
            {
                DeBruijnNode * w = q.dequeue();
                connectedComponent.push_back(w);

                std::vector<DeBruijnNode *> connectedNodes = w->getAllConnectedPositiveNodes();
                for (auto k : connectedNodes)
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
            componentLength += j->getLength();

        if (componentLength > *largestComponentLength)
            *largestComponentLength = componentLength;
    }
}

bool compareNodeDepth(DeBruijnNode * a, DeBruijnNode * b) {return (a->getDepth() < b->getDepth());}

//This function takes a node list sorted by depth and a target index (in terms of
//the whole sequence length).  It returns the depth at that index.
static double findDepthAtIndex(QList<DeBruijnNode *> * nodeList, long long targetIndex)
{
    long long lengthSoFar = 0;
    for (auto node : *nodeList)
    {
        lengthSoFar += node->getLength();
        long long currentIndex = lengthSoFar - 1;

        if (currentIndex >= targetIndex)
            return node->getDepth();
    }
    return 0.0;
}


double AssemblyGraph::getMedianDepthByBase() const
{
    if (m_totalLength == 0)
        return 0.0;

    //Make a list of all nodes.
    long long totalLength = 0;
    QList<DeBruijnNode *> nodeList;
    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;
        if (node->isPositiveNode())
        {
            nodeList.push_back(node);
            totalLength += node->getLength();
        }
    }

    //If there is only one node, then its depth is the median.
    if (nodeList.size() == 1)
        return nodeList[0]->getDepth();

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

    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;

        if (node->isPositiveNode())
        {
            int nodeLength = node->getLengthWithoutTrailingOverlap();
            double relativeDepth = node->getDepth() / medianDepthByBase;

            int closestIntegerDepth = round(relativeDepth);
            int lengthAdjustedForDepth = nodeLength * closestIntegerDepth;

            estimatedSequenceLength += lengthAdjustedForDepth;
        }
    }

    return estimatedSequenceLength;
}



long long AssemblyGraph::getTotalLengthMinusEdgeOverlaps() const
{
    long long totalLength = 0;
    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;
        if (node->isPositiveNode())
        {
            totalLength += node->getLength();
            int maxOverlap = 0;
            for (const auto* edge : node->edges()) {
                int edgeOverlap = edge->getOverlap();
                maxOverlap = std::max(maxOverlap, edgeOverlap);
            }
            totalLength -= maxOverlap;
        }
    }

    return totalLength;
}


QPair<int, int> AssemblyGraph::getOverlapRange() const
{
    int smallestOverlap = std::numeric_limits<int>::max();
    int largestOverlap = 0;
    for (auto &entry : m_deBruijnGraphEdges) {
        int overlap = entry.second->getOverlap();
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
    for (auto &entry : m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;
        if (node->isPositiveNode() && node->getDeadEndCount() == 2)
            total += node->getLength();
    }
    return total;
}

QStringList AssemblyGraph::getCustomLabelForDisplay(const DeBruijnNode *node) const {
    QStringList customLabelLines;
    QString label = getCustomLabel(node);
    if (!label.isEmpty()) {
        QStringList labelLines = label.split("\n");
        for (auto & labelLine : labelLines)
            customLabelLines << labelLine;
    }

    DeBruijnNode *rc = node->getReverseComplement();
    if (!g_settings->doubleMode && !getCustomLabel(rc).isEmpty()) {
        QStringList labelLines2 = getCustomLabel(rc).split("\n");
        for (auto & i : labelLines2)
            customLabelLines << i;
    }
    return customLabelLines;
}


QColor AssemblyGraph::getCustomColourForDisplay(const DeBruijnNode *node) const {
    if (hasCustomColour(node))
        return getCustomColour(node);

    DeBruijnNode *rc = node->getReverseComplement();
    if (!g_settings->doubleMode && hasCustomColour(rc))
        return getCustomColour(rc);
    return g_settings->defaultCustomNodeColour;
}
