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

#pragma once

#include "graphsearch/graphsearch.h"

#include <QDir>
#include <QString>

// This is a class to hold all BLAST search related stuff.
// An instance of it is made available to the whole program
// as a global.

class QProcess;

namespace search {
class Queries;

class BlastSearch : public search::GraphSearch {
    Q_OBJECT
public:
    explicit BlastSearch(const QDir &workDir = QDir::temp(), QObject *parent = nullptr);
    virtual ~BlastSearch() = default;


    QString doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
                              bool includePaths = false,
                              QString extraParameters = "") override;
    int loadQueriesFromFile(QString fullFileName) override;
    QString buildDatabase(const AssemblyGraph &graph,
                          bool includePaths = true) override;
    QString doSearch(QString extraParameters) override;
    QString doSearch(search::Queries &queries, QString extraParameters) override;

    QString name() const override { return "BLAST"; }
    QString queryFormat() const override { return "FASTA"; }
    QString annotationGroupName() const override;

public slots:
    void cancelDatabaseBuild() override;
    void cancelSearch() override;

private:
    bool findTools();

    QString runOneBlastSearch(search::QuerySequenceType sequenceType,
                              const search::Queries &queries,
                              const QString &extraParameters,
                              bool &success);

    bool m_cancelBuildDatabase = false, m_cancelSearch = false;
    QProcess *m_buildDb = nullptr, *m_doSearch = nullptr;
    QString m_makeblastdbCommand, m_blastnCommand, m_tblastnCommand;
};

}
