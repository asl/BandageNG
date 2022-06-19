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


#include "debruijnnode.h"
#include "debruijnedge.h"
#include "assemblygraph.h"
#include "sequenceutils.h"

#include "blast/blasthit.h"

#include <cmath>

#include <set>
#include <QApplication>
#include <QSet>


//The length parameter is optional.  If it is set, then the node will use that
//for its length.  If not set, it will just use the sequence length.
DeBruijnNode::DeBruijnNode(QString name, double depth, const Sequence& sequence, int length) :
    m_name(std::move(name)),
    m_depth(depth),
    m_depthRelativeToMeanDrawnDepth(1.0),
    m_sequence(sequence),
    m_length(sequence.size()),
    m_contiguityStatus(NOT_CONTIGUOUS),
    m_reverseComplement(nullptr),
    m_graphicsItemNode(nullptr),
    m_specialNode(false),
    m_drawn(false),
    m_highestDistanceInNeighbourSearch(0)
{
    if (length > 0)
        m_length = length;
}


//This function adds an edge to the Node, but only if the edge hasn't already
//been added.
void DeBruijnNode::addEdge(DeBruijnEdge * edge)
{
    if (std::find(m_edges.begin(), m_edges.end(), edge) == m_edges.end())
        m_edges.push_back(edge);
}


//This function deletes an edge from the node, if it exists.
void DeBruijnNode::removeEdge(DeBruijnEdge * edge)
{
    m_edges.erase(std::remove(m_edges.begin(), m_edges.end(), edge), m_edges.end());
}


//This function resets the node to the state it would be in after a graph
//file was loaded - no contiguity status and no OGDF nodes.
void DeBruijnNode::resetNode()
{
    m_ogdfNodes.clear();
    m_graphicsItemNode = nullptr;
    resetContiguityStatus();
    setAsNotDrawn();
    setAsNotSpecial();
    m_highestDistanceInNeighbourSearch = 0;
}


void DeBruijnNode::addToOgdfGraph(ogdf::Graph * ogdfGraph, ogdf::GraphAttributes * graphAttributes,
                                  ogdf::EdgeArray<double> * edgeArray, double xPos, double yPos)
{
    //If this node or its reverse complement is already in OGDF, then
    //it's not necessary to make the node.
    if (thisOrReverseComplementInOgdf())
        return;

    //Each node in the Velvet sense is made up of multiple nodes in the
    //OGDF sense.  This way, Velvet nodes appear as lines whose length
    //corresponds to the sequence length.
    double drawnNodeLength = getDrawnNodeLength();
    int numberOfGraphEdges = getNumberOfOgdfGraphEdges(drawnNodeLength);
    int numberOfGraphNodes = numberOfGraphEdges + 1;
    double drawnLengthPerEdge = drawnNodeLength / numberOfGraphEdges;

    ogdf::node newNode = nullptr;
    ogdf::node previousNode = nullptr;
    for (int i = 0; i < numberOfGraphNodes; ++i)
    {
        newNode = ogdfGraph->newNode();
        m_ogdfNodes.push_back(newNode);

        if (g_settings->linearLayout) {
            graphAttributes->x(newNode) = xPos;
            graphAttributes->y(newNode) = yPos;
            xPos += g_settings->nodeSegmentLength;
        }

        if (i > 0)
        {
            ogdf::edge newEdge = ogdfGraph->newEdge(previousNode, newNode);
            (*edgeArray)[newEdge] = drawnLengthPerEdge;
        }

        previousNode = newNode;
    }
}



double DeBruijnNode::getDrawnNodeLength() const
{
    double drawnNodeLength = getNodeLengthPerMegabase() * double(getLength()) / 1000000.0;
    if (drawnNodeLength < g_settings->minimumNodeLength)
        drawnNodeLength = g_settings->minimumNodeLength;
    return drawnNodeLength;
}

int DeBruijnNode::getNumberOfOgdfGraphEdges(double drawnNodeLength)
{
    int numberOfGraphEdges = ceil(drawnNodeLength / g_settings->nodeSegmentLength);
    if (numberOfGraphEdges <= 0)
        numberOfGraphEdges = 1;
    return numberOfGraphEdges;
}



