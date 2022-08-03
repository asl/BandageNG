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

#include <QMessageBox>
#include <QPushButton>
#include <QStringBuilder>

enum Columns : unsigned {
    Name = 0,
    Length = 1,
    NodePosition = 2,
    PathNodes = 3,
    TotalColumns = PathNodes + 1
};

PathListDialog::PathListDialog(const AssemblyGraph &graph,
                               const std::vector<DeBruijnNode *> &startNodes,
                               QWidget *parent)
    : m_graph(graph),
      QDialog(parent),
      ui(new Ui::PathListDialog) {
    ui->setupUi(this);
    setWindowTitle("Paths");

    QString nodes;
    for (size_t i = 0; i < startNodes.size(); ++i)
        nodes = (i == 0 ?
                 startNodes[i]->getName() :
                 nodes % ", " % startNodes[i]->getName());
    ui->nodeEdit->setText(nodes);

    // Ensure our "Close" is not default
    ui->buttonBox->button(QDialogButtonBox::Close)->setAutoDefault(false);

    ui->pathsView->setModel(new PathListModel(graph, startNodes));
    ui->pathsView->sortByColumn(1, Qt::DescendingOrder);
    ui->pathsView->setSortingEnabled(true);
    ui->pathsView->setColumnHidden(Columns::NodePosition, startNodes.size() != 1);
    ui->pathsView->resizeColumnsToContents();
    ui->pathsView->horizontalHeader()->setStretchLastSection(true);

    connect(ui->nodeEdit, &QLineEdit::editingFinished, this, &PathListDialog::refineByNode);
}

PathListDialog::~PathListDialog() {
    delete ui;
}

void PathListDialog::refineByNode() {
    QString nodeName = ui->nodeEdit->text();
    std::vector<QString> nodesNotInGraph;

    auto nodes = m_graph.getNodesFromString(nodeName, true, &nodesNotInGraph);
    if (!nodesNotInGraph.empty()) {
        QMessageBox::information(this, "Nodes not found",
                                 // FIXME: This is crazy!
                                 AssemblyGraph::generateNodesNotFoundErrorMessage(nodesNotInGraph, true));
        return;
    }

    auto *model = dynamic_cast<PathListModel*>(ui->pathsView->model());
    model->refinePathOrder(nodes);

    // Resort
    ui->pathsView->setColumnHidden(Columns::NodePosition, nodes.size() != 1);
    ui->pathsView->sortByColumn(1, Qt::DescendingOrder);
}

PathListModel::PathListModel(const AssemblyGraph &g,
                             const std::vector<DeBruijnNode*> &startNodes,
                             QObject *parent)
  : QAbstractTableModel(parent), graph(g) {
    // Build a coverage map: which node is covered by which paths (used for filtering)
    for (const auto *p : graph.m_deBruijnGraphPaths)
        for (const auto *node : p->nodes())
            m_coverageMap[node].insert(p);

    refinePathOrder(startNodes);
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
        case Columns::NodePosition:
            return "Node positions";
        case Columns::PathNodes:
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
            case Columns::NodePosition: {
                if (m_node == nullptr)
                    return {};
                auto pos = entry.second->getPosition(m_node);
                QString positions;
                for (size_t i = 0; i < pos.size(); ++i)
                    positions =
                            i == 0 ?
                            QString::number(pos[i]) :
                            positions % ", " % QString::number(pos[i]);
                return positions;
            }
            case Columns::PathNodes:
                return entry.second->getString(true);
        }
    }

    return {};
}

void PathListModel::refinePathOrder(const std::vector<DeBruijnNode*> &nodes) {
    beginResetModel();

    m_orderedPaths.clear();
    m_node = nullptr;

    // No node: whole graph and all paths
    if (nodes.empty()) {
        m_orderedPaths.reserve(graph.m_deBruijnGraphPaths.size());
        for (auto it = graph.m_deBruijnGraphPaths.begin(); it != graph.m_deBruijnGraphPaths.end(); ++it)
            m_orderedPaths.emplace_back(it.key(), *it);
    } else {
        phmap::flat_hash_set<const Path *> paths;
        if (nodes.size() == 1)
            m_node = nodes.front();

        for (const auto *node: nodes) {
            auto entry = m_coverageMap.find(node);
            if (entry == m_coverageMap.end())
                continue;

            paths.insert(entry->second.begin(), entry->second.end());
        }

        // We have to iterate over all paths as names are stored as name in the map
        for (auto it = graph.m_deBruijnGraphPaths.begin(); it != graph.m_deBruijnGraphPaths.end(); ++it)
            if (paths.contains(*it))
                m_orderedPaths.emplace_back(it.key(), *it);
    }

    endResetModel();
}
