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


#include "debruijnedge.h"
#include "assemblygraph.h"

#include "program/settings.h"

#include <charconv>
#include <cmath>
#include <QApplication>

static_assert(sizeof(DeBruijnEdge) == 32, "DeBruijnEdge must stay in the 32-byte size class");

DeBruijnEdge::DeBruijnEdge(OrientedNode startingNode, OrientedNode endingNode) :
    m_startingNode(startingNode), m_endingNode(endingNode), m_graphicsItemEdge(nullptr),
    m_drawn(false), m_drawnReverse(false), m_overlapType(UNKNOWN_OVERLAP), m_overlap(0) {
}

bool DeBruijnEdge::isOwnReverseComplement() const {
    return m_startingNode == m_endingNode.reverse() &&
           m_endingNode == m_startingNode.reverse();
}

std::pair<OrientedNode, OrientedNode> DeBruijnEdge::orientedEnds(bool reversed) const {
    if (!reversed || isOwnReverseComplement())
        return {m_startingNode, m_endingNode};
    return {m_endingNode.reverse(), m_startingNode.reverse()};
}

OrientedNode DeBruijnEdge::other(OrientedNode node) const {
    if (node == m_startingNode)
        return m_endingNode;
    if (node == m_endingNode)
        return m_startingNode;
    if (!isOwnReverseComplement()) {
        if (node == m_endingNode.reverse())
            return m_startingNode.reverse();
        if (node == m_startingNode.reverse())
            return m_endingNode.reverse();
    }
    return m_startingNode;
}

static bool nodeIsOwnReverseComplement(OrientedNode node) {
    return node && node.reverse() == node;
}

OrientedNode DeBruijnEdge::downstream(OrientedNode node) const {
    if (!nodeIsOwnReverseComplement(node))
        return other(node);
    if (node == m_startingNode)
        return m_endingNode;
    if (!isOwnReverseComplement() && node == m_endingNode.reverse())
        return m_startingNode.reverse();
    return other(node);
}

OrientedNode DeBruijnEdge::upstream(OrientedNode node) const {
    if (!nodeIsOwnReverseComplement(node))
        return other(node);
    if (node == m_endingNode && node != m_startingNode)
        return m_startingNode;
    if (node == m_startingNode && !isOwnReverseComplement())
        return m_endingNode.reverse();
    if (node == m_endingNode)
        return m_startingNode;
    return other(node);
}

bool DeBruijnEdge::leaves(OrientedNode node) const {
    if (node == m_startingNode)
        return true;
    if (isOwnReverseComplement())
        return false;
    return node == m_endingNode.reverse();
}

bool DeBruijnEdge::enters(OrientedNode node) const {
    if (node == m_endingNode)
        return true;
    if (isOwnReverseComplement())
        return false;
    return node == m_startingNode.reverse();
}

bool DeBruijnEdge::connects(OrientedNode from, OrientedNode to) const {
    if (m_startingNode == from && m_endingNode == to)
        return true;
    if (isOwnReverseComplement())
        return false;
    return from == m_endingNode.reverse() &&
           to == m_startingNode.reverse();
}

bool DeBruijnEdge::determineIfDrawn() {
    auto strandDrawn = [](OrientedNode node) {
        return node.isDrawn() || node.reverse().isDrawn();
    };

    if (g_settings->doubleMode) {
        m_drawn = m_startingNode.isDrawn() && m_endingNode.isDrawn();
        if (isOwnReverseComplement()) {
            m_drawnReverse = false;
        } else {
            OrientedNode revFrom = m_endingNode.reverse();
            OrientedNode revTo = m_startingNode.reverse();
            m_drawnReverse = revFrom.isDrawn() && revTo.isDrawn();
        }
    } else {
        // Single mode draws the stored (positive) direction once.
        m_drawnReverse = false;
        m_drawn = strandDrawn(m_startingNode) && strandDrawn(m_endingNode);
    }
    return m_drawn || m_drawnReverse;
}

