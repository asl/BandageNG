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


#ifndef BLASTSEARCH_H
#define BLASTSEARCH_H

#include "blastqueries.h"

#include <QDir>
#include <QString>
#include <QTemporaryDir>

//This is a class to hold all BLAST search related stuff.
//An instance of it is made available to the whole program
//as a global.
class BlastSearch {
public:
    explicit BlastSearch(const QDir &workDir = QDir::temp());
    ~BlastSearch();

    QTemporaryDir m_tempDirectory;

    [[nodiscard]] const auto &queries() const { return m_blastQueries; }
    auto &queries() { return m_blastQueries; }
    [[nodiscard]] const auto &query(size_t idx) const { return m_blastQueries[idx]; }
    auto &query(size_t idx) { return m_blastQueries[idx]; }
    bool isQueryPresent(const BlastQuery *query) const { return m_blastQueries.isQueryPresent(query); }
    size_t getQueryCount() const { return m_blastQueries.getQueryCount(); }
    size_t getQueryCountWithAtLeastOnePath() const { return m_blastQueries.getQueryCountWithAtLeastOnePath(); }
    size_t getQueryPathCount() const { return m_blastQueries.getQueryPathCount(); }
    size_t getQueryCount(QuerySequenceType sequenceType) const { return m_blastQueries.getQueryCount(sequenceType); }

    static bool findProgram(const QString& programName, QString * command);
    int loadBlastQueriesFromFastaFile(QString fullFileName);
    static QString cleanQueryName(QString queryName);
    void blastQueryChanged(const QString& queryName);
    void addQuery(BlastQuery *newQuery) { m_blastQueries.addQuery(newQuery); }
    BlastQuery * getQueryFromName(QString queryName) const { return m_blastQueries.getQueryFromName(queryName); }

    void clearBlastHits();
    void cleanUp();

    void emptyTempDirectory() const;
    QString doAutoBlastSearch();

private:
    BlastQueries m_blastQueries;
};

#endif // BLASTSEARCH_H
