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

#include "query.h"
#include "hits.h"
#include <vector>

namespace search {
// This class manages all queries. It holds Query objects
class Queries {
public:
    Queries();
    ~Queries();

    Query * getQueryFromName(QString queryName) const;

    bool empty() const { return m_queries.empty(); }
    const auto &operator[](size_t idx) const { return m_queries[idx]; }
    auto &operator[](size_t idx) { return m_queries[idx]; }

    auto begin() { return m_queries.begin(); }
    const auto begin() const { return m_queries.begin(); }
    auto end() { return m_queries.end(); }
    const auto end() const { return m_queries.end(); }

    void addQuery(Query * newQuery);
    QString renameQuery(Query * newQuery, QString newName);
    void clearAllQueries();
    void clearSomeQueries(const std::vector<Query *> &queriesToRemove);
    void searchOccurred();
    void clearSearchResults();

    size_t getQueryCount() const;
    size_t getQueryCountWithAtLeastOnePath() const;
    size_t getQueryPathCount() const ;
    size_t getQueryCount(QuerySequenceType sequenceType) const;
    bool isQueryPresent(const Query * query) const;
    Query::Hits allHits() const;
    size_t numHits() const;
    const auto &queries() const { return m_queries; }
    auto &queries() { return m_queries; }
    Query *query(size_t idx) { return m_queries[idx]; }
    const Query *query(size_t idx) const { return m_queries[idx]; }
    std::vector<DeBruijnNode *> getNodesFromHits(const QString& queryName = "") const;

    void addNodeHits(const NodeHits &hits);
    void addPathHits(const PathHits &hits);
    void findQueryPaths();
private:
    QString getUniqueName(QString name);

    // FIXME: This should really own the queries!
    std::vector<Query*> m_queries;
    std::vector<QColor> m_presetColours;
};

}