// Stored ends are the positive direction. Mixed signs use the same name
// comparison the old pair used against the reverse edge's start, which is
// the reverse complement of the stored end.
bool DeBruijnEdge::isPositiveOrientation(OrientedNode from, OrientedNode to) {
    if (from.positive() && to.positive())
        return true;
    if (from.negative() && to.negative())
        return false;
    if (from == to.reverse() && to == from.reverse())
        return true;
    // The opposite direction is two positive ends, so this request is the
    // other way.
    OrientedNode rcFrom = from.reverse();
    OrientedNode rcTo = to.reverse();
    if (rcFrom.positive() && rcTo.positive())
        return false;
    return from.getName() > to.reverse().getName();
}

bool DeBruijnEdge::isPositiveEdge() const {
    return isPositiveOrientation(m_startingNode, m_endingNode);
}


//This function traces all possible paths from this edge.
//It proceeds a number of steps, as determined by a setting.
//If forward is true, it looks in a forward direction (starting nodes to
//ending nodes).  If forward is false, it looks in a backward direction
//(ending nodes to starting nodes).
void DeBruijnEdge::tracePaths(bool forward,
                              int stepsRemaining,
                              std::vector<std::vector<OrientedNode>> &allPaths,
                              OrientedNode startingNode,
                              std::vector<OrientedNode> pathSoFar) const {
    //This can go for a while, so keep the UI responsive.
    QApplication::processEvents();

    // The edge may be stored in the opposite orientation from this path.
    OrientedNode here = pathSoFar.empty() ? startingNode : pathSoFar.back();
    OrientedNode nextNode = forward ? downstream(here) : upstream(here);

    //Add that node to the path so far.
    pathSoFar.push_back(nextNode);

    //If there are no steps left, then the path so far is done.
    --stepsRemaining;
    if (stepsRemaining == 0)
    {
        allPaths.push_back(pathSoFar);
        return;
    }

    //If the code got here, then more steps remain.
    auto links = forward ? outgoingEdges(nextNode) : incomingEdges(nextNode);
    if (links.empty()) {
        allPaths.push_back(pathSoFar);
        return;
    }
    for (auto link : links) {
        OrientedNode nextNextNode = link.otherNode();

        if (nextNextNode == startingNode) {
            allPaths.push_back(pathSoFar);
            continue;
        }

        if (timesNodeInPath(nextNextNode, pathSoFar) < 2)
            link.edge()->tracePaths(forward, stepsRemaining, allPaths, startingNode, pathSoFar);
    }
}


// This function counts how many times a node appears in a path
unsigned DeBruijnEdge::timesNodeInPath(OrientedNode node, const std::vector<OrientedNode> &path) {
    unsigned count = 0;
    for (OrientedNode i : path)
        count += (i == node);

    return count;
}



bool DeBruijnEdge::leadsOnlyToNode(bool forward,
                                   int stepsRemaining,
                                   OrientedNode target,
                                   std::vector<OrientedNode> pathSoFar,
                                   bool includeReverseComplement) const
{
    //This can go for a while, so keep the UI responsive.
    QApplication::processEvents();

    // Callers pass the node this edge was reached from as the last entry.
    OrientedNode nextNode = forward ? downstream(pathSoFar.back())
                                    : upstream(pathSoFar.back());

    //Add that node to the path so far.
    pathSoFar.push_back(nextNode);

    //If this path has landed on the node from which the search began,
    //that means we've followed a loop around.  The search has therefore
    //failed because this path could represent circular DNA that does
    //not contain the target.
    if (nextNode == pathSoFar[0])
        return false;

    //If the next node is the target, the search succeeded!
    if (nextNode == target)
        return true;

    //If we are including reverse complements and the next node is
    //the reverse complement of the target, the search succeeded!
    if (includeReverseComplement && nextNode.reverse() == target)
        return true;

    //If there are no steps left, then the search failed.
    --stepsRemaining;
    if (stepsRemaining == 0)
        return false;

    //If the code got here, then more steps remain.
    auto links = forward ? outgoingEdges(nextNode) : incomingEdges(nextNode);
    if (links.empty())
        return false;
    for (auto link : links) {
        OrientedNode nextNextNode = link.otherNode();
        if (timesNodeInPath(nextNextNode, pathSoFar) < 2) {
            if (!link.edge()->leadsOnlyToNode(forward, stepsRemaining, target, pathSoFar,
                                              includeReverseComplement))
                return false;
        }
    }
    return true;
}


