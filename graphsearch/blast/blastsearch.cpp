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


#include "blastsearch.h"
#include "buildblastdatabaseworker.h"
#include "runblastsearchworker.h"

#include "program/settings.h"

#include "graph/assemblygraph.h"
#include "io/fileutils.h"

#include <QDir>
#include <QRegularExpression>
#include <QProcess>
#include <cmath>
#include <unordered_set>

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

QString BlastSearch::buildDatabase(const AssemblyGraph &graph) {
    m_lastError = "";
    if (!findTools())
        return m_lastError;

    if (m_buildDbWorker)
        return (m_lastError = "Building is already in progress");

    m_buildDbWorker = new BuildBlastDatabaseWorker(m_makeblastdbCommand, graph, temporaryDir());
    if (!m_buildDbWorker->buildBlastDatabase())
        m_lastError = m_buildDbWorker->m_error;

    m_buildDbWorker->deleteLater();
    m_buildDbWorker = nullptr;

    emit finishedDbBuild(m_lastError);
    return m_lastError;
}

QString BlastSearch::doSearch(QString extraParameters) {
    return doSearch(queries(), extraParameters);
}

QString BlastSearch::doSearch(Queries &queries, QString extraParameters) {
    m_lastError = "";
    if (!findTools())
        return m_lastError;

    if (m_runSearchWorker)
        return (m_lastError = "Search is already in progress");

    m_runSearchWorker = new RunBlastSearchWorker(m_blastnCommand, m_tblastnCommand, extraParameters, temporaryDir());
    if (!m_runSearchWorker->runBlastSearch(queries))
         m_lastError = m_runSearchWorker->m_error;

    m_runSearchWorker->deleteLater();
    m_runSearchWorker = nullptr;

    emit finishedSearch(m_lastError);
    return m_lastError;
}

//This function carries out the entire BLAST search procedure automatically, without user input.
//It returns an error string which is empty if all goes well.
QString BlastSearch::doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
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
    if (!m_buildDbWorker)
        return;

    emit m_buildDbWorker->cancel();
}

void BlastSearch::cancelSearch() {
    if (!m_runSearchWorker)
        return;

    emit m_runSearchWorker->cancel();
}

QString BlastSearch::annotationGroupName() const {
    return g_settings->blastAnnotationGroupName;
}
