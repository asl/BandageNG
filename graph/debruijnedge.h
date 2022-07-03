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

class GraphicsItemEdge;

enum EdgeOverlapType {
    UNKNOWN_OVERLAP, EXACT_OVERLAP,
    AUTO_DETERMINED_EXACT_OVERLAP, JUMP};

class DeBruijnEdge
{
    static constexpr unsigned OVERLAP_BITS = 29;
public:
    //CREATORS
    DeBruijnEdge(DeBruijnNode * startingNode, DeBruijnNode * endingNode);

    DeBruijnNode * getStartingNode() const {return m_startingNode;}
    DeBruijnNode * getEndingNode() const {return m_endingNode;}
    GraphicsItemEdge * getGraphicsItemEdge() const {return m_graphicsItemEdge;}
    DeBruijnEdge * getReverseComplement() const {return m_reverseComplement;}
    bool isDrawn() const {return m_drawn;}
    int getOverlap() const {
        unsigned m = CHAR_BIT * sizeof(decltype(m_overlap)) - OVERLAP_BITS;
        return (m_overlap << m) >> m;
    }
    EdgeOverlapType getOverlapType() const {return m_overlapType;}
    DeBruijnNode * getOtherNode(const DeBruijnNode * node) const;
    bool testExactOverlap(int overlap) const;
    void tracePaths(bool forward,
                    int stepsRemaining,
                    std::vector<std::vector<DeBruijnNode *> > * allPaths,
                    DeBruijnNode * startingNode,
                    std::vector<DeBruijnNode *> pathSoFar = std::vector<DeBruijnNode *>()) const;
    bool leadsOnlyToNode(bool forward,
                         int stepsRemaining,
                         DeBruijnNode * target,
                         std::vector<DeBruijnNode *> pathSoFar,
                         bool includeReverseComplement) const;
    bool isPositiveEdge() const;
    bool isNegativeEdge() const {return !isPositiveEdge();}
    bool isOwnReverseComplement() const {return this == getReverseComplement();}
    static bool compareEdgePointers(const DeBruijnEdge * a, const DeBruijnEdge * b);

    //MODIFERS
    void setGraphicsItemEdge(GraphicsItemEdge * gie) {m_graphicsItemEdge = gie;}
    void setReverseComplement(DeBruijnEdge * rc) {m_reverseComplement = rc;}
    void setOverlap(int ol) {m_overlap = ol;}
    void setOverlapType(EdgeOverlapType olt) {m_overlapType = olt;}
    void reset() {m_graphicsItemEdge = nullptr; m_drawn = false;}
    bool determineIfDrawn() { return (m_drawn = edgeIsVisible());}
    void setExactOverlap(int overlap) {m_overlap = overlap; m_overlapType = EXACT_OVERLAP;}
    void autoDetermineExactOverlap();

private:
    DeBruijnNode * m_startingNode;
    DeBruijnNode * m_endingNode;
    GraphicsItemEdge * m_graphicsItemEdge;
    DeBruijnEdge * m_reverseComplement;
    bool m_drawn : 1;
    EdgeOverlapType m_overlapType : 2;
    int m_overlap : OVERLAP_BITS;

    bool edgeIsVisible() const;
    static int timesNodeInPath(DeBruijnNode * node, std::vector<DeBruijnNode *> * path) ;
    static std::vector<DeBruijnEdge *> findNextEdgesInPath(DeBruijnNode * nextNode,
                                                    bool forward) ;
};