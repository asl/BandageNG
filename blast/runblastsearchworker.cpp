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
#include "blastsearch.h"

#include "program/globals.h"

#include <QTemporaryFile>
#include <QProcess>

RunBlastSearchWorker::RunBlastSearchWorker(QString blastnCommand, QString tblastnCommand, QString parameters)
        : m_blastnCommand(blastnCommand), m_tblastnCommand(tblastnCommand), m_parameters(parameters) {}


bool RunBlastSearchWorker::runBlastSearch() {
    g_blastSearch->m_cancelRunBlastSearch = false;

    bool success;

    if (g_blastSearch->m_blastQueries.getQueryCount(NUCLEOTIDE) > 0) {
        g_blastSearch->m_blastOutput += runOneBlastSearch(NUCLEOTIDE, &success);
        if (!success)
            return false;
    }

    if (g_blastSearch->m_blastQueries.getQueryCount(PROTEIN) > 0 && !g_blastSearch->m_cancelRunBlastSearch) {
        g_blastSearch->m_blastOutput += runOneBlastSearch(PROTEIN, &success);
        if (!success)
            return false;
    }

    if (g_blastSearch->m_cancelRunBlastSearch) {
        m_error = "BLAST search cancelled.";
        emit finishedSearch(m_error);
        return false;
    }

    //If the code got here, then the search completed successfully.
    g_blastSearch->buildHitsFromBlastOutput();
    g_blastSearch->findQueryPaths();
    g_blastSearch->m_blastQueries.searchOccurred();
    m_error = "";
    emit finishedSearch(m_error);

    return true;
}

static void writeQueryFile(QFile *file,
                           const BlastQueries &queries,
                           QuerySequenceType sequenceType) {
    QTextStream out(file);
    for (const auto *query: queries.m_queries) {
        if (query->getSequenceType() != sequenceType)
            continue;

        out << '>' << query->getName() << '\n'
            << query->getSequence()
            << '\n';
    }
}

QString RunBlastSearchWorker::runOneBlastSearch(QuerySequenceType sequenceType, bool * success) {
    QTemporaryFile tmpFile(g_blastSearch->m_tempDirectory.filePath(sequenceType == NUCLEOTIDE ?
                                            "nucl_queries.XXXXXX.fasta" : "prot_queries.XXXXXX.fasta"));
    if (!tmpFile.open()) {
        m_error = "Failed to create temporary query file";
        *success = false;
        return "";
    }

    writeQueryFile(&tmpFile, g_blastSearch->m_blastQueries, sequenceType);

    QStringList blastOptions;
    blastOptions << "-query" << tmpFile.fileName()
                 << "-db" << (g_blastSearch->m_tempDirectory.filePath("all_nodes.fasta"))
                 << "-outfmt" << "6";
    blastOptions << m_parameters.split(" ", Qt::SkipEmptyParts);
    
    g_blastSearch->m_blast = new QProcess();
    g_blastSearch->m_blast->start(sequenceType == NUCLEOTIDE ? m_blastnCommand : m_tblastnCommand,
                                  blastOptions);

    bool finished = g_blastSearch->m_blast->waitForFinished(-1);

    if (g_blastSearch->m_blast->exitCode() != 0 || !finished) {
        if (g_blastSearch->m_cancelRunBlastSearch) {
            m_error = "BLAST search cancelled.";
            emit finishedSearch(m_error);
        } else {
            m_error = "There was a problem running the BLAST search";
            QString stdErr = g_blastSearch->m_blast->readAllStandardError();
            m_error += stdErr.isEmpty() ? "." : ":\n\n" + stdErr;
            emit finishedSearch(m_error);
        }
        *success = false;
        return "";
    }

    QString blastOutput = g_blastSearch->m_blast->readAllStandardOutput();
    g_blastSearch->m_blast->deleteLater();
    g_blastSearch->m_blast = nullptr;

    *success = true;
    return blastOutput;
}
