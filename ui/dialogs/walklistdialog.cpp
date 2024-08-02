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

#include "walklistdialog.h"
#include "ui_walklistdialog.h"

#include "graph/assemblygraph.h"
#include "graph/path.h"

#include <QMessageBox>
#include <QPushButton>
#include <QStringBuilder>

enum Columns : unsigned {
    Sample = 0,
    HapIndex = 1,
    Sequence = 2,
    Length = 3,
    SeqStart = 4,
    SeqEnd = 5,
    NodePosition = 6,
    WalkNodes = 7,
    TotalColumns = WalkNodes + 1
};

WalkListDialog::WalkListDialog(const AssemblyGraph &graph,
                               const std::vector<DeBruijnNode *> &startNodes,
                               QWidget *parent)
    : m_graph(graph),
      QDialog(parent),
      ui(new Ui::WalkListDialog) {
    ui->setupUi(this);
    setWindowTitle("Walks");

    QString nodes;
    for (size_t i = 0; i < startNodes.size(); ++i)
        nodes = (i == 0 ?
                 startNodes[i]->getName() :
                 nodes % ", " % startNodes[i]->getName());
    ui->nodeEdit->setText(nodes);

    // Ensure our "Close" is not default
    ui->buttonBox->button(QDialogButtonBox::Close)->setAutoDefault(false);

    ui->walksView->setModel(new WalkListModel(graph, startNodes, ui->walksView));
    ui->walksView->sortByColumn(Columns::Sequence, Qt::AscendingOrder);
    ui->walksView->setSortingEnabled(true);
    ui->walksView->setColumnHidden(Columns::NodePosition, startNodes.size() != 1);
    ui->walksView->resizeColumnsToContents();
    ui->walksView->horizontalHeader()->setStretchLastSection(true);

    connect(ui->nodeEdit, &QLineEdit::editingFinished, this, &WalkListDialog::refineByNode);
}

WalkListDialog::~WalkListDialog() {
    delete ui;
}

void WalkListDialog::refineByNode() {
    QString nodeName = ui->nodeEdit->text();
    std::vector<QString> nodesNotInGraph;

    auto nodes = m_graph.getNodesFromStringList(nodeName, true, &nodesNotInGraph);
    if (!nodesNotInGraph.empty()) {
        QMessageBox::information(this, "Nodes not found",
                                 // FIXME: This is crazy!
                                 AssemblyGraph::generateNodesNotFoundErrorMessage(nodesNotInGraph, true));
        return;
    }

    auto *model = qobject_cast<WalkListModel*>(ui->walksView->model());
    model->refineWalkOrder(nodes);

    // Resort
    ui->walksView->setColumnHidden(Columns::NodePosition, nodes.size() != 1);
    ui->walksView->sortByColumn(1, Qt::DescendingOrder);
}

WalkListModel::WalkListModel(const AssemblyGraph &g,
                             const std::vector<DeBruijnNode*> &startNodes,
                             QObject *parent)
  : QAbstractTableModel(parent), graph(g) {
    // Build a coverage map: which node is covered by which walks (used for filtering)
    for (const auto &w : graph.m_deBruijnGraphWalks)
        for (const auto *node : w.walk.nodes())
            m_coverageMap[node].insert(&w);

    refineWalkOrder(startNodes);
}

int WalkListModel::rowCount(const QModelIndex &) const {
    return m_orderedWalks.size();
}

int WalkListModel::columnCount(const QModelIndex &) const {
    return Columns::TotalColumns;
}

