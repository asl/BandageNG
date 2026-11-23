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

#pragma once

#include "program/llvm.h"
#include "small_vector/small_pod_vector.hpp"

#include <iterator>

class DeBruijnNode;
class DeBruijnEdge;

enum EdgeOverlapType {
    UNKNOWN_OVERLAP,
    EXACT_OVERLAP,
    AUTO_DETERMINED_EXACT_OVERLAP,
    JUMP,
    EXTRA_LINK
};

// Which incidences an iterator yields. Both visits an edge twice when it
// leaves and enters the same node, once in each direction.
enum class IncidentFilter { Outgoing, Incoming, Both };

// A stored edge as seen from the node being iterated. otherNode() is the
// far end in this direction; the stored orientation may be the opposite one.
class DirectedEdge {
    DeBruijnEdge *m_edge = nullptr;
    const DeBruijnNode *m_origin = nullptr;
    bool m_outgoing = true;

public:
    DirectedEdge() = default;
    DirectedEdge(DeBruijnEdge *edge, const DeBruijnNode *origin, bool outgoing)
            : m_edge(edge), m_origin(origin), m_outgoing(outgoing) {}

    DeBruijnEdge *edge() const { return m_edge; }
    bool outgoing() const { return m_outgoing; }
    DeBruijnNode *otherNode() const;
    int overlap() const;
    EdgeOverlapType overlapType() const;
};

// Copies are independent, so this is multi-pass and stays a forward
// iterator. operator* returns a value; the LLVM facade proxies operator->.
class IncidentEdgeIterator
        : public bandage::iterator_facade_base<IncidentEdgeIterator,
                                            std::forward_iterator_tag,
                                            DirectedEdge,
                                            std::ptrdiff_t,
                                            const DirectedEdge *,
                                            DirectedEdge> {
    using EdgeIt = adt::SmallPODVector<DeBruijnEdge *>::const_iterator;
    EdgeIt m_it{};
    EdgeIt m_begin{};
    EdgeIt m_end{};
    const DeBruijnNode *m_origin = nullptr;
    IncidentFilter m_filter = IncidentFilter::Outgoing;
    bool m_outgoingPass = true;

    void skip();
    bool onOutgoingPass() const;

public:
    IncidentEdgeIterator() = default;
    IncidentEdgeIterator(EdgeIt begin, EdgeIt end, const DeBruijnNode *origin,
                         IncidentFilter filter, bool isEnd);

    bool operator==(const IncidentEdgeIterator &rhs) const {
        return m_it == rhs.m_it && m_outgoingPass == rhs.m_outgoingPass &&
               m_filter == rhs.m_filter;
    }
    DirectedEdge operator*() const;
    IncidentEdgeIterator &operator++();
};

using OutgoingEdgeIterator = IncidentEdgeIterator;
using IncomingEdgeIterator = IncidentEdgeIterator;
