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

#pragma once

#include "graphsearch/graphsearch.h"

#include <QDir>
#include <QString>

// This is a class to hold all BLAST search related stuff.
// An instance of it is made available to the whole program
// as a global.

class BuildBlastDatabaseWorker;
class RunBlastSearchWorker;

class BlastSearch : public search::GraphSearch {
    Q_OBJECT
public:
    explicit BlastSearch(const QDir &workDir = QDir::temp(), QObject *parent = nullptr);
    virtual ~BlastSearch() = default;


    QString doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
                              QString extraParameters = "") override;
    int loadQueriesFromFile(QString fullFileName) override;
    QString buildDatabase(const AssemblyGraph &graph) override;
    QString doSearch(QString extraParameters) override;
    QString doSearch(search::Queries &queries, QString extraParameters) override;

    QString name() const override { return "BLAST"; }
    QString annotationGroupName() const override;

public slots:
    void cancelDatabaseBuild();
    void cancelSearch();

signals:
    void finishedDbBuild(QString error);
    void finishedSearch(QString error);

private:
    bool findTools();

    BuildBlastDatabaseWorker *m_buildDbWorker = nullptr;
    RunBlastSearchWorker *m_runSearchWorker = nullptr;
    QString m_makeblastdbCommand, m_blastnCommand, m_tblastnCommand;
};