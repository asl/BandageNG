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


#include "blasthit.h"
#include "blastquery.h"

#include "graph/debruijnnode.h"
#include "graph/sequenceutils.h"

Hit::Hit(Query * query, DeBruijnNode * node,
         double percentIdentity, int alignmentLength,
         int numberMismatches, int numberGapOpens,
         int queryStart, int queryEnd,
         int nodeStart, int nodeEnd,
         SciNot eValue, double bitScore) :
    m_query(query), m_node(node),
    m_percentIdentity(percentIdentity), m_alignmentLength(alignmentLength),
    m_numberMismatches(numberMismatches), m_numberGapOpens(numberGapOpens),
    m_queryStart(queryStart), m_queryEnd(queryEnd),
    m_nodeStart(nodeStart), m_nodeEnd(nodeEnd),
    m_eValue(eValue), m_bitScore(bitScore) { }

double Hit::nodeStartFraction() const {
    return double(m_nodeStart - 1) / m_node->getLength();
}

double Hit::nodeEndFraction() const {
    return double(m_nodeEnd - 1) / m_node->getLength();
}

double Hit::queryStartFraction() const {
    return double(m_queryStart - 1) / m_query->getLength();
}

double Hit::queryEndFraction() const {
    return double(m_queryEnd - 1) / m_query->getLength();
}

double Hit::getQueryCoverageFraction() const {
    int queryRegionSize = m_queryEnd - m_queryStart + 1;
    int queryLength = m_query->getLength();

    if (queryLength == 0)
        return 0.0;
    else
        return double(queryRegionSize) / queryLength;
}


GraphLocation Hit::getHitStart() const {
    return {m_node, m_nodeStart};
}

GraphLocation Hit::getHitEnd() const {
    return {m_node, m_nodeEnd};
}

//This function returns the node sequence for this hit.
QByteArray Hit::getNodeSequence() const {
    return utils::sequenceToQByteArray(m_node->getSequence().Subseq(m_nodeStart-1, m_nodeEnd));
}
