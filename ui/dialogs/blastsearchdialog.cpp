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


#include "blastsearchdialog.h"
#include "ui_blastsearchdialog.h"

#include "enteroneblastquerydialog.h"

#include "blast/blasthit.h"
#include "blast/blastquery.h"
#include "blast/blastsearch.h"
#include "blast/buildblastdatabaseworker.h"
#include "blast/runblastsearchworker.h"

#include "graph/debruijnnode.h"
#include "graph/assemblygraph.h"
#include "graph/annotationsmanager.h"

#include "myprogressdialog.h"
#include "querypathsdialog.h"
#include "blasthitfiltersdialog.h"

#include "program/globals.h"
#include "program/settings.h"
#include "program/memory.h"

#include <QFileDialog>
#include <QFile>
#include <QString>
#include <QMessageBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QPainter>
#include <QSortFilterProxyModel>

enum class QueriesHitColumns : unsigned {
    Color = 0,
    Show = 1,
    QueryName = 2,
    Type = 3,
    Length = 4,
    Hits = 5,
    QueryCover = 6,
    Paths = 7,
    TotalHitColumns = Paths + 1
};

enum class HitsColumns : unsigned {
    Color = 0,
    QueryName = 1,
    NodeName = 2,
    PercentIdentity = 3,
    AlignmentLength = 4,
    QueryCover = 5,
    Mismatches = 6,
    GapOpens = 7,
    QueryStart = 8,
    QueryEnd = 9,
    NodeStart = 10,
    NodeEnd = 11,
    Evalue = 12,
    BitScore = 13,
    TotalHitColumns = BitScore + 1
};