//This function determines the contiguity of nodes relative to this one.
//It has two steps:
// -First, for each edge leaving this node, all paths outward are found.
//  Any nodes in any path are MAYBE_CONTIGUOUS, and nodes in all of the
//  paths are CONTIGUOUS.
// -Second, it is necessary to check in the opposite direction - for each
//  of the MAYBE_CONTIGUOUS nodes, do they have a path that unambiguously
//  leads to this node?  If so, then they are CONTIGUOUS.
void DeBruijnNode::determineContiguity()
{
    upgradeContiguityStatus(STARTING);

    //A set is used to store all nodes found in the paths, as the nodes
    //that show up as MAYBE_CONTIGUOUS will have their paths checked
    //to this node.
    std::set<DeBruijnNode *> allCheckedNodes;

    //For each path leaving this node, find all possible paths
    //outward.  Nodes in any of the paths for an edge are
    //MAYBE_CONTIGUOUS.  Nodes in all of the paths for an edge
    //are CONTIGUOUS.
    for (auto edge : m_edges)
    {
        bool outgoingEdge = (this == edge->getStartingNode());

        std::vector< std::vector <DeBruijnNode *> > allPaths;
        edge->tracePaths(outgoingEdge, g_settings->contiguitySearchSteps, &allPaths, this);

        //Set all nodes in the paths as MAYBE_CONTIGUOUS
        for (auto &path : allPaths)
        {
            QApplication::processEvents();
            for (auto node : path)
            {
                node->upgradeContiguityStatus(MAYBE_CONTIGUOUS);
                allCheckedNodes.insert(node);
            }
        }

        //Set all common nodes as CONTIGUOUS_STRAND_SPECIFIC
        std::vector<DeBruijnNode *> commonNodesStrandSpecific = getNodesCommonToAllPaths(&allPaths, false);
        for (auto &node : commonNodesStrandSpecific)
            node->upgradeContiguityStatus(CONTIGUOUS_STRAND_SPECIFIC);

        //Set all common nodes (when including reverse complement nodes)
        //as CONTIGUOUS_EITHER_STRAND
        std::vector<DeBruijnNode *> commonNodesEitherStrand = getNodesCommonToAllPaths(&allPaths, true);
        for (auto node : commonNodesEitherStrand)
        {
            node->upgradeContiguityStatus(CONTIGUOUS_EITHER_STRAND);
            node->getReverseComplement()->upgradeContiguityStatus(CONTIGUOUS_EITHER_STRAND);
        }
    }

    //For each node that was checked, then we check to see if any
    //of its paths leads unambiuously back to the starting node (this node).
    for (auto node : allCheckedNodes)
    {
        QApplication::processEvents();
        ContiguityStatus status = node->getContiguityStatus();

        //First check without reverse complement target for
        //strand-specific contiguity.
        if (status != CONTIGUOUS_STRAND_SPECIFIC &&
                node->doesPathLeadOnlyToNode(this, false))
            node->upgradeContiguityStatus(CONTIGUOUS_STRAND_SPECIFIC);

        //Now check including the reverse complement target for
        //either strand contiguity.
        if (status != CONTIGUOUS_STRAND_SPECIFIC &&
                status != CONTIGUOUS_EITHER_STRAND &&
                node->doesPathLeadOnlyToNode(this, true))
        {
            node->upgradeContiguityStatus(CONTIGUOUS_EITHER_STRAND);
            node->getReverseComplement()->upgradeContiguityStatus(CONTIGUOUS_EITHER_STRAND);
        }
    }
}


//This function differs from the above by including all reverse complement
//nodes in the path search.
std::vector<DeBruijnNode *> DeBruijnNode::getNodesCommonToAllPaths(std::vector< std::vector <DeBruijnNode *> > * paths,
                                                                   bool includeReverseComplements)
{
    std::vector<DeBruijnNode *> commonNodes;

    //If there are no paths, then return the empty vector.
    if (paths->empty())
        return commonNodes;

    //If there is only one path in path, then they are all common nodes
    commonNodes = (*paths)[0];
    if (paths->size() == 1)
        return commonNodes;

    //If there are two or more paths, it's necessary to find the intersection.
    for (size_t i = 1; i < paths->size(); ++i)
    {
        QApplication::processEvents();
        std::vector <DeBruijnNode *> * path = &((*paths)[i]);

        //If we are including reverse complements in the search,
        //then it is necessary to build a new vector that includes
        //reverse complement nodes and then use that vector.
        std::vector <DeBruijnNode *> pathWithReverseComplements;
        if (includeReverseComplements)
        {
            for (auto node : *path)
            {
                pathWithReverseComplements.push_back(node);
                pathWithReverseComplements.push_back(node->getReverseComplement());
            }
            path = &pathWithReverseComplements;
        }

        //Combine the commonNodes vector with the path vector,
        //excluding any repeats.
        std::sort(commonNodes.begin(), commonNodes.end());
        std::sort(path->begin(), path->end());
        std::vector<DeBruijnNode *> newCommonNodes;
        std::set_intersection(commonNodes.begin(), commonNodes.end(), path->begin(), path->end(), std::back_inserter(newCommonNodes));
        commonNodes = newCommonNodes;
    }

    return commonNodes;
}


