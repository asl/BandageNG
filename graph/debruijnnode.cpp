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

#include <cassert>
#include <cmath>

DeBruijnNode *DirectedEdge::otherNode() const {
    return m_outgoing ? m_edge->downstream(m_origin) : m_edge->upstream(m_origin);
}

int DirectedEdge::overlap() const {
    return m_edge->getOverlap();
}

EdgeOverlapType DirectedEdge::overlapType() const {
    return m_edge->getOverlapType();
}

bool IncidentEdgeIterator::onOutgoingPass() const {
    return m_filter == IncidentFilter::Outgoing ||
           (m_filter == IncidentFilter::Both && m_outgoingPass);
}

void IncidentEdgeIterator::skip() {
    auto wanted = [&](DeBruijnEdge *edge) {
        return onOutgoingPass() ? edge->leaves(m_origin) : edge->enters(m_origin);
    };
    while (m_it != m_end && !wanted(*m_it))
        ++m_it;
    if (m_it == m_end && m_filter == IncidentFilter::Both && m_outgoingPass) {
        m_outgoingPass = false;
        m_it = m_begin;
        while (m_it != m_end && !wanted(*m_it))
            ++m_it;
    }
}

IncidentEdgeIterator::IncidentEdgeIterator(IncidentEdgeIterator::EdgeIt begin,
                                           IncidentEdgeIterator::EdgeIt end,
                                           const DeBruijnNode *origin,
                                           IncidentFilter filter, bool isEnd)
        : m_it(isEnd ? end : begin), m_begin(begin), m_end(end), m_origin(origin),
          m_filter(filter), m_outgoingPass(filter != IncidentFilter::Both || !isEnd) {
    if (!isEnd)
        skip();
}

DirectedEdge IncidentEdgeIterator::operator*() const {
    return DirectedEdge(*m_it, m_origin, onOutgoingPass());
}

IncidentEdgeIterator &IncidentEdgeIterator::operator++() {
    ++m_it;
    skip();
    return *this;
}

OutgoingEdgeIterator DeBruijnNode::outgoing_begin() const {
    return OutgoingEdgeIterator(edgeBegin(), edgeEnd(), this, IncidentFilter::Outgoing, false);
}

OutgoingEdgeIterator DeBruijnNode::outgoing_end() const {
    return OutgoingEdgeIterator(edgeBegin(), edgeEnd(), this, IncidentFilter::Outgoing, true);
}

IncomingEdgeIterator DeBruijnNode::incoming_begin() const {
    return IncomingEdgeIterator(edgeBegin(), edgeEnd(), this, IncidentFilter::Incoming, false);
}

IncomingEdgeIterator DeBruijnNode::incoming_end() const {
    return IncomingEdgeIterator(edgeBegin(), edgeEnd(), this, IncidentFilter::Incoming, true);
}

IncidentEdgeIterator DeBruijnNode::incident_begin() const {
    return IncidentEdgeIterator(edgeBegin(), edgeEnd(), this, IncidentFilter::Both, false);
}

IncidentEdgeIterator DeBruijnNode::incident_end() const {
    return IncidentEdgeIterator(edgeBegin(), edgeEnd(), this, IncidentFilter::Both, true);
}

#include <set>
#include <string>
#include <unordered_set>
#include <QApplication>
#include <QSet>

//The length parameter is optional.  If it is set, then the node will use that
//for its length.  If not set, it will just use the sequence length.
DeBruijnNode::DeBruijnNode(std::string_view name, bool positive, float depth, const Sequence& sequence, unsigned length)
        : m_name(positive ? std::string(name) : std::string()),
          m_depth(depth),
          m_sequence(sequence),
          m_reverseComplement(nullptr),
          m_graphicsItemNode(nullptr),
          m_specialNode(false),
          m_drawn(false) {
    if (positive)
        assert(!name.empty());
    m_length = length > 0 ? length : sequence.size();
}

std::string DeBruijnNode::getName() const {
    std::string name(getNameWithoutSign());
    name.push_back(getSign());
    return name;
}

std::string_view DeBruijnNode::getNameWithoutSign() const {
    if (isPositiveNode())
        return m_name;
    if (m_reverseComplement != nullptr)
        return m_reverseComplement->m_name;
    return {};
}

