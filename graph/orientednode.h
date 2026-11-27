// Copyright 2026 Anton Korobeynikov

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

// Included from debruijnnode.h once DeBruijnNode is complete. PointerIntPair
// needs the alignment of that type.

#pragma once

#include "program/llvm.h"
#include "seq/sequence.hpp"

#include <QByteArray>
#include <QHash>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class DeBruijnNode;
class DeBruijnEdge;
class GraphicsItemNode;
class IncidentEdgeIterator;

// A node, plus the strand in the pointer's low bit. A node whose sequence
// is its own reverse complement has one orientation: the bit is ignored,
// and reverse() does not flip it. An ordinary node flips the bit, so the
// two orientations share the object and compare unequal.
class OrientedNode {
    bandage::PointerIntPair<DeBruijnNode *, 1, bool> m_ref;

public:
    OrientedNode() = default;
    OrientedNode(std::nullptr_t) {}
    explicit OrientedNode(DeBruijnNode *node) : OrientedNode(node, false) {}
    OrientedNode(DeBruijnNode *node, bool negative);

    // The node that carries the sequence. The strand is the bit on this
    // pointer, not a second object.
    DeBruijnNode *node() const { return m_ref.getPointer(); }
    explicit operator bool() const { return node() != nullptr; }

    // False when the sequence is its own reverse complement, even if a
    // stored tag still has the bit set from before that was known.
    bool negative() const;
    bool positive() const { return !negative(); }
    bool isPositiveNode() const { return positive(); }
    bool isNegativeNode() const { return negative(); }
    // False when this is empty. A palindrome has one orientation.
    bool isSelfComplement() const;

    OrientedNode reverse() const;
    OrientedNode getReverseComplement() const { return reverse(); }
    // Positive orientation of this node. A palindrome is already that.
    OrientedNode getCanonical() const;

    std::string getName() const;
    std::string_view getNameWithoutSign() const;
    char getSign() const { return negative() ? '-' : '+'; }

    // Stored sequence for the positive orientation. The negative
    // orientation is the reverse complement, returned by value.
    Sequence getSequence() const;
    char getBaseAt(int i) const;

    double getDepth() const;
    void setDepth(double depth) const;
    unsigned getLength() const;
    unsigned getLengthWithoutTrailingOverlap() const;
    float getGC() const;
    bool sequenceIsMissing() const;

    QByteArray getFasta(bool sign, bool newLines = true, bool evenIfEmpty = true) const;
    QByteArray getAAFasta(unsigned shift, bool sign, bool newLines, bool evenIfEmpty) const;

    bool isDrawn() const;
    bool isNotDrawn() const { return !isDrawn(); }
    void setAsDrawn() const;
    void setAsNotDrawn() const;
    bool thisNodeOrReverseComplementIsDrawn() const;
    bool isSpecialNode() const;
    void setAsSpecial() const;
    void setAsNotSpecial() const;

    GraphicsItemNode *getGraphicsItemNode() const;
    bool hasGraphicsItem() const { return getGraphicsItemNode() != nullptr; }
    void setGraphicsItemNode(GraphicsItemNode *item) const;

    // Every edge stored on this node, both strands. Direction is on the edge ends.
    auto edges() const { return node()->edges(); }

    IncidentEdgeIterator outgoing_begin() const;
    IncidentEdgeIterator outgoing_end() const;
    IncidentEdgeIterator incoming_begin() const;
    IncidentEdgeIterator incoming_end() const;
    IncidentEdgeIterator incident_begin() const;
    IncidentEdgeIterator incident_end() const;

    std::vector<OrientedNode> getDownstreamNodes() const;
    std::vector<OrientedNode> getUpstreamNodes() const;
    std::vector<OrientedNode> getAllConnectedPositiveNodes() const;

    bool isNodeConnected(OrientedNode node) const;
    DeBruijnEdge *doesNodeLeadIn(OrientedNode node) const;
    DeBruijnEdge *doesNodeLeadAway(OrientedNode node) const;
    DeBruijnEdge *getSelfLoopingEdge() const;
    int getDeadEndCount() const;
    void labelNeighbouringNodesAsDrawn(int nodeDistance) const;