//This function checks whether this node has any path leading outward that
//unambiguously leads to the given node.
//It checks a number of steps as set by the contiguitySearchSteps setting.
//If includeReverseComplement is true, then this function returns true if
//all paths lead either to the node or its reverse complement node.
bool DeBruijnNode::doesPathLeadOnlyToNode(DeBruijnNode * node, bool includeReverseComplement)
{
    for (auto edge : m_edges)
    {
        bool outgoingEdge = (this == edge->getStartingNode());

        std::vector<DeBruijnNode *> pathSoFar;
        pathSoFar.push_back(this);
        if (edge->leadsOnlyToNode(outgoingEdge, g_settings->contiguitySearchSteps, node, pathSoFar, includeReverseComplement))
            return true;
    }

    return false;
}


//This function only upgrades a node's status, never downgrades.
void DeBruijnNode::upgradeContiguityStatus(ContiguityStatus newStatus)
{
    if (newStatus < m_contiguityStatus)
        m_contiguityStatus = newStatus;
}



//It is expected that the argument connectedNode is either in incomingNodes or
//outgoingNodes.  If that node is the only one in whichever container it is in,
//this function returns true.
bool DeBruijnNode::isOnlyPathInItsDirection(DeBruijnNode * connectedNode,
                                            std::vector<DeBruijnNode *> * incomingNodes,
                                            std::vector<DeBruijnNode *> * outgoingNodes)
{
    std::vector<DeBruijnNode *> * container;
    if (std::find(incomingNodes->begin(), incomingNodes->end(), connectedNode) != incomingNodes->end())
        container = incomingNodes;
    else
        container = outgoingNodes;

    return (container->size() == 1 && (*container)[0] == connectedNode);
}
bool DeBruijnNode::isNotOnlyPathInItsDirection(DeBruijnNode * connectedNode,
                                 std::vector<DeBruijnNode *> * incomingNodes,
                                 std::vector<DeBruijnNode *> * outgoingNodes)
{
    return !isOnlyPathInItsDirection(connectedNode, incomingNodes, outgoingNodes);
}


QByteArray DeBruijnNode::getFasta(bool sign, bool newLines, bool evenIfEmpty) const
{
    QByteArray sequence = utils::sequenceToQByteArray(getSequence());
    if (sequence.isEmpty() && !evenIfEmpty)
        return {};

    QByteArray fasta = ">";
    fasta += getNodeNameForFasta(sign).toLatin1();
    fasta += "\n";
    if (newLines)
        fasta += utils::addNewlinesToSequence(sequence);
    else {
        fasta += sequence;
        fasta += "\n";
    }
    return fasta;
}

//This function gets the node's sequence for a GFA file.  It has two main
//differences from getSequence:
//  -If the graph is from Velvet, it will extend the node sequences
//  -If the sequence is missing, it will just give "*"
QByteArray DeBruijnNode::getSequenceForGfa() const
{
    if (sequenceIsMissing())
        return {"*"};

    QByteArray sequence = utils::sequenceToQByteArray(getSequence());

    if (g_assemblyGraph->m_graphFileType != LAST_GRAPH)
        return sequence;

    //If the code got here, then we are getting a full sequence from a Velvet
    //LastGraph graph, so we need to extend the beginning of the sequence.
    int extensionLength = g_assemblyGraph->m_kmer - 1;

    //If the node is at least k-1 in length, then the necessary sequence can be
    //deduced from the reverse complement node.
    if (getLength() >= extensionLength)
    {
        QByteArray revCompSeq = utils::sequenceToQByteArray(getReverseComplement()->getSequence());
        QByteArray endOfRevCompSeq = revCompSeq.right(extensionLength);
        QByteArray extension = AssemblyGraph::getReverseComplement(endOfRevCompSeq);
        return extension + sequence;
    }

    //If the node is not long enough, then we must look in upstream nodes for
    //the rest of the sequence.
    else
    {
        QByteArray extension = getUpstreamSequence(extensionLength);
        if (extension.length() < extensionLength)
        {
            int additionalBases = extensionLength - extension.length();
            QByteArray n;
            n.fill('N', additionalBases);
            extension = n + extension;
        }
        return extension + sequence;
    }
}


