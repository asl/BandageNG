//Copyright 2017 Ryan Wick

//This file is part of Bandage.

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


#ifndef QUERYPATHSDIALOG_H
#define QUERYPATHSDIALOG_H

#include "blast/blastquerypath.h"

#include <QDialog>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QItemSelection>

class Query;

namespace Ui {
class QueryPathsDialog;
}

class CopyPathSequenceDelegate : public QStyledItemDelegate {
Q_OBJECT
    Q_DISABLE_COPY(CopyPathSequenceDelegate)
public:
    explicit CopyPathSequenceDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
    {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;
};

class QueryPathsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit QueryPathsModel(const Query *query,
                             QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    std::vector<QueryPath> m_queryPaths;
};

class QueryPathsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QueryPathsDialog(Query *query, QWidget *parent = nullptr);
    ~QueryPathsDialog() override;

private:
    Ui::QueryPathsDialog *ui;
    QueryPathsModel *m_queryPathsModel;
private slots:
    void hidden();
    void tableSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

signals:
    void selectionChanged();
};

#endif // QUERYPATHSDIALOG_H
