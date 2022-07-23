// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once
#include <QDialog>
#include <QAbstractTableModel>

namespace Ui {
class PathListDialog;
}

class AssemblyGraph;
class Path;

class PathListModel : public QAbstractTableModel {
Q_OBJECT

public:
    PathListModel(const AssemblyGraph &g, QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    std::vector<std::pair<std::string, Path*>> m_orderedPaths;
    const AssemblyGraph &graph;
};

class PathListDialog : public QDialog
{
    Q_OBJECT

public:
    PathListDialog(const AssemblyGraph &graph, QWidget *parent = nullptr);
    ~PathListDialog();

private:
    Ui::PathListDialog *ui;
};