BlastSearchDialog::BlastSearchDialog(QWidget *parent, const QString& autoQuery) :
    QDialog(parent),
    ui(new Ui::BlastSearchDialog),
    m_makeblastdbCommand("makeblastdb"), m_blastnCommand("blastn"),
    m_tblastnCommand("tblastn") {
    ui->setupUi(this);

    setWindowFlags(windowFlags() | Qt::Tool);

    m_queriesListModel = new QueriesListModel(g_blastSearch->m_blastQueries,
                                              ui->blastQueriesTable);
    auto *proxyQModel = new QSortFilterProxyModel(ui->blastQueriesTable);
    proxyQModel->setSourceModel(m_queriesListModel);
    ui->blastQueriesTable->setModel(proxyQModel);
    ui->blastQueriesTable->setSortingEnabled(true);

    auto *queryPathsDelegate = new PathButtonDelegate(ui->blastQueriesTable);
    ui->blastQueriesTable->setItemDelegateForColumn(int(QueriesHitColumns::Paths),
                                                    queryPathsDelegate);
    connect(queryPathsDelegate, &PathButtonDelegate::queryPathSelectionChanged,
            [this] { emit queryPathSelectionChanged(); });

    m_hitsListModel = new HitsListModel(g_blastSearch->m_allHits, ui->blastHitsTable);
    auto *proxyHModel = new QSortFilterProxyModel(ui->blastHitsTable);
    proxyHModel->setSourceModel(m_hitsListModel);
    ui->blastHitsTable->setModel(proxyHModel);
    ui->blastHitsTable->setSortingEnabled(true);

    setFilterText();

    // Load any previous parameters the user might have entered when previously using this dialog.
    ui->parametersLineEdit->setText(g_settings->blastSearchParameters);

    // If the dialog is given an autoQuery parameter, then it will
    // carry out the entire process on its own.
    if (!autoQuery.isEmpty()) {
        buildBlastDatabase(false);
        clearAllQueries();
        loadBlastQueriesFromFastaFile(autoQuery);
        runBlastSearches(false);
        QMetaObject::invokeMethod(this, "close", Qt::QueuedConnection);
        return;
    }

    //If a BLAST database already exists, move to step 2.
    QFile databaseFile = g_blastSearch->m_tempDirectory.filePath("all_nodes.fasta");
    if (databaseFile.exists())
        setUiStep(BLAST_DB_BUILT_BUT_NO_QUERIES);
    //If there isn't a BLAST database, clear the entire temporary directory
    //and move to step 1.
    else {
        g_blastSearch->emptyTempDirectory();
        setUiStep(BLAST_DB_NOT_YET_BUILT);
    }

    // If queries already exist, display them and move to step 3.
    if (!g_blastSearch->m_blastQueries.m_queries.empty()) {
        updateTables();
        setUiStep(READY_FOR_BLAST_SEARCH);
    }

    // If results already exist, display them and move to step 4.
    if (!g_blastSearch->m_allHits.empty()) {
        updateTables();
        setUiStep(BLAST_SEARCH_COMPLETE);
    }

    connect(ui->buildBlastDatabaseButton, SIGNAL(clicked()), this, SLOT(buildBlastDatabaseInThread()));
    connect(ui->loadQueriesFromFastaButton, SIGNAL(clicked()), this, SLOT(loadBlastQueriesFromFastaFileButtonClicked()));
    connect(ui->enterQueryManuallyButton, SIGNAL(clicked()), this, SLOT(enterQueryManually()));
    connect(ui->clearAllQueriesButton, SIGNAL(clicked()), this, SLOT(clearAllQueries()));
    connect(ui->clearSelectedQueriesButton, SIGNAL(clicked(bool)), this, SLOT(clearSelectedQueries()));
    connect(ui->runBlastSearchButton, SIGNAL(clicked()), this, SLOT(runBlastSearchesInThread()));

    connect(ui->blastQueriesTable->selectionModel(),
        &QItemSelectionModel::selectionChanged,
        [this]() {
            auto *select = ui->blastQueriesTable->selectionModel();
            ui->clearSelectedQueriesButton->setEnabled(select->hasSelection());
        });

    connect(ui->blastQueriesTable,
            &QTableView::clicked,
            m_queriesListModel,
            [this](const QModelIndex &index) {
                if (!index.isValid())
                    return;

                auto column = QueriesHitColumns(index.column());
                if (column != QueriesHitColumns::Color)
                    return;

                if (auto *query = m_queriesListModel->query(index)) {
                    QColor chosenColour = QColorDialog::getColor(query->getColour(),
                                                                 this,
                                                                 "Query color", QColorDialog::ShowAlphaChannel);
                    if (!chosenColour.isValid())
                        return;

                    m_queriesListModel->setColor(index, chosenColour);

                    this->activateWindow(); // FIXME: why do we really need this? :(
                }
            });

    connect(m_queriesListModel, &QueriesListModel::dataChanged,
            [this]() {
                emit blastChanged();
            });

    // This is weird: we need to propagate data changes to proxies
    connect(m_queriesListModel, &QueriesListModel::dataChanged,
            [proxyHModel, proxyQModel](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                emit proxyQModel->dataChanged(proxyQModel->mapFromSource(topLeft), proxyQModel->mapFromSource(bottomRight));
                emit proxyHModel->dataChanged(QModelIndex(), QModelIndex());
            });

    connect(ui->blastFiltersButton, SIGNAL(clicked(bool)), this, SLOT(openFiltersDialog()));
}

BlastSearchDialog::~BlastSearchDialog() {
    delete ui;
}

void BlastSearchDialog::afterWindowShow() {
    updateTables();
}

void BlastSearchDialog::clearBlastHits() {
    g_blastSearch->clearBlastHits();
    g_annotationsManager->removeGroupByName(g_settings->blastAnnotationGroupName);
    updateTables();
}

void BlastSearchDialog::fillTablesAfterBlastSearch() {
    if (g_blastSearch->m_allHits.empty())
        QMessageBox::information(this, "No hits", "No BLAST hits were found for the given queries and parameters.");

    updateTables();
}

void BlastSearchDialog::updateTables() {
    m_queriesListModel->update();
    m_hitsListModel->update();
    ui->blastQueriesTable->resizeColumnsToContents();
    ui->blastHitsTable->resizeColumnsToContents();
}

void BlastSearchDialog::buildBlastDatabaseInThread() {
    buildBlastDatabase(true);
}

