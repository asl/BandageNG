// Copyright 2017 Ryan Wick
// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage

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

#include <QDialog>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>

namespace search {
    class Query;
    class Queries;
    class Hit;
    class GraphSearch;
}

using Hits = std::vector<search::Hit>;

namespace Ui {
class GraphSearchDialog;
}

enum SearchUiState {
    GRAPH_DB_NOT_YET_BUILT, GRAPH_DB_BUILD_IN_PROGRESS,
    GRAPH_DB_BUILT_BUT_NO_QUERIES,
    READY_FOR_GRAPH_SEARCH, GRAPH_SEARCH_IN_PROGRESS,
    GRAPH_SEARCH_COMPLETE
};

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
    explicit QueriesListModel(search::Queries &queries,
                              QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void setColor(const QModelIndex &index, QColor color);
    void update() { startUpdate(); endUpdate(); }
    void setQueries(search::Queries &queries) { m_queries = queries; }
    void startUpdate() { beginResetModel(); }
    void endUpdate() { endResetModel(); }

    search::Query *query(const QModelIndex &index) const;

    std::reference_wrapper<search::Queries> m_queries;
};

class HitsListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit HitsListModel(search::Queries &queries,
                           QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void update(search::Queries &queries);
    void clear() { m_hits.clear(); }
    void startUpdate() { beginResetModel(); }
    void endUpdate() { endResetModel(); }
    bool empty() const { return m_hits.empty(); }

    Hits m_hits;
};

class GraphSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit GraphSearchDialog(QWidget *parent = nullptr, const QString& autoQuery = "");
    ~GraphSearchDialog() override;
    const search::GraphSearch *search() const { return m_graphSearch.get(); }
    search::GraphSearch *search() { return m_graphSearch.get(); }

private:
    Ui::GraphSearchDialog *ui;

    std::unique_ptr<search::GraphSearch> m_graphSearch;
    QueriesListModel *m_queriesListModel;
    HitsListModel *m_hitsListModel;

    void setUiStep(SearchUiState blastUiState);
    void clearHits();

    void loadQueriesFromFile(const QString& fullFileName);
    void buildDatabase(bool separateThread);
    void runGraphSearches(bool separateThread);
    void setFilterText();
    void setUiCaptions();

private slots:
    void afterWindowShow();
    void buildGraphDatabaseInThread();
    void loadQueriesFromFileButtonClicked();
    void enterQueryManually();
    void clearAllQueries();
    void clearSelectedQueries();
    void runGraphSearchesInThread();
    void fillTablesAfterGraphSearch();
    void updateTables();
    void searcherChanged();

    void graphDatabaseBuildFinished(const QString& error);
    void graphSearchFinished(const QString& error);

    void openFiltersDialog();

signals:
    void changed();
    void queryPathSelectionChanged();
};