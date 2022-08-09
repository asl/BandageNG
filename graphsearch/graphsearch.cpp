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

#include "graphsearch.h"
#include "graph/annotationsmanager.h"
#include "program/globals.h"

#include <QDir>
#include <QRegularExpression>
#include <QApplication>
#include <QProcess>

using namespace search;

GraphSearch::GraphSearch(const QDir &workDir, QObject *parent)
    : QObject(parent),
      m_queries(), m_tempDirectory(workDir.filePath("bandage_temp_XXXXXX")) {
    if (!m_tempDirectory.isValid())
        m_lastError = "A temporary directory could not be created.  BLAST search functionality will not be available. Error: " + m_tempDirectory.errorString();
}

GraphSearch::~GraphSearch() {
    clearHits();
    m_queries.clearAllQueries();
}

void GraphSearch::clearHits() {
    m_queries.clearSearchResults();
}

void GraphSearch::cleanUp() {
    clearHits();
    m_queries.clearAllQueries();
    emptyTempDirectory();
}

#ifdef Q_OS_WIN32
//On Windows, we use the WHERE command to find a program.
bool GraphSearch::findProgram(const QString &programName, QString * command)
{
    QProcess find;
    find.start("WHERE", QStringList(programName));
    find.waitForFinished();
    *command = programName;
    return (find.exitCode() == 0);
}

#else
//On Mac/Linux we use the which command to find a program.
bool GraphSearch::findProgram(const QString &programName, QString * command)
{
    QProcess find;

    //On Mac, it's necessary to adjust the PATH variable in order
    //for which to work.
#ifdef Q_OS_MAC
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList envlist = env.toStringList();

    //Add some paths to the process environment
    envlist.replaceInStrings(QRegularExpression("^(?i)PATH=(.*)"), "PATH="
                                                                   "/usr/bin:"
                                                                   "/bin:"
                                                                   "/usr/sbin:"
                                                                   "/sbin:"
                                                                   "/opt/local/bin:"
                                                                   "/usr/local/bin:"
                                                                   "/opt/homebrew/bin:"
                                                                   "$HOME/bin:"
                                                                   "$HOME/.local/bin:"
                                                                   "$HOME/miniconda3/bin:"
                                                                   "/usr/local/ncbi/blast/bin:"
                                                                   "\\1");
    find.setEnvironment(envlist);
#endif

    find.start("which", QStringList(programName));
    find.waitForFinished();

    //On a Mac, we need to use the full path to the program.
#ifdef Q_OS_MAC
    *command = QString(find.readAll()).simplified();
#else
    *command = programName;
#endif

    return (find.exitCode() == 0);
}
#endif

void GraphSearch::emptyTempDirectory() const {
    QDir tempDirectory(m_tempDirectory.path());
    tempDirectory.setNameFilters(QStringList() << "*.*");
    tempDirectory.setFilter(QDir::Files);
            foreach(QString dirFile, tempDirectory.entryList())
            tempDirectory.remove(dirFile);
}

QString GraphSearch::cleanQueryName(QString queryName) {
    //Replace whitespace with underscores
    queryName = queryName.replace(QRegularExpression("\\s"), "_");

    //Remove any dots from the end of the query name.  BLAST doesn't
    //include them in its results, so if we don't remove them, then
    //we won't be able to find a match between the query name and
    //the BLAST hit.
    while (queryName.length() > 0 && queryName[queryName.size() - 1] == '.')
        queryName = queryName.left(queryName.size() - 1);

    return queryName;
}