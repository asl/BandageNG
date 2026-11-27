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

#include "assemblygraph.h"
#include "debruijnedge.h"
#include "sequenceutils.h"

#include "program/globals.h"
#include "program/settings.h"

#include <thirdparty/seq/aa.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <unordered_set>

static_assert(sizeof(OrientedNode) == sizeof(void *),
              "OrientedNode must stay pointer-sized");

OrientedNode DirectedEdge::otherNode() const {
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
                                           OrientedNode origin,
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

IncidentEdgeIterator OrientedNode::outgoing_begin() const {
    return IncidentEdgeIterator(node()->edgeBegin(), node()->edgeEnd(),
                                *this, IncidentFilter::Outgoing, false);
}

IncidentEdgeIterator OrientedNode::outgoing_end() const {
    return IncidentEdgeIterator(node()->edgeBegin(), node()->edgeEnd(),
                                *this, IncidentFilter::Outgoing, true);
}

IncidentEdgeIterator OrientedNode::incoming_begin() const {
    return IncidentEdgeIterator(node()->edgeBegin(), node()->edgeEnd(),
                                *this, IncidentFilter::Incoming, false);
}

IncidentEdgeIterator OrientedNode::incoming_end() const {
    return IncidentEdgeIterator(node()->edgeBegin(), node()->edgeEnd(),
                                *this, IncidentFilter::Incoming, true);
}

IncidentEdgeIterator OrientedNode::incident_begin() const {
    return IncidentEdgeIterator(node()->edgeBegin(), node()->edgeEnd(),
                                *this, IncidentFilter::Both, false);
}

IncidentEdgeIterator OrientedNode::incident_end() const {
    return IncidentEdgeIterator(node()->edgeBegin(), node()->edgeEnd(),
                                *this, IncidentFilter::Both, true);
}

DeBruijnNode::DeBruijnNode(std::string_view name, float depth, const Sequence &sequence, unsigned length)
        : m_name(name),
          m_sequence(sequence),
          m_depth(depth) {
    assert(!name.empty());
    const size_t stored = length > 0 ? length : sequence.size();
    assert(stored <= MaxLength);
    setStoredLength(static_cast<unsigned>(stored));
    refreshSelfComplement();
}

void DeBruijnNode::setStoredLength(unsigned length) {
    assert(length <= LengthMask);
    m_lengthAndSpecial = (m_lengthAndSpecial & ~LengthMask) | (length & LengthMask);
}

void DeBruijnNode::refreshSelfComplement() {
    const bool selfComplement = sequenceIsSelfComplement(m_sequence);
    if (selfComplement) {
        if (graphicsBit(DrawnNegative))
            setGraphicsBit(DrawnPositive, true);
        setGraphicsBit(DrawnNegative, false);
        if (specialBit(SpecialNegative))
            setSpecialBit(SpecialPositive, true);
        setSpecialBit(SpecialNegative, false);
    }
    setGraphicsBit(SelfComplement, selfComplement);
}

void DeBruijnNode::setName(std::string_view newName) {
    if (!newName.empty() && (newName.back() == '+' || newName.back() == '-'))
        newName.remove_suffix(1);
    if (newName.empty())
        return;
    m_name.assign(newName.data(), newName.size());
}

void DeBruijnNode::setSequence(const QByteArray &newSeq) {
    setSequence(Sequence(newSeq));
}

void DeBruijnNode::setSequence(const Sequence &newSeq) {
    m_sequence = newSeq;
    const size_t stored = m_sequence.size();
    assert(stored <= MaxLength);
    setStoredLength(static_cast<unsigned>(stored));
    refreshSelfComplement();
}

uint64_t DeBruijnNode::nameHeapBytes() const {
    const char *data = m_name.data();
    const char *object = reinterpret_cast<const char *>(&m_name);
    if (data >= object && data < object + sizeof(m_name))
        return 0;
    return m_name.capacity() + 1;
}

uint64_t DeBruijnNode::edgeListHeapBytes() const {
    const char *storage = reinterpret_cast<const char *>(m_edges.cdata());
    const char *self = reinterpret_cast<const char *>(&m_edges);
    if (storage == nullptr || (storage >= self && storage < self + sizeof(m_edges)))
        return 0;
    uint64_t bytes = m_edges.capacity() * sizeof(DeBruijnEdge *);
    // More than seven pointers is a heap std::vector. Shorter spills are one array.
    if (m_edges.size() > 7)
        bytes += sizeof(std::vector<DeBruijnEdge *>);
    return bytes;
}

void DeBruijnNode::addEdge(DeBruijnEdge *edge) {
    if (std::find(m_edges.begin(), m_edges.end(), edge) == m_edges.end())
        m_edges.push_back(edge);
}

void DeBruijnNode::removeEdge(DeBruijnEdge *edge) {
    m_edges.erase(std::remove(m_edges.begin(), m_edges.end(), edge), m_edges.end());
}

void DeBruijnNode::resetNode() {
    m_graphicsItem.setPointer(nullptr);
    setGraphicsBit(DrawnPositive, false);
    setGraphicsBit(DrawnNegative, false);
    setSpecialBit(SpecialPositive, false);
    setSpecialBit(SpecialNegative, false);
}

bool DeBruijnNode::sequenceIsMissing() const {
    return m_sequence.empty() || m_sequence.missing();
}

float DeBruijnNode::getGC() const {
    size_t gc = 0;
    for (size_t i = 0; i < m_sequence.size(); ++i) {
        char c = m_sequence[i];
        gc += (c == 'G' || c == 'C');
    }
    return float(gc) / float(m_sequence.size());
}

std::string OrientedNode::getName() const {
    std::string name(getNameWithoutSign());
    name.push_back(getSign());
    return name;
}

GraphicsItemNode *OrientedNode::getGraphicsItemNode() const {
    if (!node())
        return nullptr;
    if (!negative())
        return node()->getGraphicsItemNode();
    return g_assemblyGraph->negativeNodeGraphics(node());
}

void OrientedNode::setGraphicsItemNode(GraphicsItemNode *item) const {
    if (!node())
        return;
    if (!negative()) {
        node()->setGraphicsItemNode(item);
        return;
    }
    g_assemblyGraph->setNegativeNodeGraphics(node(), item);
}

static QByteArray nodeNameForFasta(OrientedNode node, bool sign) {
    const std::string nodeName = sign ? node.getName() : std::string(node.getNameWithoutSign());
    // NODE_, a 10-character name, _length_, 10 digits, _cov_, and the depth.
    // resize(0) keeps that allocation as capacity.
    QByteArray fastaName(5 + 10 + 8 + 10 + 5 + 12, Qt::Uninitialized);
    fastaName.resize(0);
    fastaName += "NODE_";
    fastaName.append(nodeName.data(), int(nodeName.size()));
    fastaName += "_length_";
    fastaName += QByteArray::number(node.getLength());
    fastaName += "_cov_";
    fastaName += QByteArray::number(node.getDepth());
    return fastaName;
}

QByteArray OrientedNode::getFasta(bool sign, bool newLines, bool evenIfEmpty) const {
    QByteArray sequence = utils::sequenceToQByteArray(getSequence());
    if (sequence.isEmpty() && !evenIfEmpty)
        return {};

    QByteArray fasta = ">";
    fasta += nodeNameForFasta(*this, sign);
    fasta += "\n";
    if (newLines)
        fasta += utils::addNewlinesToSequence(sequence);
    else {
        fasta += sequence;
        fasta += "\n";
    }
    return fasta;
}

QByteArray OrientedNode::getAAFasta(unsigned shift, bool sign, bool newLines, bool evenIfEmpty) const {
    QByteArray sequence(utils::sequenceToQByteArray(getSequence()));
    if (sequence.isEmpty() && !evenIfEmpty)
        return {};

    if (!sequence.isEmpty())
        sequence = aa::translate(sequence.data() + shift).c_str();

    QByteArray fasta = ">";
    fasta += nodeNameForFasta(*this, sign) + "/";
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

unsigned OrientedNode::getLengthWithoutTrailingOverlap() const {
    unsigned length = getLength();
    auto leaving = outgoingEdges(*this);
    if (leaving.empty())
        return length;

    int maxOverlap = 0;
    for (auto link : leaving)
        maxOverlap = std::max(maxOverlap, link.overlap());

    if (maxOverlap > static_cast<int>(length))
        return 0;
    return length - maxOverlap;
}

void OrientedNode::labelNeighbouringNodesAsDrawn(int nodeDistance) const {
    std::unordered_set<OrientedNode> worklist, seen;
    worklist.insert(*this);
    for (int depth = 0; depth <= nodeDistance; ++depth) {
        for (OrientedNode node : worklist) {
            OrientedNode nodeToMark = g_settings->doubleMode ? node : node.getCanonical();
            nodeToMark.setAsDrawn();

            for (auto link : incidentEdges(node)) {
                OrientedNode otherNode = link.otherNode();
                if (otherNode && !otherNode.thisNodeOrReverseComplementIsDrawn())
                    seen.insert(otherNode);
            }
        }

        if (seen.empty())
            break;
        worklist.clear();
        worklist.swap(seen);
    }
}

DeBruijnEdge *OrientedNode::doesNodeLeadIn(OrientedNode node) const {
    for (DeBruijnEdge *edge : edges()) {
        if (edge->connects(node, *this))
            return edge;
    }
    return nullptr;
}

DeBruijnEdge *OrientedNode::doesNodeLeadAway(OrientedNode node) const {
    for (DeBruijnEdge *edge : edges()) {
        if (edge->connects(*this, node))
            return edge;
    }
    return nullptr;
}

bool OrientedNode::isNodeConnected(OrientedNode node) const {
    for (DeBruijnEdge *edge : edges()) {
        if (edge->connects(node, *this) || edge->connects(*this, node))
            return true;
    }
    return false;
}

std::vector<OrientedNode> OrientedNode::getDownstreamNodes() const {
    std::vector<OrientedNode> nodes;
    for (auto link : outgoingEdges(*this))
        nodes.push_back(link.otherNode());
    return nodes;
}

std::vector<OrientedNode> OrientedNode::getUpstreamNodes() const {
    std::vector<OrientedNode> nodes;
    for (auto link : incomingEdges(*this))
        nodes.push_back(link.otherNode());
    return nodes;
}

DeBruijnEdge *OrientedNode::getSelfLoopingEdge() const {
    for (DeBruijnEdge *edge : edges()) {
        if (edge->connects(*this, *this))
            return edge;
    }
    return nullptr;
}

int OrientedNode::getDeadEndCount() const {
    if (edges().empty())
        return 2;
    if (!incomingEdges(*this).empty() && !outgoingEdges(*this).empty())
        return 0;
    return 1;
}

std::vector<OrientedNode> OrientedNode::getAllConnectedPositiveNodes() const {
    std::unordered_set<OrientedNode> connected;
    for (auto link : incidentEdges(*this)) {
        OrientedNode other = link.otherNode();
        if (!other)
            continue;
        connected.insert(other.getCanonical());
    }
    return {connected.begin(), connected.end()};
}