void BlastSearchDialog::buildBlastDatabase(bool separateThread) {
    setUiStep(BLAST_DB_BUILD_IN_PROGRESS);

    if (!g_blastSearch->findProgram("makeblastdb", &m_makeblastdbCommand)) {
        QMessageBox::warning(this, "Error", "The program makeblastdb was not found.  Please install NCBI BLAST to use this feature.");
        setUiStep(BLAST_DB_NOT_YET_BUILT);
        return;
    }

    QApplication::processEvents();

    auto * progress = new MyProgressDialog(this, "Building BLAST database...", separateThread, "Cancel build", "Cancelling build...",
                                           "Clicking this button will stop the BLAST database from being "
                                           "built.");
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    if (separateThread) {
        m_buildBlastDatabaseThread = new QThread;
        auto * buildBlastDatabaseWorker = new BuildBlastDatabaseWorker(m_makeblastdbCommand);
        buildBlastDatabaseWorker->moveToThread(m_buildBlastDatabaseThread);

        connect(progress, SIGNAL(halt()), this, SLOT(buildBlastDatabaseCancelled()));
        connect(m_buildBlastDatabaseThread, SIGNAL(started()), buildBlastDatabaseWorker, SLOT(buildBlastDatabase()));
        connect(buildBlastDatabaseWorker, SIGNAL(finishedBuild(QString)), m_buildBlastDatabaseThread, SLOT(quit()));
        connect(buildBlastDatabaseWorker, SIGNAL(finishedBuild(QString)), buildBlastDatabaseWorker, SLOT(deleteLater()));
        connect(buildBlastDatabaseWorker, SIGNAL(finishedBuild(QString)), this, SLOT(blastDatabaseBuildFinished(QString)));
        connect(m_buildBlastDatabaseThread, SIGNAL(finished()), m_buildBlastDatabaseThread, SLOT(deleteLater()));
        connect(m_buildBlastDatabaseThread, SIGNAL(finished()), progress, SLOT(deleteLater()));

        m_buildBlastDatabaseThread->start();
    } else {
        BuildBlastDatabaseWorker buildBlastDatabaseWorker(m_makeblastdbCommand);
        buildBlastDatabaseWorker.buildBlastDatabase();
        progress->close();
        delete progress;
        blastDatabaseBuildFinished(buildBlastDatabaseWorker.m_error);
    }
}


void BlastSearchDialog::blastDatabaseBuildFinished(const QString& error) {
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Error", error);
        setUiStep(BLAST_DB_NOT_YET_BUILT);
    } else
        setUiStep(BLAST_DB_BUILT_BUT_NO_QUERIES);
}


void BlastSearchDialog::buildBlastDatabaseCancelled() {
    g_blastSearch->m_cancelBuildBlastDatabase = true;
    if (g_blastSearch->m_makeblastdb != nullptr)
        g_blastSearch->m_makeblastdb->kill();
}

void BlastSearchDialog::loadBlastQueriesFromFastaFileButtonClicked() {
    QStringList fullFileNames = QFileDialog::getOpenFileNames(this, "Load queries FASTA", g_memory->rememberedPath);

    if (fullFileNames.empty()) //User did hit cancel
        return;

    for (const auto &fullFileName : fullFileNames)
        loadBlastQueriesFromFastaFile(fullFileName);
}

void BlastSearchDialog::loadBlastQueriesFromFastaFile(const QString& fullFileName) {
    auto * progress = new MyProgressDialog(this, "Loading queries...", false);
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    int queriesLoaded = g_blastSearch->loadBlastQueriesFromFastaFile(fullFileName);
    if (queriesLoaded > 0) {
        clearBlastHits();

        g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
        setUiStep(READY_FOR_BLAST_SEARCH);
    }
    updateTables();

    progress->close();
    progress->deleteLater();

    if (queriesLoaded == 0)
        QMessageBox::information(this, "No queries loaded", "No queries could be loaded from the specified file.");
}


void BlastSearchDialog::enterQueryManually() {
    EnterOneBlastQueryDialog enterOneBlastQueryDialog(this);
    if (!enterOneBlastQueryDialog.exec())
        return;

    QString queryName = g_blastSearch->cleanQueryName(enterOneBlastQueryDialog.getName());
    g_blastSearch->m_blastQueries.addQuery(new BlastQuery(queryName,
                                                          enterOneBlastQueryDialog.getSequence()));
    updateTables();
    clearBlastHits();

    setUiStep(READY_FOR_BLAST_SEARCH);
}

