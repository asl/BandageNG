// Copyright 2017 Ryan Wick
// Copyright 2022 Anton Korobeynikov

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


#include "blastsearch.h"

#include "program/settings.h"

#include "graph/assemblygraph.h"
#include "io/fileutils.h"

#include <QDir>
#include <QProcess>
#include <QTemporaryFile>

#include <cmath>

using namespace search;

BlastSearch::BlastSearch(const QDir &workDir, QObject *parent)
  : GraphSearch(workDir, parent) {}

bool BlastSearch::findTools() {
    if (!findProgram("makeblastdb", &m_makeblastdbCommand)) {
        m_lastError = "Error: The program makeblastdb was not found.  Please install NCBI BLAST to use this feature.";
        return false;
    }
    if (!findProgram("blastn", &m_blastnCommand)) {
        m_lastError = "Error: The program blastn was not found.  Please install NCBI BLAST to use this feature.";
        return false;
    }
    if (!findProgram("tblastn", &m_tblastnCommand)) {
        m_lastError = "Error: The program tblastn was not found.  Please install NCBI BLAST to use this feature.";
        return false;
    }

    return true;
}

QString BlastSearch::buildDatabase(const AssemblyGraph &graph, bool includePaths) {
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

    QStringList makeBlastdbOptions;
    makeBlastdbOptions << "-in" << temporaryDir().filePath("all_nodes.fasta")
                       << "-dbtype" << "nucl";

    m_buildDb = new QProcess();
    m_buildDb->start(m_makeblastdbCommand, makeBlastdbOptions);

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

QString BlastSearch::doSearch(QString extraParameters) {
    return doSearch(queries(), extraParameters);
}

static void writeQueryFile(QFile *file,
                           const Queries &queries,
                           QuerySequenceType sequenceType) {
    QTextStream out(file);
    for (const auto *query: queries.queries()) {
        if (query->getSequenceType() != sequenceType)
            continue;

        out << '>' << query->getName() << '\n'
            << query->getSequence()
            << '\n';
    }
}

QString BlastSearch::runOneBlastSearch(QuerySequenceType sequenceType,
                                       const Queries &queries,
                                       const QString &extraParameters,
                                       bool &success) {
    QTemporaryFile tmpFile(temporaryDir().filePath(sequenceType == NUCLEOTIDE ?
                                                   "nucl_queries.XXXXXX.fasta" : "prot_queries.XXXXXX.fasta"));
    if (!tmpFile.open()) {
        m_lastError = "Failed to create temporary query file";
        success = false;
        return "";
    }

    writeQueryFile(&tmpFile, queries, sequenceType);

    QStringList blastOptions;
    blastOptions << "-query" << tmpFile.fileName()
                 << "-db" << temporaryDir().filePath("all_nodes.fasta")
                 << "-outfmt" << "6";
    blastOptions << extraParameters.split(" ", Qt::SkipEmptyParts);

    m_doSearch = new QProcess();
    m_doSearch->start(sequenceType == NUCLEOTIDE ? m_blastnCommand : m_tblastnCommand,
                      blastOptions);

    bool finished = m_doSearch->waitForFinished(-1);
    if (m_doSearch->exitCode() != 0 || !finished) {
        if (m_cancelSearch) {
            m_lastError = "BLAST search cancelled.";
        } else {
            m_lastError = "There was a problem running the BLAST search";
            QString stdErr = m_doSearch->readAllStandardError();
            m_lastError += stdErr.isEmpty() ? "." : ":\n\n" + stdErr;
        }
        m_doSearch->deleteLater();
        m_doSearch = nullptr;

        success = false;
        return m_lastError;
    }

    QString blastOutput = m_doSearch->readAllStandardOutput();
    m_doSearch->deleteLater();
    m_doSearch = nullptr;

    success = true;
    return blastOutput;
}

static std::pair<NodeHits, PathHits>
buildHitsFromBlastOutput(QString blastOutput,
                         Queries &queries);

QString BlastSearch::doSearch(Queries &queries, QString extraParameters) {
    GraphSearchFinishedRAII watcher(this);

    m_lastError = "";
    if (!findTools())
        return m_lastError;

    // FIXME: Do we need proper mutex here?
    if (m_doSearch)
        return (m_lastError = "Search is already in progress");

    m_cancelSearch = false;

    QString blastOutput;
    bool success = false;
    if (queries.getQueryCount(NUCLEOTIDE) > 0 && !m_cancelSearch) {
        blastOutput += runOneBlastSearch(NUCLEOTIDE, queries, extraParameters, success);
        if (!success)
            return m_lastError;
    }

    if (queries.getQueryCount(PROTEIN) > 0 && !m_cancelSearch) {
        blastOutput += runOneBlastSearch(PROTEIN, queries, extraParameters, success);
        if (!success)
            return m_lastError;
    }

    if (m_cancelSearch)
        return (m_lastError = "BLAST search cancelled");

    // If the code got here, then the search completed successfully.
    auto [nodeHits, pathHits] = buildHitsFromBlastOutput(blastOutput, queries);
    queries.addNodeHits(nodeHits);
    queries.findQueryPaths();
    queries.addPathHits(pathHits);
    queries.searchOccurred();

    m_lastError = "";
    return m_lastError;
}

//This function carries out the entire BLAST search procedure automatically, without user input.
//It returns an error string which is empty if all goes well.
QString BlastSearch::doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
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
int BlastSearch::loadQueriesFromFile(QString fullFileName) {
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

void BlastSearch::cancelDatabaseBuild() {
    if (!m_buildDb)
        return;

    m_cancelBuildDatabase = true;
    if (m_buildDb)
        m_buildDb->kill();
}

void BlastSearch::cancelSearch() {
    if (!m_doSearch)
        return;

    m_cancelSearch = true;
    if (m_doSearch)
        m_doSearch->kill();
}

QString BlastSearch::annotationGroupName() const {
    return g_settings->blastAnnotationGroupName;
}

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

// This function uses the contents of blastOutput (the raw output from the
// BLAST search) to construct the Hit objects.
// It looks at the filters to possibly exclude hits which fail to meet user-
// defined thresholds.
static std::pair<NodeHits, PathHits>
buildHitsFromBlastOutput(QString blastOutput,
                         Queries &queries) {
    NodeHits nodeHits; PathHits pathHits;

    QStringList blastHitList = blastOutput.split("\n", Qt::SkipEmptyParts);

    for (const auto &hitString : blastHitList) {
        QStringList alignmentParts = hitString.split('\t');

        if (alignmentParts.size() < 12)
            continue;

        QString queryName = alignmentParts[0];
        QString nodeLabel = alignmentParts[1];
        double percentIdentity = alignmentParts[2].toDouble();
        int alignmentLength = alignmentParts[3].toInt();
        int numberMismatches = alignmentParts[4].toInt();
        int numberGapOpens = alignmentParts[5].toInt();
        int queryStart = alignmentParts[6].toInt();
        int queryEnd = alignmentParts[7].toInt();
        int nodeStart = alignmentParts[8].toInt();
        int nodeEnd = alignmentParts[9].toInt();
        SciNot eValue(alignmentParts[10]);
        double bitScore = alignmentParts[11].toDouble();

        Query *query = queries.getQueryFromName(queryName);
        if (query == nullptr)
            continue;

        // Check the user-defined filters.
        if (g_settings->blastIdentityFilter.on &&
            percentIdentity < g_settings->blastIdentityFilter)
            continue;

        if (g_settings->blastEValueFilter.on &&
            eValue > g_settings->blastEValueFilter)
            continue;

        if (g_settings->blastBitScoreFilter.on &&
            bitScore < g_settings->blastBitScoreFilter)
            continue;

        if (g_settings->blastAlignmentLengthFilter.on &&
            alignmentLength < g_settings->blastAlignmentLengthFilter)
            continue;

        if (g_settings->blastQueryCoverageFilter.on) {
            double hitCoveragePercentage = 100.0 * Hit::getQueryCoverageFraction(query,
                                                                                 queryStart, queryEnd);
            if (hitCoveragePercentage < g_settings->blastQueryCoverageFilter)
                continue;
        }

        auto nodeIt = g_assemblyGraph->m_deBruijnGraphNodes.find(getNodeNameFromString(nodeLabel).toStdString());
        if (nodeIt != g_assemblyGraph->m_deBruijnGraphNodes.end()) {
            // Only save BLAST hits that are on forward strands.
            if (nodeStart > nodeEnd)
                continue;

            nodeHits.emplace_back(query,
                                  new Hit(query, nodeIt.value(),
                                          percentIdentity, alignmentLength,
                                          numberMismatches, numberGapOpens,
                                          queryStart, queryEnd,
                                          nodeStart, nodeEnd, eValue, bitScore));
        }

        auto pathIt = g_assemblyGraph->m_deBruijnGraphPaths.find(nodeLabel.toStdString());
        if (pathIt != g_assemblyGraph->m_deBruijnGraphPaths.end()) {
            pathHits.emplace_back(query, pathIt.value(),
                                  Path::MappingRange{queryStart, queryEnd,
                                                     nodeStart, nodeEnd});
        }

    }

    return { nodeHits, pathHits };
}