void WalkListModel::sort(int column, Qt::SortOrder order) {
    switch (column) {
        default:
            return;
        case Columns::Sequence:
            std::sort(m_orderedWalks.begin(), m_orderedWalks.end(),
                 [order](const auto &lhs, const auto &rhs) {
                return order == Qt::SortOrder::AscendingOrder ?
                       lhs.first < rhs.first : rhs.first < lhs.first;
            });
            break;
        case Columns::Sample:
            std::sort(m_orderedWalks.begin(), m_orderedWalks.end(),
                 [order](const auto &lhs, const auto &rhs) {
                const auto &lSample = lhs.second->sampleId;
                const auto &rSample = rhs.second->sampleId;

                return order == Qt::SortOrder::AscendingOrder ?
                       lSample < rSample : rSample < lSample;
            });
            break;
        case Columns::Length:
            std::sort(m_orderedWalks.begin(), m_orderedWalks.end(),
                      [order](const auto &lhs, const auto &rhs) {
                          unsigned l = lhs.second->walk.getLength(), r = rhs.second->walk.getLength();
                          return order == Qt::SortOrder::AscendingOrder ?
                                 l < r : r < l;
                      });
            break;
        case Columns::SeqStart:
            std::sort(m_orderedWalks.begin(), m_orderedWalks.end(),
                      [order](const auto &lhs, const auto &rhs) {
                          unsigned l = lhs.second->seqStart, r = rhs.second->seqStart;
                          return order == Qt::SortOrder::AscendingOrder ?
                                 l < r : r < l;
                      });
            break;
        case Columns::SeqEnd:
            std::sort(m_orderedWalks.begin(), m_orderedWalks.end(),
                      [order](const auto &lhs, const auto &rhs) {
                          unsigned l = lhs.second->seqEnd, r = rhs.second->seqEnd;
                          return order == Qt::SortOrder::AscendingOrder ?
                                 l < r : r < l;
                      });
            break;
    }

    emit dataChanged(QModelIndex(), QModelIndex());
}

QVariant WalkListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return QString::number(section + 1);

    switch (section) {
        default:
            return {};
        case Columns::Sample:
            return "Sample";
        case Columns::HapIndex:
            return "Haplotype";
        case Columns::Sequence:
            return "Sequence";
        case Columns::Length:
            return "Length";
        case Columns::SeqStart:
            return "Sequence start";
        case Columns::SeqEnd:
            return "Sequence end";
        case Columns::NodePosition:
            return "Node positions";
        case Columns::WalkNodes:
            return "Walk";
    }
}

QVariant WalkListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return {};
    if (index.row() >= m_orderedWalks.size())
        return {};

    if (role == Qt::DisplayRole) {
        const auto &entry = m_orderedWalks.at(index.row());

        switch (index.column()) {
            default:
                return {};
            case Columns::Sequence:
                return entry.first.c_str();
            case Columns::Sample:
                return entry.second->sampleId.c_str();
            case Columns::HapIndex:
                return entry.second->hapIndex;
            case Columns::Length:
                return entry.second->walk.getLength();
            case Columns::SeqStart:
                return entry.second->seqStart;
            case Columns::SeqEnd:
                return entry.second->seqEnd;
            case Columns::NodePosition: {
                if (m_node == nullptr)
                    return {};
                auto pos = entry.second->walk.getPosition(m_node);
                QString positions;
                for (size_t i = 0; i < pos.size(); ++i)
                    positions =
                            i == 0 ?
                            QString::number(pos[i]) :
                            positions % ", " % QString::number(pos[i]);
                return positions;
            }
            case Columns::WalkNodes:
                return entry.second->walk.getString(true);
        }
    }

    return {};
}

void WalkListModel::refineWalkOrder(const std::vector<DeBruijnNode*> &nodes) {
    beginResetModel();

    m_orderedWalks.clear();
    m_node = nullptr;

    // No node: whole graph and all walks
    if (nodes.empty()) {
        m_orderedWalks.reserve(graph.m_deBruijnGraphWalks.size());
        for (auto it = graph.m_deBruijnGraphWalks.begin(); it != graph.m_deBruijnGraphWalks.end(); ++it)
            m_orderedWalks.emplace_back(it.key(), &*it);
    } else {
        phmap::flat_hash_set<const Walk *> walks;
        if (nodes.size() == 1)
            m_node = nodes.front();

        for (const auto *node: nodes) {
            auto entry = m_coverageMap.find(node);
            if (entry == m_coverageMap.end())
                continue;

            walks.insert(entry->second.begin(), entry->second.end());
        }

        // We have to iterate over all walks as names are stored as name in the map
        for (auto it = graph.m_deBruijnGraphWalks.begin(); it != graph.m_deBruijnGraphWalks.end(); ++it)
            if (walks.contains(&*it))
                m_orderedWalks.emplace_back(it.key(), &*it);
    }

    endResetModel();
}
