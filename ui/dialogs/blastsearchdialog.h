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


#ifndef BLASTSEARCHDIALOG_H
#define BLASTSEARCHDIALOG_H

#include <QDialog>
#include <QThread>
#include <QProcess>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>

class BlastQuery;
class BlastQueries;
class BlastHit;

using BlastHits = std::vector<std::shared_ptr<BlastHit>>;

namespace Ui {
class BlastSearchDialog;
}

enum BlastUiState {BLAST_DB_NOT_YET_BUILT, BLAST_DB_BUILD_IN_PROGRESS,
    BLAST_DB_BUILT_BUT_NO_QUERIES,
    READY_FOR_BLAST_SEARCH, BLAST_SEARCH_IN_PROGRESS,
    BLAST_SEARCH_COMPLETE};

class PathButtonDelegate : public QStyledItemDelegate {
    Q_OBJECT
    Q_DISABLE_COPY(PathButtonDelegate)
public:
    explicit PathButtonDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent)
    {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

signals:
    void queryPathSelectionChanged();
};

class QueriesListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit QueriesListModel(BlastQueries &queries,
                              QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void setColor(const QModelIndex &index, QColor color);
    void update() { startUpdate(); endUpdate(); }
    void startUpdate() { beginResetModel(); }
    void endUpdate() { endResetModel(); }

    BlastQuery *query(const QModelIndex &index) const;

    BlastQueries &m_queries;
};

class HitsListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit HitsListModel(BlastQueries &queries,
                           QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void update(BlastQueries &queries);
    void clear() { m_hits.clear(); }
    void startUpdate() { beginResetModel(); }
    void endUpdate() { endResetModel(); }
    bool empty() const { return m_hits.empty(); }

    BlastHits m_hits;
};

class BlastSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit BlastSearchDialog(QWidget *parent = nullptr, const QString& autoQuery = "");
    ~BlastSearchDialog() override;

private:
    Ui::BlastSearchDialog *ui;

    QString m_makeblastdbCommand;
    QString m_blastnCommand;
    QString m_tblastnCommand;
    QThread *m_buildBlastDatabaseThread;
    QThread *m_blastSearchThread;
    QueriesListModel *m_queriesListModel;
    HitsListModel *m_hitsListModel;

    void setUiStep(BlastUiState blastUiState);
    void clearBlastHits();

    void loadBlastQueriesFromFastaFile(const QString& fullFileName);
    void buildBlastDatabase(bool separateThread);
    void runBlastSearches(bool separateThread);
    void setFilterText();

private slots:
    void afterWindowShow();
    void buildBlastDatabaseInThread();
    void loadBlastQueriesFromFastaFileButtonClicked();
    void enterQueryManually();
    void clearAllQueries();
    void clearSelectedQueries();
    void runBlastSearchesInThread();
    void fillTablesAfterBlastSearch();
    void updateTables();

    void blastDatabaseBuildFinished(const QString& error);
    void runBlastSearchFinished(const QString& error);

    void openFiltersDialog();

signals:
    void blastChanged();
    void queryPathSelectionChanged();
};


#endif // BLASTSEARCHDIALOG_H
