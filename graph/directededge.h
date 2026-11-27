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

#include "debruijnnode.h"
#include "program/llvm.h"
#include "small_vector/small_pod_vector.hpp"

#include <iterator>

class DeBruijnEdge;

enum EdgeOverlapType {
    UNKNOWN_OVERLAP,
    EXACT_OVERLAP,
    AUTO_DETERMINED_EXACT_OVERLAP,
    JUMP,
    EXTRA_LINK
};

// Which incidences an iterator yields. Both visits an edge twice when it
// leaves and enters the same orientation, once in each direction.
enum class IncidentFilter { Outgoing, Incoming, Both };

// A stored edge as seen from the orientation being iterated. otherNode() is
// the far end in this direction; the stored orientation may be the opposite one.
class DirectedEdge {
    DeBruijnEdge *m_edge = nullptr;
    OrientedNode m_origin;
    bool m_outgoing = true;

public:
    DirectedEdge() = default;
    DirectedEdge(DeBruijnEdge *edge, OrientedNode origin, bool outgoing)
            : m_edge(edge), m_origin(origin), m_outgoing(outgoing) {}

    DeBruijnEdge *edge() const { return m_edge; }
    bool outgoing() const { return m_outgoing; }
    OrientedNode otherNode() const;
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
    OrientedNode m_origin;
    IncidentFilter m_filter = IncidentFilter::Outgoing;
    bool m_outgoingPass = true;

    void skip();
    bool onOutgoingPass() const;

public:
    IncidentEdgeIterator() = default;
    IncidentEdgeIterator(EdgeIt begin, EdgeIt end, OrientedNode origin,
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

inline auto outgoingEdges(OrientedNode node) {
    return bandage::make_range(node.outgoing_begin(), node.outgoing_end());
}
inline auto incomingEdges(OrientedNode node) {
    return bandage::make_range(node.incoming_begin(), node.incoming_end());
}
inline auto incidentEdges(OrientedNode node) {
    return bandage::make_range(node.incident_begin(), node.incident_end());
}
