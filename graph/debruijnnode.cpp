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

#include "program/settings.h"

#include <thirdparty/seq/aa.hpp>

#include <cmath>

#include <set>
#include <string>
#include <unordered_set>
#include <QApplication>
#include <QSet>

//The length parameter is optional.  If it is set, then the node will use that
//for its length.  If not set, it will just use the sequence length.
DeBruijnNode::DeBruijnNode(QString name, float depth, const Sequence& sequence, unsigned length)
        : m_name(std::move(name)),
          m_depth(depth),
          m_sequence(sequence),
          m_reverseComplement(nullptr),
          m_graphicsItemNode(nullptr),
          m_specialNode(false),
          m_drawn(false) {
    m_length = length > 0 ? length : sequence.size();
}


//This function adds an edge to the Node, but only if the edge hasn't already
//been added.
void DeBruijnNode::addEdge(DeBruijnEdge * edge) {
    if (std::find(m_edges.begin(), m_edges.end(), edge) == m_edges.end())
        m_edges.push_back(edge);
}


//This function deletes an edge from the node, if it exists.
void DeBruijnNode::removeEdge(DeBruijnEdge * edge) {
    m_edges.erase(std::remove(m_edges.begin(), m_edges.end(), edge), m_edges.end());
}


//This function resets the node to the state it would be in after a graph
//file was loaded - no contiguity status and no OGDF nodes.
void DeBruijnNode::resetNode()
{
    m_graphicsItemNode = nullptr;
    setAsNotDrawn();
    setAsNotSpecial();
}

QByteArray DeBruijnNode::getFasta(bool sign, bool newLines, bool evenIfEmpty) const {
    QByteArray sequence = utils::sequenceToQByteArray(getSequence());
    if (sequence.isEmpty() && !evenIfEmpty)
        return {};

    QByteArray fasta = ">";
    fasta += getNodeNameForFasta(sign);
    fasta += "\n";
    if (newLines)
        fasta += utils::addNewlinesToSequence(sequence);
    else {
        fasta += sequence;
        fasta += "\n";
    }
    return fasta;
}

QByteArray DeBruijnNode::getAAFasta(unsigned shift, bool sign, bool newLines, bool evenIfEmpty) const {
    QByteArray sequence(utils::sequenceToQByteArray(getSequence()));
    if (sequence.isEmpty() && !evenIfEmpty)
        return {};

    if (!sequence.isEmpty())
        sequence = aa::translate(sequence.data() + shift).c_str();

    QByteArray fasta = ">";
    fasta += getNodeNameForFasta(sign) + "/";
    fasta += std::to_string(shift);
    fasta += "\n";
    if (newLines)
        fasta += utils::addNewlinesToSequence(sequence);
    else {
        fasta += sequence;
        fasta += "\n";
    }
    return fasta;
}

QByteArray DeBruijnNode::getUpstreamSequence(int upstreamSequenceLength) const {
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


unsigned DeBruijnNode::getLengthWithoutTrailingOverlap() const {
    unsigned length = getLength();
    std::vector<DeBruijnEdge *> leavingEdges = getLeavingEdges();

    if (leavingEdges.empty())
        return length;

    int maxOverlap = 0;
    for (const auto *leavingEdge : leavingEdges)
        maxOverlap = std::max(maxOverlap, leavingEdge->getOverlap());

    if (maxOverlap > length)
        return 0;
    
    return length - maxOverlap;
}


QByteArray DeBruijnNode::getNodeNameForFasta(bool sign) const
{
    QByteArray nodeNameForFasta;

    nodeNameForFasta += "NODE_";
    nodeNameForFasta += sign ? qPrintable(getName()) : qPrintable(getNameWithoutSign());

    nodeNameForFasta += "_length_";
    nodeNameForFasta += QByteArray::number(getLength());
    nodeNameForFasta += "_cov_";
    nodeNameForFasta += QByteArray::number(getDepth());

    return nodeNameForFasta;
}


// This function recursively labels all nodes as drawn that are within a
// certain distance of this node.  Whichever node called this will
// definitely be drawn, so that one is excluded from the recursive call.
// FIXME: this function does not belong here
void DeBruijnNode::labelNeighbouringNodesAsDrawn(int nodeDistance) {
    std::unordered_set<DeBruijnNode*> worklist, seen;
    worklist.insert(this);
    for (int depth = 0; depth <= nodeDistance; depth++) {
        for (auto *node : worklist) {
            auto *nodeToMark =
                        g_settings->doubleMode ? node : node->getCanonical();
            nodeToMark->setAsDrawn();

            for (auto *edge : node->m_edges) {
                if (edge->getOverlapType() == EdgeOverlapType::EXTRA_LINK)
                    continue;
                DeBruijnNode *otherNode = edge->getOtherNode(node);
                if (!otherNode->thisNodeOrReverseComplementIsDrawn())
                    seen.insert(otherNode);
            }
        }

        if (seen.empty())
            break;
        worklist.clear();
        worklist.swap(seen);
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
    for (auto *edge : m_edges) {
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
    for (auto *edge : m_edges) {
        if (edge->getStartingNode() == this && edge->getEndingNode() == node)
            return edge;
    }
    return nullptr;
}


bool DeBruijnNode::isNodeConnected(DeBruijnNode * node) const {
    for (auto *edge : m_edges) {
        if (edge->getStartingNode() == node || edge->getEndingNode() == node)
            return true;
    }
    return false;
}



std::vector<DeBruijnEdge *> DeBruijnNode::getEnteringEdges() const
{
    std::vector<DeBruijnEdge *> returnVector;
    for (auto *edge : m_edges) {
        if (this == edge->getEndingNode())
            returnVector.push_back(edge);
    }
    return returnVector;
}
std::vector<DeBruijnEdge *> DeBruijnNode::getLeavingEdges() const
{
    std::vector<DeBruijnEdge *> returnVector;
    for (auto *edge : m_edges) {
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
    for (auto *leavingEdge : leavingEdges)
        returnVector.push_back(leavingEdge->getEndingNode());

    return returnVector;
}


std::vector<DeBruijnNode *> DeBruijnNode::getUpstreamNodes() const
{
    std::vector<DeBruijnEdge *> enteringEdges = getEnteringEdges();

    std::vector<DeBruijnNode *> returnVector;
    returnVector.reserve(enteringEdges.size());
    for (auto *enteringEdge : enteringEdges)
        returnVector.push_back(enteringEdge->getStartingNode());

    return returnVector;
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
    std::unordered_set<DeBruijnNode *> connectedPositiveNodesSet;

    for (auto *edge : m_edges) {
        DeBruijnNode * connectedNode = edge->getOtherNode(this);
        if (connectedNode->isNegativeNode())
            connectedNode = connectedNode->getReverseComplement();

        connectedPositiveNodesSet.insert(connectedNode);
    }

    return { connectedPositiveNodesSet.begin(), connectedPositiveNodesSet.end() };
}

float DeBruijnNode::getGC() const {
    size_t gc = 0;
    for (size_t i = 0; i < m_sequence.size(); ++i) {
        char c = m_sequence[i];
        gc += (c == 'G' || c == 'C');
    }

    return float(gc) / float(m_sequence.size());
}
