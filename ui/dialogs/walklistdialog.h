// Copyright 2023 Anton Korobeynikov

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

#include "parallel_hashmap/phmap.h"

#include <QDialog>
#include <QAbstractTableModel>

#include <vector>

namespace Ui {
class WalkListDialog;
}

class AssemblyGraph;
class Walk;
class DeBruijnNode;

class WalkListModel : public QAbstractTableModel {
Q_OBJECT

public:
    WalkListModel(const AssemblyGraph &g,
                  const std::vector<DeBruijnNode *> &startNodes = {},
                  QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void refineWalkOrder(const std::vector<DeBruijnNode *> &nodes = {});
private:

    std::vector<std::pair<std::string, const Walk*>> m_orderedWalks;
    phmap::parallel_flat_hash_map<const DeBruijnNode*, phmap::flat_hash_set<const Walk*>> m_coverageMap;
    const DeBruijnNode *m_node = nullptr;
    const AssemblyGraph &graph;
};

class WalkListDialog : public QDialog
{
    Q_OBJECT

public:
    WalkListDialog(const AssemblyGraph &graph,
                   const std::vector<DeBruijnNode *> &startNodes = {},
                   QWidget *parent = nullptr);
    ~WalkListDialog();

private:
    void refineByNode();

    const AssemblyGraph &m_graph;
    Ui::WalkListDialog *ui;
};
