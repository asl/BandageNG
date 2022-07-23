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

#include "pathlistdialog.h"
#include "ui_pathlistdialog.h"

#include "graph/assemblygraph.h"
#include "graph/path.h"

PathListDialog::PathListDialog(const AssemblyGraph &graph, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PathListDialog) {
    ui->setupUi(this);

    ui->pathsView->setModel(new PathListModel(graph));
    ui->pathsView->sortByColumn(1, Qt::DescendingOrder);
    ui->pathsView->setSortingEnabled(true);
    ui->pathsView->resizeColumnsToContents();
    ui->pathsView->horizontalHeader()->setStretchLastSection(true);
}

PathListDialog::~PathListDialog() {
    delete ui;
}

enum Columns : unsigned {
    Name = 0,
    Length = 1,
    PathSeq = 2,
    TotalColumns = PathSeq + 1
};

PathListModel::PathListModel(const AssemblyGraph &g, QObject *parent)
  : QAbstractTableModel(parent), graph(g) {
    m_orderedPaths.reserve(graph.m_deBruijnGraphPaths.size());
    for (auto it = graph.m_deBruijnGraphPaths.begin(); it != graph.m_deBruijnGraphPaths.end(); ++it)
        m_orderedPaths.emplace_back(it.key(), *it);
}

int PathListModel::rowCount(const QModelIndex &) const {
    return m_orderedPaths.size();
}

int PathListModel::columnCount(const QModelIndex &) const {
    return Columns::TotalColumns;
}

void PathListModel::sort(int column, Qt::SortOrder order) {
    switch (column) {
        default:
            return;
        case Columns::Name:
            std::sort(m_orderedPaths.begin(), m_orderedPaths.end(),
                 [order](const auto &lhs, const auto &rhs) {
                return order == Qt::SortOrder::AscendingOrder ?
                       lhs.first < rhs.first : rhs.first < lhs.first;
            });
            break;
        case Columns::Length:
            std::sort(m_orderedPaths.begin(), m_orderedPaths.end(),
                      [order](const auto &lhs, const auto &rhs) {
                          unsigned l = lhs.second->getLength(), r = rhs.second->getLength();
                          return order == Qt::SortOrder::AscendingOrder ?
                                 l < r : r < l;
                      });
            break;
    }

    emit dataChanged(QModelIndex(), QModelIndex());
}

QVariant PathListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return QString::number(section + 1);

    switch (section) {
        default:
            return {};
        case Columns::Name:
            return "Name";
        case Columns::Length:
            return "Length";
        case Columns::PathSeq:
            return "Path";
    }
}

QVariant PathListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return {};
    if (index.row() >= m_orderedPaths.size())
        return {};

    if (role == Qt::DisplayRole) {
        const auto &entry = m_orderedPaths.at(index.row());

        switch (index.column()) {
            default:
                return {};
            case Columns::Name:
                return entry.first.c_str();
            case Columns::Length:
                return entry.second->getLength();
            case Columns::PathSeq:
                return entry.second->getString(true);
        }
    }

    return {};
}