void DeBruijnNode::setName(std::string_view newName) {
    if (newName.empty())
        return;

    DeBruijnNode *positive = isPositiveNode() ? this : m_reverseComplement;
    if (positive == nullptr)
        return;

    positive->m_name.assign(newName.data(), newName.size());
    if (positive->m_reverseComplement != nullptr && positive->m_reverseComplement != positive)
        positive->m_reverseComplement->m_name.clear();
}


//This function adds an edge to the Node, but only if the edge hasn't already
//been added.
void DeBruijnNode::addEdge(DeBruijnEdge *edge) {
    if (std::find(m_edges.begin(), m_edges.end(), edge) == m_edges.end())
        m_edges.push_back(edge);
}


//This function deletes an edge from the node, if it exists.
void DeBruijnNode::removeEdge(DeBruijnEdge *edge) {
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
    std::vector<DeBruijnNode *> upstreamNodes = getUpstreamNodes();

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
    auto leaving = outgoing_edges();
    if (leaving.empty())
        return length;

    int maxOverlap = 0;
    for (auto link : leaving)
        maxOverlap = std::max(maxOverlap, link.overlap());

    if (maxOverlap > static_cast<int>(length))
        return 0;

    return length - maxOverlap;
}


QByteArray DeBruijnNode::getNodeNameForFasta(bool sign) const
{
    QByteArray nodeNameForFasta;

    nodeNameForFasta += "NODE_";
    const auto nodeName = sign ? getName() : std::string(getNameWithoutSign());
    nodeNameForFasta += QByteArray(nodeName.data(), int(nodeName.size()));

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
    std::unordered_set<DeBruijnNode *> worklist, seen;
    worklist.insert(this);
    for (int depth = 0; depth <= nodeDistance; depth++) {
        for (auto *node : worklist) {
            auto *nodeToMark =
                        g_settings->doubleMode ? node : node->getCanonical();
            nodeToMark->m_drawn = true;

            for (auto link : node->incident_edges()) {
                DeBruijnNode *otherNode = link.otherNode();
                if (otherNode != nullptr && !otherNode->thisNodeOrReverseComplementIsDrawn())
                    seen.insert(otherNode);
            }
        }

        if (seen.empty())
            break;
        worklist.clear();
        worklist.swap(seen);
    }
}

//This function checks to see if the passed node leads into
//this node.  If so, it returns the connecting edge.  If not,
//it returns a null pointer.
DeBruijnEdge *DeBruijnNode::doesNodeLeadIn(DeBruijnNode *node) const
{
    for (auto *edge : m_edges) {
        if (edge->connects(node, this))
            return edge;
    }
    return nullptr;
}

//This function checks to see if the passed node leads away from
//this node.  If so, it returns the connecting edge.  If not,
//it returns a null pointer.
DeBruijnEdge *DeBruijnNode::doesNodeLeadAway(DeBruijnNode *node) const
{
    for (auto *edge : m_edges) {
        if (edge->connects(this, node))
            return edge;
    }
    return nullptr;
}


bool DeBruijnNode::isNodeConnected(DeBruijnNode *node) const {
    for (auto *edge : m_edges) {
        if (edge->connects(node, this) || edge->connects(this, node))
            return true;
    }
    return false;
}



std::vector<DeBruijnNode *> DeBruijnNode::getDownstreamNodes() const {
    std::vector<DeBruijnNode *> nodes;
    for (auto link : outgoing_edges())
        nodes.push_back(link.otherNode());
    return nodes;
}

std::vector<DeBruijnNode *> DeBruijnNode::getUpstreamNodes() const {
    std::vector<DeBruijnNode *> nodes;
    for (auto link : incoming_edges())
        nodes.push_back(link.otherNode());
    return nodes;
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
DeBruijnEdge *DeBruijnNode::getSelfLoopingEdge() const
{
    for (auto edge : m_edges)
    {
        if (edge->connects(this, this))
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

    if (!incoming_edges().empty() && !outgoing_edges().empty())
        return 0;
    return 1;
}




//This function returns all of the positive nodes that this node (or its
//reverse complement) are connected to.
std::vector<DeBruijnNode *> DeBruijnNode::getAllConnectedPositiveNodes() const
{
    std::unordered_set<DeBruijnNode *> connectedPositiveNodesSet;

    for (auto link : incident_edges()) {
        DeBruijnNode *connectedNode = link.otherNode();
        if (connectedNode == nullptr)
            continue;
        if (connectedNode->isNegativeNode())
            connectedNode = connectedNode->getReverseComplement();
        if (connectedNode != nullptr)
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
