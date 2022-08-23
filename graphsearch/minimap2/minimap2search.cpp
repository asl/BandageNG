// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

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


#include "minimap2search.h"

#include "program/settings.h"

#include "graph/assemblygraph.h"
#include "io/fileutils.h"

#include <QDir>
#include <QProcess>
#include <QTemporaryFile>
#include <cmath>

using namespace search;

Minimap2Search::Minimap2Search(const QDir &workDir, QObject *parent)
        : GraphSearch(workDir, parent) {}

bool Minimap2Search::findTools() {
    if (!findProgram("minimap2", &m_minimap2Command)) {
        m_lastError = "Error: The program minimap2 was not found.";
        return false;
    }

    return true;
}

QString Minimap2Search::buildDatabase(const AssemblyGraph &graph, bool includePaths) {
    DbBuildFinishedRAII watcher(this);
    m_lastError = "";
    if (!findTools())
        return m_lastError;

    if (m_buildDb)
        return (m_lastError = "Building is already in progress");

    m_cancelBuildDatabase = false;

    QFile file(temporaryDir().filePath("all_nodes.fasta"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return (m_lastError = "Failed to open: " + file.fileName());

    {
        QTextStream out(&file);
        for (const auto *node : graph.m_deBruijnGraphNodes) {
            if (m_cancelBuildDatabase)
                return (m_lastError = "Build cancelled.");

            out << node->getFasta(true, false, false);
        }

        if (includePaths) {
            for (auto it = graph.m_deBruijnGraphPaths.begin(); it != graph.m_deBruijnGraphPaths.end(); ++it) {
                if (m_cancelBuildDatabase)
                    return (m_lastError = "Build cancelled.");

                out << it.value()->getFasta(it.key().c_str());
            }
        }
    }

    // Make sure the graph has sequences
    bool atLeastOneSequence = false;
    for (const auto *node : graph.m_deBruijnGraphNodes) {
        if (!node->sequenceIsMissing()) {
            atLeastOneSequence = true;
            break;
        }
    }

    if (!atLeastOneSequence)
        return (m_lastError = "Cannot build the Minimap2 database as this graph contains no sequences");

    QStringList minimap2Options;
    minimap2Options << "-d" << temporaryDir().filePath("all_nodes.idx")
                    << temporaryDir().filePath("all_nodes.fasta");

    m_buildDb = new QProcess();
    m_buildDb->start(m_minimap2Command, minimap2Options);

    bool finished = m_buildDb->waitForFinished(-1);
    if (m_buildDb->exitCode() != 0 || !finished) {
        m_lastError = "There was a problem building minimap2 database";
        QString stdErr = m_buildDb->readAllStandardError();
        m_lastError += stdErr.isEmpty() ? "." : ":\n\n" + stdErr;
    } else if (m_cancelBuildDatabase)
        m_lastError = "Build cancelled.";
    else
        m_lastError = "";

    m_buildDb->deleteLater();
    m_buildDb = nullptr;

    return m_lastError;
}

QString Minimap2Search::doSearch(QString extraParameters) {
    return doSearch(queries(), extraParameters);
}

static void writeQueryFile(QFile *file,
                           const Queries &queries) {
    QTextStream out(file);
    for (const auto *query: queries.queries()) {
        out << '>' << query->getName() << '\n'
            << query->getSequence()
            << '\n';
    }
}

// FIXME: Factor out common functionality
static QString getNodeNameFromString(const QString &nodeString) {
    QStringList nodeStringParts = nodeString.split("_");

    // The node string format should look like this:
    // NODE_nodename_length_123_cov_1.23
    if (nodeStringParts.size() < 6)
        return "";

    if (nodeStringParts.size() == 6)
        return nodeStringParts[1];

    // If the code got here, there are more than 6 parts.  This means there are
    // underscores in the node name (happens a lot with Trinity graphs).  So we
    // need to pull out the parts which constitute the name.
    int underscoreCount = nodeStringParts.size() - 6;
    QString nodeName = "";
    for (int i = 0; i <= underscoreCount; ++i) {
        nodeName += nodeStringParts[1+i];
        if (i < underscoreCount)
            nodeName += "_";
    }

    return nodeName;
}

void Minimap2Search::buildHitsFromPAF(const QString &PAF,
                                      Queries &queries) const {
    QStringList hitList = PAF.split("\n", Qt::SkipEmptyParts);

    for (const auto &hitString : hitList) {
        QStringList alignmentParts = hitString.split('\t');
        if (alignmentParts.size() < 12)
            continue;

        QString queryName = alignmentParts[0];
        int queryStart = alignmentParts[2].toInt() + 1;
        int queryEnd = alignmentParts[3].toInt();
        bool strand = alignmentParts[4] == '+';

        QString nodeLabel = alignmentParts[5];
        int nodeStart = alignmentParts[7].toInt() + 1;
        int nodeEnd = alignmentParts[8].toInt();

        int alignmentLength = alignmentParts[10].toInt();

        Query *query = queries.getQueryFromName(queryName);
        if (query == nullptr)
            continue;

        auto nodeIt = g_assemblyGraph->m_deBruijnGraphNodes.find(getNodeNameFromString(nodeLabel).toStdString());
        if (nodeIt != g_assemblyGraph->m_deBruijnGraphNodes.end()) {
            if (!strand)
                continue;

            addNodeHit(query, nodeIt.value(),
                       queryStart, queryEnd,
                       nodeStart, nodeEnd,
                       -1, -1, -1,
                       alignmentLength, 0, 0);
        }

        auto pathIt = g_assemblyGraph->m_deBruijnGraphPaths.find(nodeLabel.toStdString());
        if (pathIt != g_assemblyGraph->m_deBruijnGraphPaths.end()) {
            addPathHit(query, pathIt.value(),
                       queryStart, queryEnd,
                       nodeStart, nodeEnd);
        }

    }
}

QString Minimap2Search::doSearch(Queries &queries, QString extraParameters) {
    GraphSearchFinishedRAII watcher(this);
    
    m_lastError = "";
    if (!findTools())
        return m_lastError;

    // FIXME: Do we need proper mutex here?
    if (m_doSearch)
        return (m_lastError = "Search is already in progress");

    for (const auto *query: queries.queries()) {
        if (query->getSequenceType() != search::NUCLEOTIDE)
            return (m_lastError = "Cannot handle non-nucleotide query: " + query->getName() + ". Remove it and retry search.");
    }

    QTemporaryFile tmpFile(temporaryDir().filePath("queries.XXXXXX.fasta"));
    if (!tmpFile.open())
        return (m_lastError = "Failed to create temporary query file");

    writeQueryFile(&tmpFile, queries);

    QStringList minimap2Options;
    minimap2Options << extraParameters.split(" ", Qt::SkipEmptyParts)
                    << temporaryDir().filePath("all_nodes.idx")
                    << tmpFile.fileName();

    m_cancelSearch = false;
    m_doSearch = new QProcess();
    m_doSearch->start(m_minimap2Command, minimap2Options);

    bool finished = m_doSearch->waitForFinished(-1);

    if (m_doSearch->exitCode() != 0 || !finished) {
        if (m_cancelSearch) {
            m_lastError = "Minimap2 search cancelled.";
        } else {
            m_lastError = "There was a problem running the Minimap2 search";
            QString stdErr = m_doSearch->readAllStandardError();
            m_lastError += stdErr.isEmpty() ? "." : ":\n\n" + stdErr;
        }

        m_doSearch->deleteLater();
        m_doSearch = nullptr;

        return m_lastError;
    }

    QString minimap2Output = m_doSearch->readAllStandardOutput();
    m_doSearch->deleteLater();
    m_doSearch = nullptr;

    if (m_cancelSearch)
        return (m_lastError = "Minimap2 search cancelled");

    buildHitsFromPAF(minimap2Output, queries);
    queries.findQueryPaths();
    queries.searchOccurred();

    m_lastError = "";

    return m_lastError;
}

QString Minimap2Search::doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
                                          bool includePaths,
                                          QString extraParameters) {
    cleanUp();

    QString maybeError = buildDatabase(graph, includePaths); // It is expected that buildDatabase will setup last error as well
    if (!maybeError.isEmpty())
        return maybeError;

    loadQueriesFromFile(queriesFilename);

    maybeError = doSearch(queries(), extraParameters);
    if (!maybeError.isEmpty())
        return maybeError;

    return "";
}

//This function returns the number of queries loaded from the FASTA file.
int Minimap2Search::loadQueriesFromFile(QString fullFileName) {
    m_lastError = "";
    int queriesBefore = int(getQueryCount());

    std::vector<QString> queryNames;
    std::vector<QByteArray> querySequences;
    if (!utils::readFastxFile(fullFileName, queryNames, querySequences)) {
        m_lastError = "Failed to parse FASTA file: " + fullFileName;
        return 0;
    }

    for (size_t i = 0; i < queryNames.size(); ++i) {
        //We only use the part of the query name up to the first space.
        QStringList queryNameParts = queryNames[i].split(" ");
        QString queryName;
        if (!queryNameParts.empty())
            queryName = cleanQueryName(queryNameParts[0]);

        addQuery(new Query(queryName, querySequences[i]));
    }

    int queriesAfter = int(getQueryCount());
    return queriesAfter - queriesBefore;
}

void Minimap2Search::cancelDatabaseBuild() {
    if (!m_buildDb)
        return;

    m_cancelBuildDatabase = true;
    if (m_buildDb)
        m_buildDb->kill();
}

void Minimap2Search::cancelSearch() {
    if (!m_doSearch)
        return;

    m_cancelSearch = true;
    if (m_doSearch)
        m_doSearch->kill();
}
