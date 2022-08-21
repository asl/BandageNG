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


#include "hmmersearch.h"

#include "graph/debruijnnode.h"
#include "program/settings.h"

#include "graph/assemblygraph.h"
#include "io/fileutils.h"
#include "seq/sequence.hpp"

#include <QDir>
#include <QRegularExpression>
#include <QProcess>
#include <QTemporaryFile>

#include <cmath>

using namespace search;

HmmerSearch::HmmerSearch(const QDir &workDir, QObject *parent)
        : GraphSearch(workDir, parent) {}

bool HmmerSearch::findTools() {
    if (!findProgram("nhmmer", &m_hmmerCommand)) {
        m_lastError = "Error: The program nhmmer was not found.";
        return false;
    }

    return true;
}

QString HmmerSearch::buildDatabase(const AssemblyGraph &graph) {
    DbBuildFinishedRAII watcher(this);

    m_lastError = "";
    if (!findTools())
        return m_lastError;
    
    if (m_buildDb)
        return (m_lastError = "Building is already in progress");

    {
        QFile file(temporaryDir().filePath("all_nodes.fna"));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return (m_lastError = "Failed to open: " + file.fileName());

        QTextStream out(&file);
        // nhmmer alphabet detection has a bug (https://github.com/tseemann/barrnap/issues/54)
        // in order to mitigate this, emit the largest (=more diverse) node first
        const DeBruijnNode *longest = nullptr;
        size_t length = 0;
        for (const auto *node : graph.m_deBruijnGraphNodes) {
            if (!node->sequenceIsMissing() && node->getLength() > length) {
                length = node->getLength();
                longest = node;
            }
        }

        if (!longest)
            return (m_lastError = "Cannot build the hmmer input set as this graph contains no sequences");

        out << longest->getFasta(true, false, false);
    
        for (const auto *node : graph.m_deBruijnGraphNodes) {
            if (m_cancelBuildDatabase)
                return (m_lastError = "Build cancelled.");

            if (node == longest)
                continue;;
            
            out << node->getFasta(true, false, false);
        }
    }
    
    return m_lastError;
}

QString HmmerSearch::doSearch(QString extraParameters) {
    return doSearch(queries(), extraParameters);
}