void BlastSearchDialog::clearAllQueries() {
    ui->clearAllQueriesButton->setEnabled(false);

    g_blastSearch->m_blastQueries.clearAllQueries();

    clearBlastHits();
    updateTables();

    setUiStep(BLAST_DB_BUILT_BUT_NO_QUERIES);
    emit blastChanged();
}

void BlastSearchDialog::clearSelectedQueries() {
    // Use the table selection to figure out which queries are to be removed.
    std::vector<BlastQuery *> queriesToRemove;
    QItemSelectionModel * select = ui->blastQueriesTable->selectionModel();
    QModelIndexList selection = select->selectedIndexes();

    if (selection.size() == g_blastSearch->m_blastQueries.m_queries.size()) {
        clearAllQueries();
        return;
    }

    for (const auto &index : selection)
        queriesToRemove.push_back(g_blastSearch->m_blastQueries.m_queries[index.row()]);
    g_blastSearch->clearSomeQueries(queriesToRemove);

    updateTables();

    emit blastChanged();
}

void BlastSearchDialog::runBlastSearchesInThread() {
    runBlastSearches(true);
}


void BlastSearchDialog::runBlastSearches(bool separateThread) {
    setUiStep(BLAST_SEARCH_IN_PROGRESS);

    if (!g_blastSearch->findProgram("blastn", &m_blastnCommand)) {
        QMessageBox::warning(this, "Error", "The program blastn was not found.  Please install NCBI BLAST to use this feature.");
        setUiStep(READY_FOR_BLAST_SEARCH);
        return;
    }
    if (!g_blastSearch->findProgram("tblastn", &m_tblastnCommand)) {
        QMessageBox::warning(this, "Error", "The program tblastn was not found.  Please install NCBI BLAST to use this feature.");
        setUiStep(READY_FOR_BLAST_SEARCH);
        return;
    }

    clearBlastHits();

    auto * progress = new MyProgressDialog(this, "Running BLAST search...", separateThread, "Cancel search", "Cancelling search...",
                                           "Clicking this button will stop the BLAST search.");
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    if (separateThread) {
        m_blastSearchThread = new QThread;
        auto * runBlastSearchWorker = new RunBlastSearchWorker(m_blastnCommand, m_tblastnCommand, ui->parametersLineEdit->text().simplified());
        runBlastSearchWorker->moveToThread(m_blastSearchThread);

        connect(progress, SIGNAL(halt()), this, SLOT(runBlastSearchCancelled()));
        connect(m_blastSearchThread, SIGNAL(started()), runBlastSearchWorker, SLOT(runBlastSearch()));
        connect(runBlastSearchWorker, SIGNAL(finishedSearch(QString)), m_blastSearchThread, SLOT(quit()));
        connect(runBlastSearchWorker, SIGNAL(finishedSearch(QString)), runBlastSearchWorker, SLOT(deleteLater()));
        connect(runBlastSearchWorker, SIGNAL(finishedSearch(QString)), this, SLOT(runBlastSearchFinished(QString)));
        connect(m_blastSearchThread, SIGNAL(finished()), m_blastSearchThread, SLOT(deleteLater()));
        connect(m_blastSearchThread, SIGNAL(finished()), progress, SLOT(deleteLater()));

        m_blastSearchThread->start();
    } else {
        RunBlastSearchWorker runBlastSearchWorker(m_blastnCommand, m_tblastnCommand, ui->parametersLineEdit->text().simplified());
        runBlastSearchWorker.runBlastSearch();
        progress->close();
        delete progress;
        runBlastSearchFinished(runBlastSearchWorker.m_error);
    }
}



void BlastSearchDialog::runBlastSearchFinished(const QString& error) {
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Error", error);
        setUiStep(READY_FOR_BLAST_SEARCH);
    } else {
        fillTablesAfterBlastSearch();
        g_settings->blastSearchParameters = ui->parametersLineEdit->text().simplified();
        setUiStep(BLAST_SEARCH_COMPLETE);
    }

    emit blastChanged();
}

