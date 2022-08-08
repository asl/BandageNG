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


#include "runblastsearchworker.h"

#include "blastqueries.h"

#include "graph/assemblygraph.h"

#include "program/globals.h"
#include "program/settings.h"

#include <QTemporaryFile>
#include <QProcess>

using namespace search;

RunBlastSearchWorker::RunBlastSearchWorker(QString blastnCommand, QString tblastnCommand, QString parameters,
                                           const QTemporaryDir &workdir)
        : m_blastnCommand(blastnCommand), m_tblastnCommand(tblastnCommand), m_parameters(parameters),
          m_tempDirectory(workdir) {}


static void buildHitsFromBlastOutput(QString blastOutput,
                                     Queries &queries);

    bool RunBlastSearchWorker::runBlastSearch(Queries &queries) {
    m_cancelRunBlastSearch = false;
    bool success = false;

    QString blastOutput;
    if (queries.getQueryCount(NUCLEOTIDE) > 0 && !m_cancelRunBlastSearch) {
        blastOutput += runOneBlastSearch(NUCLEOTIDE, queries, &success);
        if (!success)
            return false;
    }

    if (queries.getQueryCount(PROTEIN) > 0 && !m_cancelRunBlastSearch) {
        blastOutput += runOneBlastSearch(PROTEIN, queries, &success);
        if (!success)
            return false;
    }

    if (m_cancelRunBlastSearch) {
        m_error = "BLAST search cancelled.";
        emit finishedSearch(m_error);
        return false;
    }

    // If the code got here, then the search completed successfully.
    buildHitsFromBlastOutput(blastOutput, queries);
    queries.findQueryPaths();
    queries.searchOccurred();
    m_error = "";
    emit finishedSearch(m_error);

    return true;
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

QString RunBlastSearchWorker::runOneBlastSearch(QuerySequenceType sequenceType,
                                                const Queries &queries,
                                                bool * success) {
    QTemporaryFile tmpFile(m_tempDirectory.filePath(sequenceType == NUCLEOTIDE ?
                           "nucl_queries.XXXXXX.fasta" : "prot_queries.XXXXXX.fasta"));
    if (!tmpFile.open()) {
        m_error = "Failed to create temporary query file";
        *success = false;
        return "";
    }

    writeQueryFile(&tmpFile, queries, sequenceType);

    QStringList blastOptions;
    blastOptions << "-query" << tmpFile.fileName()
                 << "-db" << m_tempDirectory.filePath("all_nodes.fasta")
                 << "-outfmt" << "6";
    blastOptions << m_parameters.split(" ", Qt::SkipEmptyParts);
    
    m_blast = new QProcess();
    m_blast->start(sequenceType == NUCLEOTIDE ? m_blastnCommand : m_tblastnCommand,
                   blastOptions);

    bool finished = m_blast->waitForFinished(-1);

    if (m_blast->exitCode() != 0 || !finished) {
        if (m_cancelRunBlastSearch) {
            m_error = "BLAST search cancelled.";
            emit finishedSearch(m_error);
        } else {
            m_error = "There was a problem running the BLAST search";
            QString stdErr = m_blast->readAllStandardError();
            m_error += stdErr.isEmpty() ? "." : ":\n\n" + stdErr;
            emit finishedSearch(m_error);
        }
        *success = false;
        return "";
    }

    QString blastOutput = m_blast->readAllStandardOutput();
    m_blast->deleteLater();
    m_blast = nullptr;

    *success = true;
    return blastOutput;
}

void RunBlastSearchWorker::cancel() {
    m_cancelRunBlastSearch = true;

    if (m_blast) {
        m_blast->kill();
        m_blast->deleteLater();
        m_blast = nullptr;
    }
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
static void buildHitsFromBlastOutput(QString blastOutput,
                                     Queries &queries) {
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

        //Only save BLAST hits that are on forward strands.
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

        if (g_settings->blastIdentityFilter.on &&
            percentIdentity < g_settings->blastIdentityFilter)
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
