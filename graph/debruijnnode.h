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

#pragma once

#include "program/llvm.h"
#include "seq/sequence.hpp"
#include "small_vector/small_pod_vector.hpp"

#include <QByteArray>
#include <QColor>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class DeBruijnEdge;
class GraphicsItemNode;

// Last character of a Bandage node key is the strand suffix. The stored name
// does not include it.
struct SignedNodeName {
    std::string_view base;
    bool positive = true;
};

inline SignedNodeName splitSignedNodeName(std::string_view signedName) {
    if (!signedName.empty() && (signedName.back() == '+' || signedName.back() == '-'))
        return {signedName.substr(0, signedName.size() - 1), signedName.back() == '+'};
    return {signedName, true};
}

// One object per node. The strand lives on OrientedNode, not on a second
// copy of this object. The stored sequence is the positive orientation.
class DeBruijnNode
{
public:
    // `name` is the unsigned name.
    DeBruijnNode(std::string_view name, float depth, const Sequence &sequence, unsigned length = 0);
    ~DeBruijnNode() = default;

    std::string_view getNameWithoutSign() const { return m_name; }
    // Unsigned name. A trailing + or - is stripped. Empty is refused.
    void setName(std::string_view newName);

    double getDepth() const { return m_depth; }
    void setDepth(double newDepth) { m_depth = float(newDepth); }
    float getGC() const;

    const Sequence &getSequence() const { return m_sequence; }
    Sequence &getSequence() { return m_sequence; }
    void setSequence(const QByteArray &newSeq);
    void setSequence(const Sequence &newSeq);
    // 30 bits. A longer sequence is rejected by loaders before it is stored.
    static constexpr unsigned MaxLength = (1u << 30) - 1;

    static bool sequenceIsSelfComplement(const Sequence &sequence) {
        if (sequence.empty() || sequence.missing())
            return false;
        // The ends disagree, so the reverse complement cannot match.
        if (nucl_complement(sequence[0]) != sequence[sequence.size() - 1])
            return false;
        return sequence == sequence.GetReverseComplement();
    }

    unsigned getLength() const { return m_lengthAndSpecial & LengthMask; }
    bool sequenceIsMissing() const;
    bool isInDepthRange(double min, double max) const { return m_depth >= min && m_depth <= max; }

    // Forward sequence. A negative orientation indexes the reverse complement
    // through OrientedNode::getBaseAt.
    char getBaseAt(int i) const {
        if (i >= 0 && i < m_sequence.size())
            return m_sequence[i];
        return '\0';
    }

    bool isSelfComplement() const { return graphicsBit(SelfComplement); }

    auto edgeBegin() { return m_edges.begin(); }
    const auto edgeBegin() const { return m_edges.begin(); }
    auto edgeEnd() { return m_edges.end(); }
    const auto edgeEnd() const { return m_edges.end(); }
    auto edges() { return bandage::make_range(edgeBegin(), edgeEnd()); }
    const auto edges() const { return bandage::make_range(edgeBegin(), edgeEnd()); }
    // Edge pointers that spilled out of this object.
    uint64_t edgeListHeapBytes() const;
    // Name characters that did not fit in this object. A short name reports 0.
    uint64_t nameHeapBytes() const;
    void addEdge(DeBruijnEdge *edge);
    void removeEdge(DeBruijnEdge *edge);

    // The graphics item for the positive orientation, which is the only item
    // single mode draws. The negative orientation's item lives on the graph.
    // setPointer keeps the flag bits. Assigning a new pair would clear them.
    GraphicsItemNode *getGraphicsItemNode() const { return m_graphicsItem.getPointer(); }
    void setGraphicsItemNode(GraphicsItemNode *item) { m_graphicsItem.setPointer(item); }

    bool isDrawn(bool negative) const {
        if (isSelfComplement())
            negative = false;
        return graphicsBit(negative ? DrawnNegative : DrawnPositive);
    }
    void setDrawn(bool negative, bool drawn) {
        if (isSelfComplement())
            negative = false;
        setGraphicsBit(negative ? DrawnNegative : DrawnPositive, drawn);
    }
    bool isSpecial(bool negative) const {
        if (isSelfComplement())
            negative = false;
        return specialBit(negative ? SpecialNegative : SpecialPositive);
    }
    void setSpecial(bool negative, bool special) {
        if (isSelfComplement())
            negative = false;
        setSpecialBit(negative ? SpecialNegative : SpecialPositive, special);
    }
    void resetNode();

private:
    // GraphicsItemNode is incomplete in this header. Its alignment is 8,
    // so the pointer has three spare bits: two drawn flags and self-complement.
    struct GraphicsItemPointerTraits {
        static void *getAsVoidPointer(GraphicsItemNode *pointer) { return pointer; }
        static GraphicsItemNode *getFromVoidPointer(void *pointer) {
            return static_cast<GraphicsItemNode *>(pointer);
        }
        static constexpr int NumLowBitsAvailable = 3;
    };

    enum GraphicsBit : unsigned {
        DrawnPositive = 1u << 0,
        DrawnNegative = 1u << 1,
        SelfComplement = 1u << 2,
    };

    // Bits 0–29 are the length, up to 1 Gbp. The top two bits are the
    // per-orientation special flags.
    static constexpr uint32_t LengthMask = MaxLength;
    static constexpr uint32_t SpecialPositive = 1u << 30;
    static constexpr uint32_t SpecialNegative = 1u << 31;

    bool graphicsBit(unsigned bit) const { return (m_graphicsItem.getInt() & bit) != 0; }
    void setGraphicsBit(unsigned bit, bool on) {
        unsigned bits = m_graphicsItem.getInt();
        if (on)
            bits |= bit;
        else
            bits &= ~bit;
        m_graphicsItem.setInt(bits);
    }
    bool specialBit(uint32_t bit) const { return (m_lengthAndSpecial & bit) != 0; }
    void setSpecialBit(uint32_t bit, bool on) {
        if (on)
            m_lengthAndSpecial |= bit;
        else
            m_lengthAndSpecial &= ~bit;
    }
    void setStoredLength(unsigned length);

    std::string m_name;
    Sequence m_sequence;
    // A third edge is a separate allocation.
    adt::SmallPODVector<DeBruijnEdge *,
                        adt::impl::HybridAllocatedStorage<DeBruijnEdge *, 2>> m_edges;
    bandage::PointerIntPair<GraphicsItemNode *, 3, unsigned, GraphicsItemPointerTraits> m_graphicsItem;
    float m_depth = 0;
    uint32_t m_lengthAndSpecial = 0;

    void refreshSelfComplement();
};

// std::string is 24 bytes in libc++ and 32 in libstdc++ and MSVC.
static_assert(sizeof(DeBruijnNode) ==
                      sizeof(std::string) + sizeof(Sequence) +
                      sizeof(adt::SmallPODVector<DeBruijnEdge *,
                                                 adt::impl::HybridAllocatedStorage<DeBruijnEdge *, 2>>) +
                      16,
              "drawn, special, and self-complement flags must stay in the existing words");

#include "orientednode.h"
#include "directededge.h"