static void writeQueryFile(QFile *file,
                           const Queries &queries) {
    QTextStream out(file);
    for (const auto *query: queries.queries()) {
        out << query->getAuxData()
            << "//\n";
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

static void buildHitsFromTblOut(QString hmmerOutput,
                                Queries &queries);

QString HmmerSearch::doSearch(Queries &queries, QString extraParameters) {
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

    QTemporaryFile tmpQueryFile(temporaryDir().filePath("queries.XXXXXX.hmm"));
    if (!tmpQueryFile.open())
        return (m_lastError = "Failed to create temporary query file");

    writeQueryFile(&tmpQueryFile, queries);

    QTemporaryFile tmpOutFile(temporaryDir().filePath("hits.XXXXXX.tblout"));
    if (!tmpOutFile.open())
        return (m_lastError = "Failed to create temporary output file");
    
    tmpOutFile.setAutoRemove(false);

    QStringList hmmerOptions;
    hmmerOptions << "--tblout" << tmpOutFile.fileName()
                 << extraParameters.split(" ", Qt::SkipEmptyParts)
                 << tmpQueryFile.fileName()            
                 << temporaryDir().filePath("all_nodes.fna");

    m_cancelSearch = false;
    m_doSearch = new QProcess();
    m_doSearch->start(m_hmmerCommand, hmmerOptions);

    bool finished = m_doSearch->waitForFinished(-1);

    if (m_doSearch->exitCode() != 0 || !finished) {
        if (m_cancelSearch) {
            m_lastError = "HMMER search cancelled.";
        } else {
            m_lastError = "There was a problem running the HMMER search";
            QString stdErr = m_doSearch->readAllStandardError();
            m_lastError += stdErr.isEmpty() ? "." : ":\n\n" + stdErr;
        }

        m_doSearch = nullptr;

        return m_lastError;
    }

    QString hmmerOutput = tmpOutFile.readAll();
    m_doSearch->deleteLater();
    m_doSearch = nullptr;

    if (m_cancelSearch)
        return (m_lastError = "HMMER search cancelled");

    m_lastError = hmmerOutput;
    
    buildHitsFromTblOut(hmmerOutput, queries);
    queries.findQueryPaths();
    queries.searchOccurred();

    m_lastError = "";

    return m_lastError;
}

QString HmmerSearch::doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
                                          QString extraParameters) {
    cleanUp();

    QString maybeError = buildDatabase(graph); // It is expected that buildDatabase will setup last error as well
    if (!maybeError.isEmpty())
        return maybeError;

    loadQueriesFromFile(queriesFilename);

    maybeError = doSearch(queries(), extraParameters);
    if (!maybeError.isEmpty())
        return maybeError;

    return "";
}

// This function returns the number of queries loaded from the HMM file.
int HmmerSearch::loadQueriesFromFile(QString fullFileName) {
    m_lastError = "";
    if (!findTools())
        return 0;
    
    int queriesBefore = int(getQueryCount());

    std::vector<QString> queryNames;
    std::vector<QByteArray> querySequences;
    std::vector<unsigned> queryLengths;
    std::vector<bool> queryProtHmms;
    if (!utils::readHmmFile(fullFileName,
                            queryNames, queryLengths, querySequences, queryProtHmms)) {
        m_lastError = "Failed to parse HMM file: " + fullFileName;
        return 0;
    }

    for (size_t i = 0; i < queryNames.size(); ++i)
        addQuery(new Query(cleanQueryName(queryNames[i]),
                           QString(queryLengths[i],
                                   queryProtHmms[i] ? 'F' : 'N'),
                           querySequences[i]));

    int queriesAfter = int(getQueryCount());
    return queriesAfter - queriesBefore;
}

void HmmerSearch::cancelDatabaseBuild() {
    if (!m_buildDb)
        return;

    m_cancelBuildDatabase = true;
    if (m_buildDb)
        m_buildDb->kill();
}

void HmmerSearch::cancelSearch() {
    if (!m_doSearch)
        return;

    m_cancelSearch = true;
    if (m_doSearch)
        m_doSearch->kill();
}

static void buildHitsFromTblOut(QString hmmerOutput,
                                Queries &queries) {
    QStringList hmmerHitList = hmmerOutput.split("\n", Qt::SkipEmptyParts);

    for (const auto &hitString : hmmerHitList) {
        if (hitString.startsWith('#'))
            continue;
        
        QStringList alignmentParts = hitString.split(' ', Qt::SkipEmptyParts);
        if (alignmentParts.size() < 16)
            continue;

        QString nodeLabel = alignmentParts[0];
        QString queryName = alignmentParts[2];

        double percentIdentity = -1;
        int numberMismatches = -1;
        int numberGapOpens = -1;

        int queryStart = alignmentParts[4].toInt();
        int queryEnd = alignmentParts[5].toInt();
        
        int nodeStart = alignmentParts[6].toInt();
        int nodeEnd = alignmentParts[7].toInt();

        int alignmentLength = nodeEnd - nodeStart + 1;

        SciNot eValue(alignmentParts[12]);
        double bitScore = alignmentParts[13].toDouble();

        // Only save hits that are on forward strands.
        if (nodeStart > nodeEnd)
            continue;

        QString nodeName = getNodeNameFromString(nodeLabel);
        DeBruijnNode *node = nullptr;
        auto it = g_assemblyGraph->m_deBruijnGraphNodes.find(nodeName.toStdString());
        if (it == g_assemblyGraph->m_deBruijnGraphNodes.end())
            continue;

        node = it.value();

        Query *query = queries.getQueryFromName(queryName);
        if (query == nullptr)
            continue;

        // Check the user-defined filters.
        if (g_settings->blastAlignmentLengthFilter.on &&
            alignmentLength < g_settings->blastAlignmentLengthFilter)
            continue;

        if (g_settings->blastEValueFilter.on &&
            eValue > g_settings->blastEValueFilter)
            continue;

        if (g_settings->blastBitScoreFilter.on &&
            bitScore < g_settings->blastBitScoreFilter)
            continue;

        Hit hit(query, node, percentIdentity, alignmentLength,
                numberMismatches, numberGapOpens, queryStart, queryEnd,
                nodeStart, nodeEnd, eValue, bitScore);

        if (g_settings->blastQueryCoverageFilter.on) {
            double hitCoveragePercentage = 100.0 * hit.getQueryCoverageFraction();
            if (hitCoveragePercentage < g_settings->blastQueryCoverageFilter)
                continue;
        }

        query->addHit(hit);
    }
}
