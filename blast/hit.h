// Copyright 2017 Ryan Wick
// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "graph/graphlocation.h"
#include "program/scinot.h"

#include <QString>
#include <vector>

class DeBruijnNode;

namespace search {
    class Query;

    class Hit {
    public:
        Hit(Query *query, DeBruijnNode *node,
            double percentIdentity, int alignmentLength,
            int numberMismatches, int numberGapOpens,
            int queryStart, int queryEnd,
            int nodeStart, int nodeEnd, SciNot eValue, double bitScore);

        Query *m_query;
        DeBruijnNode *m_node;
        double m_percentIdentity;
        int m_alignmentLength;
        int m_numberMismatches;
        int m_numberGapOpens;
        int m_queryStart;
        int m_queryEnd;
        int m_nodeStart;
        int m_nodeEnd;
        SciNot m_eValue;
        double m_bitScore;

        double getQueryCoverageFraction() const;

        GraphLocation getHitStart() const;
        GraphLocation getHitEnd() const;

        QByteArray getNodeSequence() const;
        int getNodeLength() const { return m_nodeEnd - m_nodeStart + 1; }

        double nodeStartFraction() const;
        double nodeEndFraction() const;
        double queryStartFraction() const;
        double queryEndFraction() const;
    };
}