//This function tries to automatically determine the overlap size
//between the two nodes.  It tries each overlap size between the min
//to the max (in settings), assigning the first one it finds.
void DeBruijnEdge::autoDetermineExactOverlap()
{
    m_overlap = 0;
    m_overlapType = AUTO_DETERMINED_EXACT_OVERLAP;

    //Find an appropriate search range
    int minPossibleOverlap = std::min(m_startingNode.getLength(), m_endingNode.getLength());
    if (minPossibleOverlap < g_settings->minAutoFindEdgeOverlap)
        return;
    int min = std::min(minPossibleOverlap, g_settings->minAutoFindEdgeOverlap);
    int max = std::min(minPossibleOverlap, g_settings->maxAutoFindEdgeOverlap);

    //Try each overlap in the range and set the first one found.
    //However, we don't want the search to be biased towards larger
    //or smaller overlaps, so start with a pseudorandom value and loop.
    int testOverlap = min + (rand() % (max - min + 1));
    for (int i = min; i <= max; ++i)
    {
        if (testExactOverlap(testOverlap))
        {
            m_overlap = testOverlap;
            return;
        }

        ++testOverlap;
        if (testOverlap > max)
            testOverlap = min;
    }
}




//This function tries the given overlap between the two nodes.
//If the overlap works perfectly, it returns true.
bool DeBruijnEdge::testExactOverlap(int overlap) const
{
    bool mismatchFound = false;

    int seq1Offset = int(m_startingNode.getLength()) - overlap;

    //Look at each position in the overlap
    for (int j = 0; j < overlap && !mismatchFound; ++j)
    {
        char a = m_startingNode.getBaseAt(seq1Offset + j);
        char b = m_endingNode.getBaseAt(j);
        if (a != b)
            mismatchFound = true;
    }

    return !mismatchFound;
}


static bool parseWholeNumber(std::string_view text, long long &value) {
    if (text.empty())
        return false;
    const char *end = text.data() + text.size();
    auto [parsed, error] = std::from_chars(text.data(), end, value);
    return error == std::errc() && parsed == end;
}

bool DeBruijnEdge::compareEdgePointers(const DeBruijnEdge *a, const DeBruijnEdge *b) {
    const std::string aStart = a->getStartingNode().getName();
    const std::string bStart = b->getStartingNode().getName();
    long long aStartNumber = 0;
    long long bStartNumber = 0;
    long long aEndNumber = 0;
    long long bEndNumber = 0;
    const bool ok1 = parseWholeNumber(a->getStartingNode().getNameWithoutSign(), aStartNumber);
    const bool ok2 = parseWholeNumber(b->getStartingNode().getNameWithoutSign(), bStartNumber);
    const bool ok3 = parseWholeNumber(a->getEndingNode().getNameWithoutSign(), aEndNumber);
    const bool ok4 = parseWholeNumber(b->getEndingNode().getNameWithoutSign(), bEndNumber);

    //If the node names are essentially numbers, then sort them as numbers.
    if (ok1 && ok2 && ok3 && ok4)
    {
        if (aStartNumber != bStartNumber)
            return aStartNumber < bStartNumber;

        return aEndNumber < bEndNumber;
    }

    //If the node names are strings, then just sort them as strings.
    return aStart < bStart;
}
