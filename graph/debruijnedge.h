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

#include "debruijnnode.h"

#include <utility>

class GraphicsItemEdge;

class DeBruijnEdge
{
    // Two drawn bits share the overlap word, so the edge stays 32 bytes.
    static constexpr unsigned OVERLAP_BITS = 27;

    OrientedNode m_startingNode;
    OrientedNode m_endingNode;
    GraphicsItemEdge *m_graphicsItemEdge;

    bool m_drawn : 1;
    bool m_drawnReverse : 1;
    EdgeOverlapType m_overlapType : 3;
    int m_overlap : OVERLAP_BITS;

public:
    //CREATORS
    DeBruijnEdge(OrientedNode startingNode, OrientedNode endingNode);

    OrientedNode getStartingNode() const {return m_startingNode;}
    OrientedNode getEndingNode() const {return m_endingNode;}
    void setStartingNode(OrientedNode node) { m_startingNode = node;}
    void setEndingNode(OrientedNode node) { m_endingNode = node;}

    // Stored orientation, or the opposite direction when `reversed` is set.
    std::pair<OrientedNode, OrientedNode> orientedEnds(bool reversed) const;

    GraphicsItemEdge *getGraphicsItemEdge() const {return m_graphicsItemEdge;}
    bool isDrawn() const {return m_drawn;}
    bool isDrawnReverse() const {return m_drawnReverse;}
    int getOverlap() const {
        unsigned m = CHAR_BIT * sizeof(decltype(m_overlap)) - OVERLAP_BITS;
        return (m_overlap << m) >> m;
    }
    int exactOverlapLength() const {
        if (m_overlapType == EXACT_OVERLAP || m_overlapType == AUTO_DETERMINED_EXACT_OVERLAP)
            return getOverlap();
        return 0;
    }
    EdgeOverlapType getOverlapType() const {return m_overlapType;}

    // One object stores one ordered pair of ends. The reverse direction is
    // rc(stored end) -> rc(stored start). An own-RC link is that pair
    // reversed onto itself (A+ -> A-, or a self-loop on a palindromic
    // node), so it has a single direction. The node in these queries
    // holds the pointer: a stored end, or the reverse complement of one.

    // The other end of the orientation `node` is on. The stored start and
    // the stored end swap. rc(stored end) and rc(stored start) swap. This
    // is one node for either direction. downstream() and upstream() are
    // what split a palindromic node.
    OrientedNode other(OrientedNode node) const;
    // Neighbour along this edge. DirectedEdge::otherNode() uses
    // downstream() when the edge leaves `node` and upstream() when it
    // enters. An ordinary orientation is incident from one side, and both
    // return other(). A palindromic node is one orientation on both
    // sides: downstream() is the node reached by leaving, upstream() the
    // node reached by entering. Stored as S -> B, downstream(S) is B and
    // upstream(S) is rc(B). A self-loop S -> S stays on S either way.
    OrientedNode downstream(OrientedNode node) const;
    OrientedNode upstream(OrientedNode node) const;
    // This edge departs `node`. `node` is the stored start, or rc(stored
    // end) on a link with a distinct reverse direction.
    bool leaves(OrientedNode node) const;
    // This edge arrives at `node`. `node` is the stored end, or
    // rc(stored start) on a link with a distinct reverse direction.
    bool enters(OrientedNode node) const;
    // `from` -> `to` is the stored pair, or rc(stored end) -> rc(stored
    // start) when that direction is a different pair of orientations.
    bool connects(OrientedNode from, OrientedNode to) const;

    bool testExactOverlap(int overlap) const;
    void tracePaths(bool forward,
                    int stepsRemaining,
                    std::vector<std::vector<OrientedNode>> &allPaths,
                    OrientedNode startingNode,
                    std::vector<OrientedNode> pathSoFar = {}) const;
    bool leadsOnlyToNode(bool forward,
                         int stepsRemaining,
                         OrientedNode target,
                         std::vector<OrientedNode> pathSoFar,
                         bool includeReverseComplement) const;
    // True when the stored ends are a positive orientation.
    bool isPositiveEdge() const;
    bool isNegativeEdge() const {return !isPositiveEdge();}
    static bool isPositiveOrientation(OrientedNode from, OrientedNode to);
    bool isOwnReverseComplement() const;
    static bool compareEdgePointers(const DeBruijnEdge *a, const DeBruijnEdge *b);

    //MODIFERS
    void setGraphicsItemEdge(GraphicsItemEdge *gie) {m_graphicsItemEdge = gie;}
    void setOverlap(int ol) {m_overlap = ol;}
    void setOverlapType(EdgeOverlapType olt) {m_overlapType = olt;}
    void reset() {m_graphicsItemEdge = nullptr; m_drawn = false; m_drawnReverse = false;}
    bool determineIfDrawn();
    void setExactOverlap(int overlap) {m_overlap = overlap; m_overlapType = EXACT_OVERLAP;}
    void autoDetermineExactOverlap();

private:
    static unsigned timesNodeInPath(OrientedNode node, const std::vector<OrientedNode> &path);
};