QByteArray DeBruijnNode::getUpstreamSequence(int upstreamSequenceLength) const
{
    std::vector<DeBruijnNode*> upstreamNodes = getUpstreamNodes();

    QByteArray bestUpstreamNodeSequence;

    for (auto upstreamNode : upstreamNodes)
    {
        QByteArray upstreamNodeFullSequence = utils::sequenceToQByteArray(upstreamNode->getSequence());
        QByteArray upstreamNodeSequence;

        //If the upstream node has enough sequence, great!
        if (upstreamNodeFullSequence.length() >= upstreamSequenceLength)
            upstreamNodeSequence = upstreamNodeFullSequence.right(upstreamSequenceLength);

        //If the upstream node does not have enough sequence, then we need to
        //look even further upstream.
        else
            upstreamNodeSequence = upstreamNode->getUpstreamSequence(upstreamSequenceLength - upstreamNodeFullSequence.length()) + upstreamNodeFullSequence;

        //If we now have enough sequence, then we can return it.
        if (upstreamNodeSequence.length() == upstreamSequenceLength)
            return upstreamNodeSequence;

        //If we don't have enough sequence, then we need to try the next
        //upstream node.  If our current one is the best so far, save that in
        //case no complete sequence is found.
        if (upstreamNodeSequence.length() > bestUpstreamNodeSequence.length())
            bestUpstreamNodeSequence = upstreamNodeSequence;
    }

    //If the code got here, that means that not enough upstream sequence was
    //found in any path!  Return what we have managed to get so far.
    return bestUpstreamNodeSequence;
}


int DeBruijnNode::getFullLength() const
{
    if (g_assemblyGraph->m_graphFileType != LAST_GRAPH)
        return getLength();
    else
        return getLength() + g_assemblyGraph->m_kmer - 1;
}


int DeBruijnNode::getLengthWithoutTrailingOverlap() const
{
    int length = getLength();
    std::vector<DeBruijnEdge *> leavingEdges = getLeavingEdges();

    if (leavingEdges.empty())
        return length;

    int maxOverlap = 0;
    for (auto &leavingEdge : leavingEdges)
    {
        int overlap = leavingEdge->getOverlap();
        if (overlap > maxOverlap)
            maxOverlap = overlap;
    }

    length -= maxOverlap;

    if (length < 0)
        length = 0;

    return length;
}


QString DeBruijnNode::getNodeNameForFasta(bool sign) const
{
    QString nodeNameForFasta;

    nodeNameForFasta += "NODE_";
    if (sign)
        nodeNameForFasta += getName();
    else
        nodeNameForFasta += getNameWithoutSign();

    nodeNameForFasta += "_length_";
    nodeNameForFasta += QByteArray::number(getLength());
    nodeNameForFasta += "_cov_";
    nodeNameForFasta += QByteArray::number(getDepth());

    return nodeNameForFasta;
}


//This function recursively labels all nodes as drawn that are within a
//certain distance of this node.  Whichever node called this will
//definitely be drawn, so that one is excluded from the recursive call.
void DeBruijnNode::labelNeighbouringNodesAsDrawn(int nodeDistance, DeBruijnNode * callingNode)
{
    if (m_highestDistanceInNeighbourSearch > nodeDistance)
        return;
    m_highestDistanceInNeighbourSearch = nodeDistance;

    if (nodeDistance == 0)
        return;

    DeBruijnNode * otherNode;
    for (auto &m_edge : m_edges)
    {
        otherNode = m_edge->getOtherNode(this);

        if (otherNode == callingNode)
            continue;

        if (g_settings->doubleMode)
            otherNode->m_drawn = true;
        else //single mode
        {
            if (otherNode->isPositiveNode())
                otherNode->m_drawn = true;
            else
                otherNode->getReverseComplement()->m_drawn = true;
        }
        otherNode->labelNeighbouringNodesAsDrawn(nodeDistance-1, this);
    }
}


bool DeBruijnNode::isPositiveNode() const
{
    QChar lastChar = m_name.at(m_name.length() - 1);
    return lastChar == '+';
}


bool DeBruijnNode::isNegativeNode() const
{
    QChar lastChar = m_name.at(m_name.length() - 1);
    return lastChar == '-';
}


//This function checks to see if the passed node leads into
//this node.  If so, it returns the connecting edge.  If not,
//it returns a null pointer.
DeBruijnEdge * DeBruijnNode::doesNodeLeadIn(DeBruijnNode * node) const
{
    for (auto edge : m_edges)
    {
        if (edge->getStartingNode() == node && edge->getEndingNode() == this)
            return edge;
    }
    return nullptr;
}

//This function checks to see if the passed node leads away from
//this node.  If so, it returns the connecting edge.  If not,
//it returns a null pointer.
DeBruijnEdge * DeBruijnNode::doesNodeLeadAway(DeBruijnNode * node) const
{
    for (auto edge : m_edges)
    {
        if (edge->getStartingNode() == this && edge->getEndingNode() == node)
            return edge;
    }
    return nullptr;
}


