// Copyright 2017 Ryan Wick
// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your m_option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#include "querypathsdialog.h"
#include "ui_querypathsdialog.h"

#include "graphsearch/query.h"
#include "program/globals.h"
#include "program/memory.h"

#include <QSortFilterProxyModel>
#include <QClipboard>

using namespace search;

enum class QueryPathsColumns : int {
    PathString = 0,
    Length,
    QueryStart,
    QueryEnd,
    QueryCoveragePath,
    QueryCoverageHits,
    PercIdentity,
    Mismatches,
    GapOpens,
    RelativeLength,
    LengthDisc,
    Evalue,
    Copy,
    TotalColumns = Copy + 1
};

QueryPathsDialog::QueryPathsDialog(Query *query, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QueryPathsDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() | Qt::Tool);

    connect(this, SIGNAL(rejected()), this, SLOT(hidden()));

    g_memory->queryPathDialogIsVisible = true;
    g_memory->queryPaths.clear();

    QString queryDescription = "Query name: " + query->getName();
    queryDescription += "      type: " + query->getTypeString();
    queryDescription += "      length: " + formatIntForDisplay(query->getLength());
    if (query->getSequenceType() == PROTEIN)
        queryDescription += " (" + formatIntForDisplay(3 * query->getLength()) + " bp)";
    else
        queryDescription += " bp";
    ui->queryLabel->setText(queryDescription);

    auto *proxyModel = new QSortFilterProxyModel(ui->tableView);
    m_queryPathsModel = new QueryPathsModel(query, ui->tableView);
    proxyModel->setSourceModel(m_queryPathsModel);
    ui->tableView->setModel(proxyModel);
    ui->tableView->setItemDelegateForColumn(int(QueryPathsColumns::Copy),
                                            new CopyPathSequenceDelegate(ui->tableView));
    ui->tableView->setWordWrap(true);
    ui->tableView->setSortingEnabled(true);
    ui->tableView->horizontalHeader()->setStretchLastSection(true);
    ui->tableView->resizeColumnsToContents();

    connect(ui->tableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            &QueryPathsDialog::tableSelectionChanged);
}

QueryPathsDialog::~QueryPathsDialog() {
    delete ui;
    g_memory->queryPathDialogIsVisible = false;
}


void QueryPathsDialog::hidden() {
    g_memory->queryPathDialogIsVisible = false;
    emit selectionChanged();
}

void QueryPathsDialog::tableSelectionChanged(const QItemSelection &selected,
                                             const QItemSelection &deselected) {
    g_memory->queryPaths.clear();

    for (const auto &index : selected.indexes()) {
        const auto *proxyModel = qobject_cast<const QSortFilterProxyModel *>(index.model());
        const auto &queryPath = m_queryPathsModel->m_queryPaths[proxyModel->mapToSource(index).row()];
        g_memory->queryPaths.emplace_back(queryPath.getPath());
    }

    emit selectionChanged();
}

QueryPathsModel::QueryPathsModel(const Query *query, QObject *parent)
  : m_queryPaths(query->getPaths()), QAbstractTableModel(parent) {}

int QueryPathsModel::rowCount(const QModelIndex &) const {
    return m_queryPaths.size();
}

int QueryPathsModel::columnCount(const QModelIndex &) const {
    return int(QueryPathsColumns::TotalColumns);
}

QVariant QueryPathsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::TextAlignmentRole && orientation == Qt::Horizontal)
        return Qt::AlignCenter;

    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return QString::number(section + 1);

    switch (QueryPathsColumns(section)) {
        default:
            return {};
        case QueryPathsColumns::PathString:
            return "Path";
        case QueryPathsColumns::Length:
            return "Length";
        case QueryPathsColumns::QueryStart:
            return "Query\nstart";
        case QueryPathsColumns::QueryEnd:
            return "Query\nend";
        case QueryPathsColumns::QueryCoveragePath:
            return "Query\ncovered\nby path";
        case QueryPathsColumns::QueryCoverageHits:
            return "Query\ncovered\nby hits";
        case QueryPathsColumns::PercIdentity:
            return "IDY";
        case QueryPathsColumns::Mismatches:
            return "Mismatches";
        case QueryPathsColumns::GapOpens:
            return "Gaps";
        case QueryPathsColumns::RelativeLength:
            return "Relative\nlength";
        case QueryPathsColumns::LengthDisc:
            return "Length\ndiscrepancy";
        case QueryPathsColumns::Evalue:
            return "E-value";
        case QueryPathsColumns::Copy:
            return "Copy sequence\nto clipboard";
    }

    return {};
}

