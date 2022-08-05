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
#include "blastsearch.h"

#include "program/globals.h"
#include "program/settings.h"
#include <QTextStream>
#include <unordered_set>

BlastQueries::BlastQueries()
: m_presetColours{getPresetColours()} {}

BlastQueries::~BlastQueries() {
    clearAllQueries();
}


BlastQuery *BlastQueries::getQueryFromName(QString queryName) {
    auto res =
            std::find_if(m_queries.begin(), m_queries.end(),
                         [&](const auto *query) { return queryName == query->getName(); });

    if (res == m_queries.end())
        return nullptr;

    return *res;
}


void BlastQueries::addQuery(BlastQuery * newQuery)
{
    newQuery->setName(getUniqueName(newQuery->getName()));

    //Give the new query a colour
    int colourIndex = int(m_queries.size());
    colourIndex %= m_presetColours.size();
    newQuery->setColour(m_presetColours[colourIndex]);

    m_queries.push_back(newQuery);
}


//This function renames the query.  It returns the name given, because that
//might not be exactly the same as the name passed to the function if it
//wasn't unique.
QString BlastQueries::renameQuery(BlastQuery * newQuery, QString newName)
{
    newQuery->setName(getUniqueName(newName));
    return newQuery->getName();
}


//This function looks at the name, and if it is not unique, it adds a suffix
//to make it unique.  Also make sure it's not "all" or "none", as those will
//conflict with viewing all queries at once or no queries.
QString BlastQueries::getUniqueName(QString name)
{
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

void BlastQueries::clearAllQueries()
{
    for (size_t i = 0; i < m_queries.size(); ++i)
        delete m_queries[i];
    m_queries.clear();
}

void BlastQueries::clearSomeQueries(const std::vector<BlastQuery *> &queriesToRemove) {
    std::unordered_set<BlastQuery *> queries(queriesToRemove.begin(), queriesToRemove.end());
    m_queries.erase(std::remove_if(m_queries.begin(), m_queries.end(),
                                   [&](auto *query) { return queries.count(query); }),
                    m_queries.end());

    for (auto *query: queriesToRemove)
        delete query;
}

void BlastQueries::searchOccurred() {
    for (auto *query : m_queries)
        query->setAsSearchedFor();
}


void BlastQueries::clearSearchResults() {
    for (auto *query : m_queries)
        query->clearSearchResults();
}


int BlastQueries::getQueryCount() const
{
    return int(m_queries.size());
}

int BlastQueries::getQueryCountWithAtLeastOnePath() const {
    int count = 0;
    for (const auto *query : m_queries)
        count += (query->getPathCount() > 0);

    return count;
}

int BlastQueries::getQueryPathCount() const {
    int count = 0;
    for (const auto *query : m_queries)
        count += query->getPathCount();
    return count;
}

int BlastQueries::getQueryCount(QuerySequenceType sequenceType) const {
    int count = 0;
    for (const auto *query : m_queries)
        count += (query->getSequenceType() == sequenceType);
    return count;
}

//This function looks to see if a query pointer is in the list
//of queries.  The query pointer given may or may not still
//actually exist, so it can't be dereferenced.
bool BlastQueries::isQueryPresent(const BlastQuery * query) const {
    return std::find(m_queries.begin(), m_queries.end(), query) != m_queries.end();
}

//This function looks at each BLAST query and tries to find a path through
//the graph which covers the maximal amount of the query.
void BlastQueries::findQueryPaths() {
    for (auto *query : m_queries)
        query->findQueryPaths();
}