    friend bool operator==(OrientedNode a, OrientedNode b);
    friend bool operator!=(OrientedNode a, OrientedNode b) { return !(a == b); }
    friend bool operator==(OrientedNode node, std::nullptr_t) { return node.node() == nullptr; }
    friend bool operator!=(OrientedNode node, std::nullptr_t) { return node.node() != nullptr; }
    friend bool operator==(std::nullptr_t, OrientedNode node) { return node.node() == nullptr; }
    friend bool operator!=(std::nullptr_t, OrientedNode node) { return node.node() != nullptr; }

    friend size_t hash_value(OrientedNode node);
};

inline OrientedNode::OrientedNode(DeBruijnNode *node, bool negative) {
    if (node != nullptr && node->isSelfComplement())
        negative = false;
    m_ref.setPointerAndInt(node, negative);
}

inline bool OrientedNode::negative() const {
    DeBruijnNode *stored = node();
    if (stored == nullptr || stored->isSelfComplement())
        return false;
    return m_ref.getInt();
}

inline bool OrientedNode::isSelfComplement() const {
    DeBruijnNode *stored = node();
    return stored != nullptr && stored->isSelfComplement();
}

inline OrientedNode OrientedNode::reverse() const {
    if (node() == nullptr || node()->isSelfComplement())
        return OrientedNode(node(), false);
    return OrientedNode(node(), !m_ref.getInt());
}

inline OrientedNode OrientedNode::getCanonical() const {
    return negative() ? reverse() : *this;
}

inline std::string_view OrientedNode::getNameWithoutSign() const {
    return node()->getNameWithoutSign();
}

inline Sequence OrientedNode::getSequence() const {
    const Sequence &stored = node()->getSequence();
    if (!negative() || stored.empty() || stored.missing())
        return stored;
    return stored.GetReverseComplement();
}

inline char OrientedNode::getBaseAt(int i) const {
    if (!negative())
        return node()->getBaseAt(i);
    Sequence reversed = getSequence();
    if (i < 0 || i >= int(reversed.size()))
        return '\0';
    return reversed[size_t(i)];
}

inline double OrientedNode::getDepth() const { return node()->getDepth(); }
inline void OrientedNode::setDepth(double depth) const { node()->setDepth(depth); }
inline unsigned OrientedNode::getLength() const { return node()->getLength(); }
inline float OrientedNode::getGC() const { return node()->getGC(); }
inline bool OrientedNode::sequenceIsMissing() const { return node()->sequenceIsMissing(); }

inline bool OrientedNode::isDrawn() const { return node()->isDrawn(negative()); }
inline void OrientedNode::setAsDrawn() const { node()->setDrawn(negative(), true); }
inline void OrientedNode::setAsNotDrawn() const { node()->setDrawn(negative(), false); }
inline bool OrientedNode::thisNodeOrReverseComplementIsDrawn() const {
    return isDrawn() || reverse().isDrawn();
}
inline bool OrientedNode::isSpecialNode() const { return node()->isSpecial(negative()); }
inline void OrientedNode::setAsSpecial() const { node()->setSpecial(negative(), true); }
inline void OrientedNode::setAsNotSpecial() const { node()->setSpecial(negative(), false); }

inline bool operator<(OrientedNode a, OrientedNode b) {
    if (a.node() != b.node())
        return a.node() < b.node();
    return a.negative() < b.negative();
}

inline bool operator==(OrientedNode a, OrientedNode b) {
    if (a.node() != b.node())
        return false;
    if (!a || a.isSelfComplement())
        return true;
    return a.m_ref.getInt() == b.m_ref.getInt();
}

inline size_t hash_value(OrientedNode node) {
    auto bits = reinterpret_cast<uintptr_t>(node.node());
    if (node && !node.isSelfComplement() && node.m_ref.getInt())
        bits ^= uintptr_t{0x9e3779b97f4a7c15ULL};
    return size_t(bits);
}

inline size_t qHash(OrientedNode node, size_t seed = 0) noexcept {
    return qHash(hash_value(node), seed);
}

namespace std {
template<>
struct hash<OrientedNode> {
    size_t operator()(OrientedNode node) const noexcept { return hash_value(node); }
};
} // namespace std

// Strand of a name that is already in hand. Iterating the node trie does not
// use this: both signed keys store the same pointer, and the strand is in
// the name.
inline OrientedNode orientationOf(std::string_view signedName, DeBruijnNode *node) {
    const bool negative = !signedName.empty() && signedName.back() == '-';
    return OrientedNode(node, negative);
}
