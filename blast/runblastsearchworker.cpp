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
        : m_blastnCommand(blastnCommand), m_tblastnCommand(tblastnCommand), m_parameters(parameters), m_blast(nullptr) {}


bool RunBlastSearchWorker::runBlastSearch() {
    m_cancelRunBlastSearch = false;
    bool success = false;

    QString blastOutput;
    if (g_blastSearch->m_blastQueries.getQueryCount(NUCLEOTIDE) > 0 && !m_cancelRunBlastSearch) {
        blastOutput += runOneBlastSearch(NUCLEOTIDE, &success);
        if (!success)
            return false;
    }

    if (g_blastSearch->m_blastQueries.getQueryCount(PROTEIN) > 0 && !m_cancelRunBlastSearch) {
        blastOutput += runOneBlastSearch(PROTEIN, &success);
        if (!success)
            return false;
    }

    if (m_cancelRunBlastSearch) {
        m_error = "BLAST search cancelled.";
        emit finishedSearch(m_error);
        return false;
    }

    //If the code got here, then the search completed successfully.
    g_blastSearch->buildHitsFromBlastOutput(blastOutput);
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