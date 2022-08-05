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

#include "blasthit.h"
#include "blastqueries.h"

#include <QString>
#include <QDir>
#include <QTemporaryDir>

#include <vector>

//This is a class to hold all BLAST search related stuff.
//An instance of it is made available to the whole program
//as a global.

class QProcess;


using BlastHits = std::vector<std::shared_ptr<BlastHit>>;

class BlastSearch
{
public:
    explicit BlastSearch(const QDir &workDir = QDir::temp());
    ~BlastSearch();

    BlastQueries m_blastQueries;
    QString m_blastOutput;
    bool m_cancelRunBlastSearch{};
    QProcess *m_makeblastdb{};
    QProcess *m_blast{};
    QTemporaryDir m_tempDirectory;
    BlastHits m_allHits;

    static bool findProgram(const QString& programName, QString * command);
    int loadBlastQueriesFromFastaFile(QString fullFileName);
    static QString cleanQueryName(QString queryName);
    void blastQueryChanged(const QString& queryName);

    void clearBlastHits();
    void cleanUp();
    void buildHitsFromBlastOutput();
    void findQueryPaths();
    void clearSomeQueries(const std::vector<BlastQuery *> &queriesToRemove);
    void emptyTempDirectory() const;
    QString doAutoBlastSearch();
};

#endif // BLASTSEARCH_H
