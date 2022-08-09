// Copyright 2022 Ryan Wick

// This file is part of Bandage-NG

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

#pragma once

#include "graphsearch/graphsearch.h"

#include <QDir>
#include <QString>

class QProcess;

class Minimap2Search : public search::GraphSearch {
    Q_OBJECT
public:
    explicit Minimap2Search(const QDir &workDir = QDir::temp());
    virtual ~Minimap2Search() = default;


    QString doAutoGraphSearch(const AssemblyGraph &graph, QString queriesFilename,
                              QString extraParameters = "") override;
    int loadQueriesFromFile(QString fullFileName) override;
    QString buildDatabase(const AssemblyGraph &graph) override;
    QString doSearch(QString extraParameters) override;
    QString doSearch(search::Queries &queries, QString extraParameters) override;

    QString name() const override { return "Minimap2"; }
    QString annotationGroupName() const override { return "Minimap2 hits"; };

public slots:
    void cancelDatabaseBuild();
    void cancelSearch();

signals:
    void finishedDbBuild(QString error);
    void finishedSearch(QString error);

private:
    bool findTools();

    bool m_cancelBuildDatabase = false, m_cancelSearch = false;

    QProcess *m_buildDb = nullptr, *m_doSearch = nullptr;
    QString m_minimap2Command;
};