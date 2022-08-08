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


#include "blastqueries.h"

#include "program/globals.h"
#include "program/settings.h"

#include <unordered_set>

using namespace search;

Queries::Queries()
: m_presetColours{getPresetColours()} {}

Queries::~Queries() {
    clearAllQueries();
}

Query *Queries::getQueryFromName(QString queryName) const {
    auto res =
            std::find_if(m_queries.begin(), m_queries.end(),
                         [&](const auto *query) { return queryName == query->getName(); });

    if (res == m_queries.end())
        return nullptr;

    return *res;
}

void Queries::addQuery(Query * newQuery) {
    newQuery->setName(getUniqueName(newQuery->getName()));

    // Give the new query a colour
    newQuery->setColour(m_presetColours[m_queries.size() % m_presetColours.size()]);

    m_queries.push_back(newQuery);
}

// This function renames the query.  It returns the name given, because that
// might not be exactly the same as the name passed to the function if it
// wasn't unique.
QString Queries::renameQuery(Query * newQuery, QString newName) {
    newQuery->setName(getUniqueName(newName));
    return newQuery->getName();
}

//This function looks at the name, and if it is not unique, it adds a suffix
//to make it unique.  Also make sure it's not "all" or "none", as those will
//conflict with viewing all queries at once or no queries.
QString Queries::getUniqueName(QString name) {
    //If the query name ends in a semicolon, remove it.  Ending semicolons
    //mess with BLAST.
    if (name.endsWith(';'))
        name.chop(1);

    //The name can't be empty.
    if (name == "")
        name = g_settings->unnamedQueryDefaultName;

    int queryNumber = 2;
    QString finalName = name;
    while (getQueryFromName(finalName) != 0 ||
           finalName == "all" || finalName == "none")
        finalName = name + "_" + QString::number(queryNumber++);
    return finalName;
}

void Queries::clearAllQueries() {
    for (auto *query : m_queries)
        delete query;
    m_queries.clear();
}

void Queries::clearSomeQueries(const std::vector<Query *> &queriesToRemove) {
    std::unordered_set<Query *> queries(queriesToRemove.begin(), queriesToRemove.end());
    m_queries.erase(std::remove_if(m_queries.begin(), m_queries.end(),
                                   [&](auto *query) { return queries.count(query); }),
                    m_queries.end());

    for (auto *query: queriesToRemove)
        delete query;
}

void Queries::searchOccurred() {
    for (auto *query : m_queries)
        query->setAsSearchedFor();
}


void Queries::clearSearchResults() {
    for (auto *query : m_queries)
        query->clearSearchResults();
}


size_t Queries::getQueryCount() const {
    return m_queries.size();
}

size_t Queries::getQueryCountWithAtLeastOnePath() const {
    int count = 0;
    for (const auto *query : m_queries)
        count += (query->getPathCount() > 0);

    return count;
}

size_t Queries::getQueryPathCount() const {
    size_t count = 0;
    for (const auto *query : m_queries)
        count += query->getPathCount();
    return count;
}

size_t Queries::getQueryCount(QuerySequenceType sequenceType) const {
    size_t count = 0;
    for (const auto *query : m_queries)
        count += (query->getSequenceType() == sequenceType);
    return count;
}

//This function looks to see if a query pointer is in the list
//of queries.  The query pointer given may or may not still
//actually exist, so it can't be dereferenced.
bool Queries::isQueryPresent(const Query * query) const {
    return std::find(m_queries.begin(), m_queries.end(), query) != m_queries.end();
}

// This function looks at each BLAST query and tries to find a path through
// the graph which covers the maximal amount of the query.
void Queries::findQueryPaths() {
    for (auto *query : m_queries)
        query->findQueryPaths();
}

std::vector<Hit> Queries::allHits() const {
    std::vector<Hit> res;

    for (const auto *query : m_queries) {
        const auto &hits = query->getHits();
        res.insert(res.end(), hits.begin(), hits.end());
    }

    return res;
}

std::vector<DeBruijnNode *> Queries::getNodesFromHits(const QString& queryName) const {
    std::vector<DeBruijnNode *> returnVector;

    if (empty())
        return returnVector;

    // If "all" is selected, then we'll display nodes with hits from any query
    if (queryName == "all" || queryName.isEmpty()) {
        // Add pointers to nodes that have a hit for the selected target(s).
        for (auto *currentQuery: m_queries) {
            for (const auto &hit: currentQuery->getHits())
                returnVector.push_back(hit.m_node);
        }
    } else {
        if (Query *query = getQueryFromName(queryName)) {
            for (const auto &hit: query->getHits())
                returnVector.push_back(hit.m_node);
        }
    }

    return returnVector;
}
