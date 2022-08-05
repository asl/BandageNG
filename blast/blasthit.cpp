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

BlastHit::BlastHit(BlastQuery * query, DeBruijnNode * node,
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
    m_eValue(eValue), m_bitScore(bitScore)
{
    int nodeLength = m_node->getLength();
    int queryLength = m_query->getLength();

    m_nodeStartFraction = double(nodeStart - 1) / nodeLength;
    m_nodeEndFraction = double(nodeEnd) / nodeLength;
    m_queryStartFraction = double(queryStart - 1) / queryLength;
    m_queryEndFraction = double(queryEnd) / queryLength;
}


double BlastHit::getQueryCoverageFraction() const {
    int queryRegionSize = m_queryEnd - m_queryStart + 1;
    int queryLength = m_query->getLength();

    if (queryLength == 0)
        return 0.0;
    else
        return double(queryRegionSize) / queryLength;
}


GraphLocation BlastHit::getHitStart() const {
    return {m_node, m_nodeStart};
}

GraphLocation BlastHit::getHitEnd() const {
    return {m_node, m_nodeEnd};
}

//This function returns the node sequence for this hit.
QByteArray BlastHit::getNodeSequence() const {
    return utils::sequenceToQByteArray(m_node->getSequence().Subseq(m_nodeStart-1, m_nodeEnd));
}
