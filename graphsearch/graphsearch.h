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
enum GraphSearchKind {
    BLAST = 0,
    Minimap2,
    NHMMER,
};

// This is a class to hold all graph node search related stuff.
class GraphSearch : public QObject {
    Q_OBJECT;
public:
    explicit GraphSearch(const QDir &workDir = QDir::temp(), QObject *parent = nullptr);
    ~GraphSearch();

    using NodeHits = std::vector<std::pair<Query*, Hit*>>;
    using PathHits = std::vector<std::pair<Path*, Path::MappingRange>>;

    // Small RAII helpers to emit signals on destruction
    class DbBuildFinishedRAII {
      public:
        DbBuildFinishedRAII(GraphSearch *search)
                : m_search(search) {}
        ~DbBuildFinishedRAII();
      private:
        GraphSearch *m_search;
    };

    class GraphSearchFinishedRAII {
      public:
        GraphSearchFinishedRAII(GraphSearch *search)
                : m_search(search) {}
        ~GraphSearchFinishedRAII();
      private:
        GraphSearch *m_search;
    };

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
    search::Query *getQueryFromName(QString queryName) const { return m_queries.getQueryFromName(queryName); }

    void clearHits();
    void cleanUp();

    [[nodiscard]] bool ready() const { return m_tempDirectory.isValid(); }
    [[nodiscard]] const QTemporaryDir &temporaryDir() const { return m_tempDirectory; }
    [[nodiscard]] QString lastError() const { return m_lastError; }

    void emptyTempDirectory() const;

    virtual int loadQueriesFromFile(QString fullFileName) = 0;
    virtual QString buildDatabase(const AssemblyGraph &graph, bool includePaths = true) = 0;
    virtual QString doSearch(QString extraParameters) = 0;
    virtual QString doSearch(search::Queries &queries, QString extraParameters) = 0;
    virtual QString doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
                                      bool includePaths = false,
                                      QString extraParameters = "") = 0;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual QString queryFormat() const = 0;
    [[nodiscard]] virtual QString annotationGroupName() const = 0;
    [[nodiscard]] virtual bool allowManualQueries() const { return true; }

    static std::unique_ptr<GraphSearch> get(GraphSearchKind kind,
                                            const QDir &workDir = QDir::temp(), QObject *parent = nullptr);

protected:
    static void addNodeHit(Query *query, DeBruijnNode *node,
                           int queryStart, int queryEnd,
                           int nodeStart, int nodeEnd,
                           double percentIdentity,
                           int numberMismatches, int numberGapOpens,
                           int alignmentLength, SciNot eValue, double bitScore);

    static void addPathHit(Query *query, Path *path,
                           int queryStart, int queryEnd,
                           int pathStart, int pathEnd);

public slots:
    virtual void cancelDatabaseBuild() {};
    virtual void cancelSearch() {};

signals:
    void finishedDbBuild(QString error);
    void finishedSearch(QString error);

protected:
    QString m_lastError;

private:
    Queries m_queries;
    QTemporaryDir m_tempDirectory;
};

}
