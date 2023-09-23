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

#include "hit.h"

#include "graph/path.h"
#include "program/scinot.h"

namespace search {
    class Query;

    class QueryPath {
    public:
        // CREATORS
        QueryPath(Path path, Query *query);
        QueryPath(Path path, Query *query, std::vector<const Hit*> hits);

        //ACCESSORS
        const Path &getPath() const { return m_path; }
        const auto &getHits() const { return m_hits; }
        int queryStart() const;
        int queryEnd() const;
        size_t queryLength() const;

        SciNot getEvalueProduct() const;
        double getMeanHitPercIdentity() const;
        double getRelativeLengthDiscrepancy() const;
        double getRelativePathLength() const;
        int getAbsolutePathLengthDifference() const;
        QString getAbsolutePathLengthDifferenceString(bool commas) const;
        double getPathQueryCoverage() const;
        double getHitsQueryCoverage() const;
        int getTotalHitMismatches() const;
        int getTotalHitGapOpens() const;

        bool operator<(QueryPath const &other) const;
    private:
        Path m_path;
        Query *m_query;
        std::vector<const Hit*> m_hits;

        int getHitQueryLength() const;
        int getHitOverlap(const Hit *hit1, const Hit *hit2) const;
    };
}