bool DeBruijnNode::isNodeConnected(DeBruijnNode * node) const
{
    for (auto edge : m_edges)
    {
        if (edge->getStartingNode() == node || edge->getEndingNode() == node)
            return true;
    }
    return false;
}



std::vector<DeBruijnEdge *> DeBruijnNode::getEnteringEdges() const
{
    std::vector<DeBruijnEdge *> returnVector;
    for (auto edge : m_edges)
    {
        if (this == edge->getEndingNode())
            returnVector.push_back(edge);
    }
    return returnVector;
}
std::vector<DeBruijnEdge *> DeBruijnNode::getLeavingEdges() const
{
    std::vector<DeBruijnEdge *> returnVector;
    for (auto edge : m_edges)
    {
        if (this == edge->getStartingNode())
            returnVector.push_back(edge);
    }
    return returnVector;
}



std::vector<DeBruijnNode *> DeBruijnNode::getDownstreamNodes() const
{
    std::vector<DeBruijnEdge *> leavingEdges = getLeavingEdges();

    std::vector<DeBruijnNode *> returnVector;
    returnVector.reserve(leavingEdges.size());
    for (auto &leavingEdge : leavingEdges)
        returnVector.push_back(leavingEdge->getEndingNode());

    return returnVector;
}


std::vector<DeBruijnNode *> DeBruijnNode::getUpstreamNodes() const
{
    std::vector<DeBruijnEdge *> enteringEdges = getEnteringEdges();

    std::vector<DeBruijnNode *> returnVector;
    returnVector.reserve(enteringEdges.size());
    for (auto &enteringEdge : enteringEdges)
        returnVector.push_back(enteringEdge->getStartingNode());

    return returnVector;
}



double DeBruijnNode::getNodeLengthPerMegabase()
{
    if (g_settings->nodeLengthMode == AUTO_NODE_LENGTH)
        return g_settings->autoNodeLengthPerMegabase;
    else
        return g_settings->manualNodeLengthPerMegabase;
}



bool DeBruijnNode::isInDepthRange(double min, double max) const
{
    return m_depth >= min && m_depth <= max;
}


bool DeBruijnNode::sequenceIsMissing() const
{
    return m_sequence.empty() || m_sequence.missing();
}


const Sequence &DeBruijnNode::getSequence() const
{
    return m_sequence;
}

Sequence &DeBruijnNode::getSequence()
{
    return m_sequence;
}

//If the node has an edge which leads to itself (creating a loop), this function
//will return it.  Otherwise, it returns 0.
DeBruijnEdge * DeBruijnNode::getSelfLoopingEdge() const
{
    for (auto edge : m_edges)
    {
        if (edge->getStartingNode() == this && edge->getEndingNode() == this)
            return edge;
    }

    return nullptr;
}


//This function returns either 0, 1 or 2.  A node with connections on both ends
//(i.e. has both incoming and outgoing edges) returns 0.  A node with no edges
//returns 2.  A node with either incoming or outgoing edges returns 1.
int DeBruijnNode::getDeadEndCount() const
{
    if (m_edges.empty())
        return 2;

    std::vector<DeBruijnEdge *> enteringEdges = getEnteringEdges();
    std::vector<DeBruijnEdge *> leavingEdges = getLeavingEdges();

    if (!enteringEdges.empty() && !leavingEdges.empty())
        return 0;
    else
        return 1;
}




//This function returns all of the positive nodes that this node (or its
//reverse complement) are connected to.
std::vector<DeBruijnNode *> DeBruijnNode::getAllConnectedPositiveNodes() const
{
    QSet<DeBruijnNode *> connectedPositiveNodesSet;

    for (auto edge : m_edges)
    {
        DeBruijnNode * connectedNode = edge->getOtherNode(this);
        if (connectedNode->isNegativeNode())
            connectedNode = connectedNode->getReverseComplement();

        connectedPositiveNodesSet.insert(connectedNode);
    }

    std::vector<DeBruijnNode *> connectedPositiveNodesVector;
    QSetIterator<DeBruijnNode *> i(connectedPositiveNodesSet);
    while (i.hasNext())
        connectedPositiveNodesVector.push_back(i.next());

    return connectedPositiveNodesVector;
}

float DeBruijnNode::getGC() const {
    size_t gc = 0;
    for (size_t i = 0; i < m_sequence.size(); ++i) {
        char c = m_sequence[i];
        gc += (c == 'G' || c == 'C');
    }

    return float(gc) / float(m_sequence.size());
}