void BlastSearchDialog::runBlastSearchCancelled() {
    g_blastSearch->m_cancelRunBlastSearch = true;
    if (g_blastSearch->m_blast)
        g_blastSearch->m_blast->kill();
}

void BlastSearchDialog::setUiStep(BlastUiState blastUiState)
{
    QPixmap tick(":/icons/tick-128.png");
    tick.setDevicePixelRatio(devicePixelRatio()); //This is a workaround for a Qt bug.  Can possibly remove in the future.  https://bugreports.qt.io/browse/QTBUG-46846

    switch (blastUiState)
    {
    case BLAST_DB_NOT_YET_BUILT:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(true);
        ui->step2Label->setEnabled(false);
        ui->loadQueriesFromFastaButton->setEnabled(false);
        ui->enterQueryManuallyButton->setEnabled(false);
        ui->blastQueriesTable->setEnabled(false);
        ui->blastQueriesTableInfoText->setEnabled(false);
        ui->step3Label->setEnabled(false);
        ui->parametersLabel->setEnabled(false);
        ui->parametersLineEdit->setEnabled(false);
        ui->runBlastSearchButton->setEnabled(false);
        ui->clearAllQueriesButton->setEnabled(false);
        ui->clearSelectedQueriesButton->setEnabled(false);
        ui->hitsLabel->setEnabled(false);
        ui->step1TickLabel->setPixmap(QPixmap());
        ui->step2TickLabel->setPixmap(QPixmap());
        ui->step3TickLabel->setPixmap(QPixmap());
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(false);
        ui->enterQueryManuallyInfoText->setEnabled(false);
        ui->clearAllQueriesInfoText->setEnabled(false);
        ui->clearSelectedQueriesInfoText->setEnabled(false);
        ui->blastHitsTable->setEnabled(false);
        ui->blastSearchWidget->setEnabled(false);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_DB_BUILD_IN_PROGRESS:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(false);
        ui->loadQueriesFromFastaButton->setEnabled(false);
        ui->enterQueryManuallyButton->setEnabled(false);
        ui->blastQueriesTable->setEnabled(false);
        ui->blastQueriesTableInfoText->setEnabled(false);
        ui->step3Label->setEnabled(false);
        ui->parametersLabel->setEnabled(false);
        ui->parametersLineEdit->setEnabled(false);
        ui->runBlastSearchButton->setEnabled(false);
        ui->clearAllQueriesButton->setEnabled(false);
        ui->clearSelectedQueriesButton->setEnabled(false);
        ui->hitsLabel->setEnabled(false);
        ui->step1TickLabel->setPixmap(QPixmap());
        ui->step2TickLabel->setPixmap(QPixmap());
        ui->step3TickLabel->setPixmap(QPixmap());
        ui->buildBlastDatabaseInfoText->setEnabled(false);
        ui->loadQueriesFromFastaInfoText->setEnabled(false);
        ui->enterQueryManuallyInfoText->setEnabled(false);
        ui->clearAllQueriesInfoText->setEnabled(false);
        ui->clearSelectedQueriesInfoText->setEnabled(false);
        ui->blastHitsTable->setEnabled(false);
        ui->blastSearchWidget->setEnabled(false);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_DB_BUILT_BUT_NO_QUERIES:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTable->setEnabled(true);
        ui->blastQueriesTableInfoText->setEnabled(true);
        ui->step3Label->setEnabled(false);
        ui->parametersLabel->setEnabled(false);
        ui->parametersLineEdit->setEnabled(false);
        ui->runBlastSearchButton->setEnabled(false);
        ui->clearAllQueriesButton->setEnabled(false);
        ui->clearAllQueriesButton->setEnabled(false);
        ui->hitsLabel->setEnabled(false);
        ui->step1TickLabel->setPixmap(tick);
        ui->step2TickLabel->setPixmap(QPixmap());
        ui->step3TickLabel->setPixmap(QPixmap());
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(true);
        ui->enterQueryManuallyInfoText->setEnabled(true);
        ui->clearSelectedQueriesInfoText->setEnabled(false);
        ui->clearSelectedQueriesInfoText->setEnabled(false);
        ui->blastHitsTable->setEnabled(false);
        ui->blastSearchWidget->setEnabled(false);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case READY_FOR_BLAST_SEARCH:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTable->setEnabled(true);
        ui->blastQueriesTableInfoText->setEnabled(true);
        ui->step3Label->setEnabled(true);
        ui->parametersLabel->setEnabled(true);
        ui->parametersLineEdit->setEnabled(true);
        ui->runBlastSearchButton->setEnabled(true);
        ui->clearAllQueriesButton->setEnabled(true);
        ui->hitsLabel->setEnabled(false);
        ui->step1TickLabel->setPixmap(tick);
        ui->step2TickLabel->setPixmap(tick);
        ui->step3TickLabel->setPixmap(QPixmap());
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(true);
        ui->enterQueryManuallyInfoText->setEnabled(true);
        ui->clearAllQueriesInfoText->setEnabled(true);
        ui->clearSelectedQueriesInfoText->setEnabled(true);
        ui->blastHitsTable->setEnabled(false);
        ui->blastSearchWidget->setEnabled(true);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_SEARCH_IN_PROGRESS:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTable->setEnabled(true);
        ui->blastQueriesTableInfoText->setEnabled(true);
        ui->step3Label->setEnabled(true);
        ui->parametersLabel->setEnabled(true);
        ui->parametersLineEdit->setEnabled(true);
        ui->runBlastSearchButton->setEnabled(false);
        ui->clearAllQueriesButton->setEnabled(true);
        ui->hitsLabel->setEnabled(false);
        ui->step1TickLabel->setPixmap(tick);
        ui->step2TickLabel->setPixmap(tick);
        ui->step3TickLabel->setPixmap(QPixmap());
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(true);
        ui->enterQueryManuallyInfoText->setEnabled(true);
        ui->clearAllQueriesInfoText->setEnabled(true);
        ui->clearSelectedQueriesInfoText->setEnabled(true);
        ui->blastHitsTable->setEnabled(false);
        ui->blastSearchWidget->setEnabled(true);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_SEARCH_COMPLETE:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTable->setEnabled(true);
        ui->blastQueriesTableInfoText->setEnabled(true);
        ui->step3Label->setEnabled(true);
        ui->parametersLabel->setEnabled(true);
        ui->parametersLineEdit->setEnabled(true);
        ui->runBlastSearchButton->setEnabled(true);
        ui->clearAllQueriesButton->setEnabled(true);
        ui->hitsLabel->setEnabled(true);
        ui->step1TickLabel->setPixmap(tick);
        ui->step2TickLabel->setPixmap(tick);
        ui->step3TickLabel->setPixmap(tick);
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(true);
        ui->enterQueryManuallyInfoText->setEnabled(true);
        ui->clearAllQueriesInfoText->setEnabled(true);
        ui->clearSelectedQueriesInfoText->setEnabled(true);
        ui->blastHitsTable->setEnabled(true);
        ui->blastSearchWidget->setEnabled(true);
        ui->blastHitsTableInfoText->setEnabled(true);
        break;
    }
}