QVariant QueryPathsModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_queryPaths.size())
        return {};

    auto column = QueryPathsColumns(index.column());
    const auto &queryPath = m_queryPaths[index.row()];

    if (role != Qt::DisplayRole)
        return {};

    switch (column) {
        default:
            return {};
        case QueryPathsColumns::PathString:
            return queryPath.getPath().getString(true);
        case QueryPathsColumns::Length:
            return queryPath.getPath().getLength();
        case QueryPathsColumns::QueryStart: {
            int start = queryPath.queryStart();
            if (start < 0)
                return "N/A";
            return start;
        }
        case QueryPathsColumns::QueryEnd: {
            int end = queryPath.queryEnd();
            if (end < 0)
                return "N/A";
            return end;
        }
        case QueryPathsColumns::QueryCoveragePath:
            return formatDoubleForDisplay(100.0 * queryPath.getPathQueryCoverage(), 2) + "%";
        case QueryPathsColumns::QueryCoverageHits:
            return formatDoubleForDisplay(100.0 * queryPath.getHitsQueryCoverage(), 2) + "%";
        case QueryPathsColumns::PercIdentity: {
            double idy = queryPath.getMeanHitPercIdentity();
            if (idy < 0)
                return "N/A";
            return formatDoubleForDisplay(idy, 2) + "%";
        }
        case QueryPathsColumns::Mismatches: {
            int mismatches = queryPath.getTotalHitMismatches();
            if (mismatches < 0)
                return "N/A";
            return mismatches;
        }
        case QueryPathsColumns::GapOpens: {
            int gaps = queryPath.getTotalHitGapOpens();
            if (gaps < 0)
                return "N/A";

            return gaps;
        }
        case QueryPathsColumns::RelativeLength:
            return formatDoubleForDisplay(100.0 * queryPath.getRelativePathLength(), 2) + "%";
        case QueryPathsColumns::LengthDisc:
            return queryPath.getAbsolutePathLengthDifferenceString(true);
        case QueryPathsColumns::Evalue: {
            SciNot res = queryPath.getEvalueProduct();
            if (std::isnan(res.toDouble()))
                return "N/A";
            return res.asString(false);
        }
        case QueryPathsColumns::Copy: {
            QByteArray pathSequence = queryPath.getPath().getPathSequence();
            return pathSequence.length() < 8 ? pathSequence : pathSequence.left(8) + "...";
        }
    }

    return {};
}

Qt::ItemFlags QueryPathsModel::flags(const QModelIndex &index) const {
    if (!index.isValid())
        return Qt::ItemIsEnabled;

    auto column = QueryPathsColumns(index.column());
    if (column == QueryPathsColumns::Copy)
        return Qt::ItemIsEnabled;

    return QAbstractTableModel::flags(index);
}

void CopyPathSequenceDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const auto *proxyModel = qobject_cast<const QSortFilterProxyModel*>(index.model());
    const auto *model = qobject_cast<const QueryPathsModel*>(proxyModel->sourceModel());
    const auto &queryPath = model->m_queryPaths[proxyModel->mapToSource(index).row()];

    QStyleOptionButton btn;
    btn.features = QStyleOptionButton::None;
    btn.rect = option.rect;
    btn.state = option.state | QStyle::State_Enabled | QStyle::State_Raised;

    QByteArray pathSequence = queryPath.getPath().getPathSequence();
    btn.text = pathSequence.length() < 8 ? pathSequence : pathSequence.left(8) + "...";

    QStyle *style = option.widget ? option.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_PushButton, &btn, painter);
}

bool CopyPathSequenceDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                                           const QModelIndex &index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *proxyModel = qobject_cast<const QSortFilterProxyModel*>(model);
        const auto *dataModel = qobject_cast<const QueryPathsModel*>(proxyModel->sourceModel());
        const auto &queryPath = dataModel->m_queryPaths[proxyModel->mapToSource(index).row()];

        QApplication::clipboard()->setText(queryPath.getPath().getPathSequence());
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
