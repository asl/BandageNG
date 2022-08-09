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

#include "graphsearch/queries.h"

#include <QDir>
#include <QString>
#include <QTemporaryDir>

namespace search {
// This is a class to hold all graph node search related stuff.
class GraphSearch {
public:
    explicit GraphSearch(const QDir &workDir = QDir::temp());
    ~GraphSearch();

    [[nodiscard]] const auto &queries() const { return m_queries; }
    auto &queries() { return m_queries; }
    [[nodiscard]] const auto &query(size_t idx) const { return m_queries[idx]; }
    auto &query(size_t idx) { return m_queries[idx]; }
    bool isQueryPresent(const Query *query) const { return m_queries.isQueryPresent(query); }
    size_t getQueryCount() const { return m_queries.getQueryCount(); }
    size_t getQueryCountWithAtLeastOnePath() const { return m_queries.getQueryCountWithAtLeastOnePath(); }
    size_t getQueryPathCount() const { return m_queries.getQueryPathCount(); }
    size_t getQueryCount(QuerySequenceType sequenceType) const { return m_queries.getQueryCount(sequenceType); }

    static bool findProgram(const QString& programName, QString * command);

    static QString cleanQueryName(QString queryName);
    void addQuery(Query *newQuery) { m_queries.addQuery(newQuery); }
    search::Query * getQueryFromName(QString queryName) const { return m_queries.getQueryFromName(queryName); }

    void clearHits();
    void cleanUp();

    bool ready() const { return m_tempDirectory.isValid(); }
    [[nodiscard]] const QTemporaryDir &temporaryDir() const { return m_tempDirectory; }

    void emptyTempDirectory() const;

    virtual QString doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
                                      QString extraParameters = "") = 0;
    QString lastError() const { return m_lastError; }

protected:
    QString m_lastError;

private:
    Queries m_queries;
    QTemporaryDir m_tempDirectory;
};

}