void BlastSearchDialog::openFiltersDialog() {
    BlastHitFiltersDialog filtersDialog(this);
    filtersDialog.setWidgetsFromSettings();

    if (!filtersDialog.exec())
        return; //The user did not click OK

    filtersDialog.setSettingsFromWidgets();
    setFilterText();
}

void BlastSearchDialog::setFilterText() {
    ui->blastHitFiltersLabel->setText("Current filters: " + BlastHitFiltersDialog::getFilterText());
}

QueriesListModel::QueriesListModel(BlastQueries &queries, QObject *parent)
  : m_queries(queries), QAbstractTableModel(parent) {}

int QueriesListModel::rowCount(const QModelIndex &) const {
    return m_queries.getQueryCount();
}

int QueriesListModel::columnCount(const QModelIndex &) const {
    return int(QueriesHitColumns::TotalHitColumns);
}

QVariant QueriesListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return {};

    auto *query = this->query(index);
    if (!query)
        return {};

    auto column = QueriesHitColumns(index.column());
    if (role == Qt::BackgroundRole) {
        if (query->isHidden()) // Hide disabled queries
            return QColor(150, 150, 150);
        else if (column == QueriesHitColumns::Color)
            return query->getColour();
    }

    if (role == Qt::CheckStateRole) {
        if (column == QueriesHitColumns::Show)
            return query->isShown() ? Qt::Checked : Qt::Unchecked;
    }

    if (role == Qt::TextAlignmentRole) {
        if (column == QueriesHitColumns::Show)
            return Qt::AlignCenter;
    }

    if (role == Qt::EditRole && column == QueriesHitColumns::QueryName)
        return query->getName();

    if (role != Qt::DisplayRole)
        return {};

    switch (column) {
        default:
            return {};
        case QueriesHitColumns::QueryName:
            return query->getName();
        case QueriesHitColumns::Type:
            return query->getTypeString();
        case QueriesHitColumns::Length:
            return query->getLength();
        case QueriesHitColumns::Hits:
            if (query->wasSearchedFor())
                return query->hitCount();
            return "-";
        case QueriesHitColumns::QueryCover:
            if (query->wasSearchedFor())
                return formatDoubleForDisplay(100.0 * query->fractionCoveredByHits(), 2) + "%";
            return "-";
        case QueriesHitColumns::Paths:
            if (query->wasSearchedFor())
                return query->getPathCount();
            return "-";
    }
};

