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


#ifndef BLASTQUERIES_H
#define BLASTQUERIES_H

#include "blastquery.h"

#include <vector>

// This class manages all BLAST queries. It holds BlastQuery
// objects

class BlastQueries {
public:
    BlastQueries();
    ~BlastQueries();

    BlastQuery * getQueryFromName(QString queryName) const;

    bool empty() const { return m_queries.empty(); }
    const auto &operator[](size_t idx) const { return m_queries[idx]; }
    auto &operator[](size_t idx) { return m_queries[idx]; }

    auto begin() { return m_queries.begin(); }
    const auto begin() const { return m_queries.begin(); }
    auto end() { return m_queries.end(); }
    const auto end() const { return m_queries.end(); }

    void addQuery(BlastQuery * newQuery);
    QString renameQuery(BlastQuery * newQuery, QString newName);
    void clearAllQueries();
    void clearSomeQueries(const std::vector<BlastQuery *> &queriesToRemove);
    void searchOccurred();
    void clearSearchResults();

    size_t getQueryCount() const;
    size_t getQueryCountWithAtLeastOnePath() const;
    size_t getQueryPathCount() const ;
    size_t getQueryCount(QuerySequenceType sequenceType) const;
    bool isQueryPresent(const BlastQuery * query) const;
    std::vector<BlastHit> allHits() const;
    const auto &queries() const { return m_queries; }
    auto &queries() { return m_queries; }
    BlastQuery *query(size_t idx) { return m_queries[idx]; }
    const BlastQuery *query(size_t idx) const { return m_queries[idx]; }
    std::vector<DeBruijnNode *> getNodesFromHits(const QString& queryName = "") const;

    void findQueryPaths();
private:
    QString getUniqueName(QString name);

    std::vector<BlastQuery *> m_queries;
    std::vector<QColor> m_presetColours;
};

#endif // BLASTQUERIES_H