QVariant QueriesListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::TextAlignmentRole && orientation == Qt::Horizontal)
        return Qt::AlignCenter;

    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return QString::number(section + 1);

    switch (QueriesHitColumns(section)) {
        default:
            return {};
        case QueriesHitColumns::Show:
            return "Show";
        case QueriesHitColumns::QueryName:
            return "Query name";
        case QueriesHitColumns::Type:
            return "Type";
        case QueriesHitColumns::Length:
            return "Length";
        case QueriesHitColumns::Hits:
            return "Hits";
        case QueriesHitColumns::QueryCover:
            return "Query cover";
        case QueriesHitColumns::Paths:
            return "Paths";
    }
}

Qt::ItemFlags QueriesListModel::flags(const QModelIndex &index) const {
    if (!index.isValid())
        return Qt::ItemIsEnabled;

    auto column = QueriesHitColumns(index.column());
    if (column == QueriesHitColumns::Show)
        return QAbstractTableModel::flags(index) | Qt::ItemIsUserCheckable;
    else if (column == QueriesHitColumns::QueryName)
        return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
    else if (column == QueriesHitColumns::Color || column == QueriesHitColumns::Paths)
        return Qt::ItemIsEnabled;

    return QAbstractTableModel::flags(index);
}

bool QueriesListModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid())
        return false;

    auto *query = this->query(index);
    if (!query)
        return false;

    auto column = QueriesHitColumns(index.column());
    if (role == Qt::CheckStateRole && column == QueriesHitColumns::Show) { // Implement query shown checkbox
        query->setShown(value.toBool());
        emit dataChanged(index, QModelIndex()); // Here we need to refresh the whole row
        return true;
    } else if (role == Qt::EditRole && column == QueriesHitColumns::QueryName) { // Change query name
        QString newName = value.toString();
        if (newName != query->getName()) {
            m_queries.renameQuery(query, newName);
            emit dataChanged(index, index);
            return true;
        }
    }

    return false;
}

BlastQuery *QueriesListModel::query(const QModelIndex &index) const {
    if (!index.isValid() || index.row() >= m_queries.getQueryCount())
        return nullptr;

    return m_queries.m_queries[index.row()];
}

void QueriesListModel::setColor(const QModelIndex &index, QColor color) {
    if (!index.isValid())
        return;

    auto column = QueriesHitColumns(index.column());
    if (column != QueriesHitColumns::Color)
        return;

    if (auto *query = this->query(index)) {
        query->setColour(color);
        emit dataChanged(index, index);
    };
}

void PathButtonDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const auto *model = qobject_cast<const QSortFilterProxyModel*>(index.model());
    auto *query = qobject_cast<const QueriesListModel*>(model->sourceModel())->query(model->mapToSource(index));
    if (query && query->wasSearchedFor()) {
        QStyleOptionButton btn;
        btn.features = QStyleOptionButton::None;
        btn.rect = option.rect;
        btn.state = option.state | QStyle::State_Enabled | QStyle::State_Raised;
        btn.text = QString::number(query->getPathCount());

        QStyle *style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_PushButton, &btn, painter);
        return;
    }

    QStyledItemDelegate::paint(painter, option, index);
}

bool PathButtonDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *proxyModel = qobject_cast<const QSortFilterProxyModel*>(model);
        auto *query = qobject_cast<const QueriesListModel*>(proxyModel->sourceModel())->query(proxyModel->mapToSource(index));
        if (query && query->wasSearchedFor()) {
            auto *queryPathsDialog = new QueryPathsDialog(nullptr, query);

            connect(queryPathsDialog,
                    &QueryPathsDialog::selectionChanged,
                    [this]() {
                        emit queryPathSelectionChanged();
                    });

            connect(queryPathsDialog, &QueryPathsDialog::finished,
                    queryPathsDialog, &QueryPathsDialog::deleteLater);

            queryPathsDialog->show();
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

HitsListModel::HitsListModel(BlastHits &hits, QObject *parent)
 : m_hits(hits) {}

int HitsListModel::columnCount(const QModelIndex &) const {
    return int(HitsColumns::TotalHitColumns);
}

int HitsListModel::rowCount(const QModelIndex &) const {
    return m_hits.size();
}

QVariant HitsListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_hits.size())
        return {};

    auto column = HitsColumns(index.column());
    const auto &hit = *m_hits[index.row()];
    const auto &hitQuery = *hit.m_query;

    if (role == Qt::BackgroundRole) {
        if (hitQuery.isHidden()) // Hide disabled queries
            return QColor(150, 150, 150);
        else if (column == HitsColumns::Color)
            return hitQuery.getColour();
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (column) {
        default:
            return {};
        case HitsColumns::QueryName:
            return hitQuery.getName();
        case HitsColumns::NodeName:
            return hit.m_node->getName();
        case HitsColumns::PercentIdentity:
            return formatDoubleForDisplay(hit.m_percentIdentity, 2) + "%";
        case HitsColumns::AlignmentLength:
            return hit.m_alignmentLength;
        case HitsColumns::QueryCover:
            return formatDoubleForDisplay(100.0 * hit.getQueryCoverageFraction(), 2) + "%";
        case HitsColumns::Mismatches:
            return hit.m_numberMismatches;
        case HitsColumns::GapOpens:
            return hit.m_numberGapOpens;
        case HitsColumns::QueryStart:
            return hit.m_queryStart;
        case HitsColumns::QueryEnd:
            return hit.m_queryEnd;
        case HitsColumns::NodeStart:
            return hit.m_nodeStart;
        case HitsColumns::NodeEnd:
            return hit.m_nodeEnd;
        case HitsColumns::Evalue:
            return hit.m_eValue.asString(false);
        case HitsColumns::BitScore:
            return hit.m_bitScore;
    }

    return {};
}

QVariant HitsListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::TextAlignmentRole && orientation == Qt::Horizontal)
        return Qt::AlignCenter;

    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return QString::number(section + 1);

    switch (HitsColumns(section)) {
        default:
            return {};
        case HitsColumns::QueryName:
            return "Query\nname";
        case HitsColumns::NodeName:
            return "Node\nname";
        case HitsColumns::PercentIdentity:
            return "Percent\nidentity";
        case HitsColumns::AlignmentLength:
            return "Alignment\nlength";
        case HitsColumns::QueryCover:
            return "Query\ncover";
        case HitsColumns::Mismatches:
            return "Mis-\nmatches";
        case HitsColumns::GapOpens:
            return "Gap\nopens";
        case HitsColumns::QueryStart:
            return "Query\nstart";
        case HitsColumns::QueryEnd:
            return "Query\nend";
        case HitsColumns::NodeStart:
            return "Node\nstart";
        case HitsColumns::NodeEnd:
            return "Node\nend";
        case HitsColumns::Evalue:
            return "E-\nvalue";
        case HitsColumns::BitScore:
            return "Bit\nscore";
    }
}
