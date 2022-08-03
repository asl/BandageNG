﻿//Copyright 2017 Ryan Wick

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
#include "tablewidgetitemname.h"
#include "tablewidgetitemint.h"
#include "tablewidgetitemdouble.h"
#include "tablewidgetitemshown.h"
#include "ui/widgets/colourbutton.h"
#include "querypathspushbutton.h"
#include "querypathsdialog.h"
#include "blasthitfiltersdialog.h"

#include "program/globals.h"
#include "program/settings.h"
#include "program/memory.h"

#include <QFileDialog>
#include <QFile>
#include <QString>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QSet>
#include <QCheckBox>

BlastSearchDialog::BlastSearchDialog(QWidget *parent, const QString& autoQuery) :
    QDialog(parent),
    ui(new Ui::BlastSearchDialog),
    m_makeblastdbCommand("makeblastdb"), m_blastnCommand("blastn"),
    m_tblastnCommand("tblastn"), m_queryPathsDialog(nullptr)
{
    ui->setupUi(this);

    setWindowFlags(windowFlags() | Qt::Tool);

    ui->blastHitsTableWidget->m_smallFirstColumn = true;
    ui->blastQueriesTableWidget->m_smallFirstColumn = true;
    ui->blastQueriesTableWidget->m_smallSecondColumn = true;

    setFilterText();

    //Load any previous parameters the user might have entered when previously using this dialog.
    ui->parametersLineEdit->setText(g_settings->blastSearchParameters);

    //If the dialog is given an autoQuery parameter, then it will
    //carry out the entire process on its own.
    if (autoQuery != "")
    {
        buildBlastDatabase(false);
        clearAllQueries();
        loadBlastQueriesFromFastaFile(autoQuery);
        runBlastSearches(false);
        QMetaObject::invokeMethod(this, "close", Qt::QueuedConnection);
        return;
    }

    //Prepare the query and hits tables
    ui->blastHitsTableWidget->setHorizontalHeaderLabels(QStringList() << "" << "Query\nname" << "Node\nname" <<
                                                        "Percent\nidentity" << "Alignment\nlength" << "Query\ncover" << "Mis-\nmatches" <<
                                                        "Gap\nopens" << "Query\nstart" << "Query\nend" << "Node\nstart" <<
                                                        "Node\nend" <<"E-\nvalue" << "Bit\nscore");
    QFont font = ui->blastQueriesTableWidget->horizontalHeader()->font();
    font.setBold(true);
    ui->blastQueriesTableWidget->horizontalHeader()->setFont(font);
    ui->blastHitsTableWidget->horizontalHeader()->setFont(font);


    //If a BLAST database already exists, move to step 2.
    QFile databaseFile =g_blastSearch->m_tempDirectory.filePath("all_nodes.fasta");
    if (databaseFile.exists())
        setUiStep(BLAST_DB_BUILT_BUT_NO_QUERIES);

    //If there isn't a BLAST database, clear the entire temporary directory
    //and move to step 1.
    else
    {
        g_blastSearch->emptyTempDirectory();
        setUiStep(BLAST_DB_NOT_YET_BUILT);
    }

    //If queries already exist, display them and move to step 3.
    if (!g_blastSearch->m_blastQueries.m_queries.empty())
    {
        fillQueriesTable();
        setUiStep(READY_FOR_BLAST_SEARCH);
    }

    //If results already exist, display them and move to step 4.
    if (!g_blastSearch->m_allHits.empty())
    {
        fillHitsTable();
        setUiStep(BLAST_SEARCH_COMPLETE);
    }

    //Call this function to disable rows in either table that are for queries
    //the user has hidden.
    queryShownChanged();

    connect(ui->buildBlastDatabaseButton, SIGNAL(clicked()), this, SLOT(buildBlastDatabaseInThread()));
    connect(ui->loadQueriesFromFastaButton, SIGNAL(clicked()), this, SLOT(loadBlastQueriesFromFastaFileButtonClicked()));
    connect(ui->enterQueryManuallyButton, SIGNAL(clicked()), this, SLOT(enterQueryManually()));
    connect(ui->clearAllQueriesButton, SIGNAL(clicked()), this, SLOT(clearAllQueries()));
    connect(ui->clearSelectedQueriesButton, SIGNAL(clicked(bool)), this, SLOT(clearSelectedQueries()));
    connect(ui->runBlastSearchButton, SIGNAL(clicked()), this, SLOT(runBlastSearchesInThread()));
    connect(ui->blastQueriesTableWidget, SIGNAL(cellChanged(int,int)), this, SLOT(queryCellChanged(int,int)));
    connect(ui->blastQueriesTableWidget, SIGNAL(itemSelectionChanged()), this, SLOT(queryTableSelectionChanged()));
    connect(ui->blastFiltersButton, SIGNAL(clicked(bool)), this, SLOT(openFiltersDialog()));
}

BlastSearchDialog::~BlastSearchDialog()
{
    delete ui;
    deleteQueryPathsDialog();
}



void BlastSearchDialog::afterWindowShow()
{
    ui->blastQueriesTableWidget->resizeColumns();
    ui->blastHitsTableWidget->resizeColumns();
}

void BlastSearchDialog::clearBlastHits()
{
    g_blastSearch->clearBlastHits();
    deleteQueryPathsDialog();
    ui->blastHitsTableWidget->clearContents();
    while (ui->blastHitsTableWidget->rowCount() > 0)
        ui->blastHitsTableWidget->removeRow(0);
    g_annotationsManager->removeGroupByName(g_settings->blastAnnotationGroupName);
}

void BlastSearchDialog::fillTablesAfterBlastSearch()
{
    if (g_blastSearch->m_allHits.empty())
        QMessageBox::information(this, "No hits", "No BLAST hits were found for the given queries and parameters.");

    fillQueriesTable();
    fillHitsTable();
}


void BlastSearchDialog::fillQueriesTable()
{
    //Turn off table widget signals for this function so the
    //queryCellChanged slot doesn't get called.
    ui->blastQueriesTableWidget->blockSignals(true);
    ui->blastQueriesTableWidget->setSortingEnabled(false);

    ui->blastQueriesTableWidget->clearContents();

    int queryCount = int(g_blastSearch->m_blastQueries.m_queries.size());
    if (queryCount == 0)
        return;

    ui->blastQueriesTableWidget->setRowCount(queryCount);

    for (int i = 0; i < queryCount; ++i)
        makeQueryRow(i);

    ui->blastQueriesTableWidget->resizeColumns();
    ui->blastQueriesTableWidget->setSortingEnabled(true);

    ui->blastQueriesTableWidget->setSortingEnabled(true);
    ui->blastQueriesTableWidget->blockSignals(false);
}

void BlastSearchDialog::makeQueryRow(int row)
{
    if (row >= int(g_blastSearch->m_blastQueries.m_queries.size()))
        return;

    BlastQuery * query = g_blastSearch->m_blastQueries.m_queries[row];

    auto * name = new TableWidgetItemName(query);
    name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);

    auto * type = new QTableWidgetItem(query->getTypeString());
    type->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    int queryLength = query->getLength();
    auto * length = new TableWidgetItemInt(formatIntForDisplay(queryLength), queryLength);
    length->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    //If the search hasn't yet been run, some of the columns will just have
    //a dash.
    TableWidgetItemInt * hits;
    TableWidgetItemDouble * percent;
    QTableWidgetItem * paths;

    QueryPathsPushButton * pathsButton = nullptr;
    if (query->wasSearchedFor())
    {
        int hitCount = query->hitCount();
        hits = new TableWidgetItemInt(formatIntForDisplay(hitCount), hitCount);
        percent = new TableWidgetItemDouble(formatDoubleForDisplay(100.0 * query->fractionCoveredByHits(), 2) + "%", query->fractionCoveredByHits());

        //The path count isn't displayed in the TableWidgetItem because it will
        //be shown in a button which will bring up a separate dialog showing a
        //table of the paths.
        int pathCount = query->getPathCount();
        paths = new TableWidgetItemInt("", pathCount);
        paths->setFlags(Qt::ItemIsEnabled);
        pathsButton = new QueryPathsPushButton(pathCount, query);
        connect(pathsButton, SIGNAL(showPathsDialog(BlastQuery*)), this, SLOT(showPathsDialog(BlastQuery*)));
    }
    else
    {
        hits = new TableWidgetItemInt("-", 0);
        percent = new TableWidgetItemDouble("-", 0.0);
        paths = new QTableWidgetItem("-");
    }

    hits->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    percent->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    paths->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    auto * colour = new QTableWidgetItem(query->getColour().name());
    auto * colourButton = new ColourButton();
    colourButton->setColour(query->getColour());
    connect(colourButton, SIGNAL(colourChosen(QColor)), query, SLOT(setColour(QColor)));
    connect(colourButton, SIGNAL(colourChosen(QColor)), this, SLOT(fillHitsTable()));

    auto * showCheckBoxWidget = new QWidget;
    auto * showCheckBox = new QCheckBox();
    auto * layout = new QHBoxLayout(showCheckBoxWidget);
    layout->addWidget(showCheckBox);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(0, 0, 0, 0);
    showCheckBoxWidget->setLayout(layout);
    bool queryShown = query->isShown();
    showCheckBox->setChecked(queryShown);
    QTableWidgetItem * show = new TableWidgetItemShown(queryShown);
    show->setFlags(Qt::ItemIsEnabled);
    connect(showCheckBox, SIGNAL(toggled(bool)), query, SLOT(setShown(bool)));
    connect(showCheckBox, SIGNAL(toggled(bool)), this, SLOT(queryShownChanged()));

    ui->blastQueriesTableWidget->setCellWidget(row, 0, colourButton);
    ui->blastQueriesTableWidget->setCellWidget(row, 1, showCheckBoxWidget);
    ui->blastQueriesTableWidget->setItem(row, 0, colour);
    ui->blastQueriesTableWidget->setItem(row, 1, show);
    ui->blastQueriesTableWidget->setItem(row, 2, name);
    ui->blastQueriesTableWidget->setItem(row, 3, type);
    ui->blastQueriesTableWidget->setItem(row, 4, length);
    ui->blastQueriesTableWidget->setItem(row, 5, hits);
    ui->blastQueriesTableWidget->setItem(row, 6, percent);
    ui->blastQueriesTableWidget->setItem(row, 7, paths);
    if (pathsButton != nullptr)
        ui->blastQueriesTableWidget->setCellWidget(row, 7, pathsButton);
}


void BlastSearchDialog::fillHitsTable()
{
    ui->blastHitsTableWidget->clearContents();
    ui->blastHitsTableWidget->setSortingEnabled(false);

    int hitCount = g_blastSearch->m_allHits.size();
    ui->blastHitsTableWidget->setRowCount(hitCount);

    if (hitCount == 0)
        return;

    for (int i = 0; i < hitCount; ++i)
    {
        const BlastHit &hit = *g_blastSearch->m_allHits[i];
        const BlastQuery &hitQuery = *hit.m_query;

        auto *queryColour = new QTableWidgetItem(hitQuery.getColour().name());
        queryColour->setFlags(Qt::ItemIsEnabled);
        queryColour->setBackground(hitQuery.getColour());

        auto *queryName = new QTableWidgetItem(hitQuery.getName());
        queryName->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *nodeName = new QTableWidgetItem(hit.m_node->getName());
        nodeName->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *percentIdentity = new TableWidgetItemDouble(formatDoubleForDisplay(hit.m_percentIdentity, 2) + "%", hit.m_percentIdentity);
        percentIdentity->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *alignmentLength = new TableWidgetItemInt(formatIntForDisplay(hit.m_alignmentLength), hit.m_alignmentLength);
        alignmentLength->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        double queryCoverPercent = 100.0 * hit.getQueryCoverageFraction();
        auto *queryCover = new TableWidgetItemDouble(formatDoubleForDisplay(queryCoverPercent, 2) + "%", queryCoverPercent);
        queryCover->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *numberMismatches = new TableWidgetItemInt(formatIntForDisplay(hit.m_numberMismatches), hit.m_numberMismatches);
        numberMismatches->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *numberGapOpens = new TableWidgetItemInt(formatIntForDisplay(hit.m_numberGapOpens), hit.m_numberGapOpens);
        numberGapOpens->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *queryStart = new TableWidgetItemInt(formatIntForDisplay(hit.m_queryStart), hit.m_queryStart);
        queryStart->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *queryEnd = new TableWidgetItemInt(formatIntForDisplay(hit.m_queryEnd), hit.m_queryEnd);
        queryEnd->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *nodeStart = new TableWidgetItemInt(formatIntForDisplay(hit.m_nodeStart), hit.m_nodeStart);
        nodeStart->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *nodeEnd = new TableWidgetItemInt(formatIntForDisplay(hit.m_nodeEnd), hit.m_nodeEnd);
        nodeEnd->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *eValue = new TableWidgetItemDouble(hit.m_eValue.asString(false), hit.m_eValue.toDouble());
        eValue->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto *bitScore = new TableWidgetItemDouble(QString::number(hit.m_bitScore), hit.m_bitScore);
        bitScore->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        ui->blastHitsTableWidget->setItem(i, 0, queryColour);
        ui->blastHitsTableWidget->setItem(i, 1, queryName);
        ui->blastHitsTableWidget->setItem(i, 2, nodeName);
        ui->blastHitsTableWidget->setItem(i, 3, percentIdentity);
        ui->blastHitsTableWidget->setItem(i, 4, alignmentLength);
        ui->blastHitsTableWidget->setItem(i, 5, queryCover);
        ui->blastHitsTableWidget->setItem(i, 6, numberMismatches);
        ui->blastHitsTableWidget->setItem(i, 7, numberGapOpens);
        ui->blastHitsTableWidget->setItem(i, 8, queryStart);
        ui->blastHitsTableWidget->setItem(i, 9, queryEnd);
        ui->blastHitsTableWidget->setItem(i, 10, nodeStart);
        ui->blastHitsTableWidget->setItem(i, 11, nodeEnd);
        ui->blastHitsTableWidget->setItem(i, 12, eValue);
        ui->blastHitsTableWidget->setItem(i, 13, bitScore);
    }

    ui->blastHitsTableWidget->resizeColumns();
    ui->blastHitsTableWidget->setEnabled(true);
    ui->blastHitsTableWidget->setSortingEnabled(true);
}

void BlastSearchDialog::buildBlastDatabaseInThread()
{
    buildBlastDatabase(true);
}

void BlastSearchDialog::buildBlastDatabase(bool separateThread)
{
    setUiStep(BLAST_DB_BUILD_IN_PROGRESS);

    if (!g_blastSearch->findProgram("makeblastdb", &m_makeblastdbCommand))
    {
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

    if (separateThread)
    {
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
    }
    else
    {
        BuildBlastDatabaseWorker buildBlastDatabaseWorker(m_makeblastdbCommand);
        buildBlastDatabaseWorker.buildBlastDatabase();
        progress->close();
        delete progress;
        blastDatabaseBuildFinished(buildBlastDatabaseWorker.m_error);
    }
}



void BlastSearchDialog::blastDatabaseBuildFinished(const QString& error)
{
    if (error != "")
    {
        QMessageBox::warning(this, "Error", error);
        setUiStep(BLAST_DB_NOT_YET_BUILT);
    }
    else
        setUiStep(BLAST_DB_BUILT_BUT_NO_QUERIES);
}


void BlastSearchDialog::buildBlastDatabaseCancelled()
{
    g_blastSearch->m_cancelBuildBlastDatabase = true;
    if (g_blastSearch->m_makeblastdb != nullptr)
        g_blastSearch->m_makeblastdb->kill();
}

void BlastSearchDialog::loadBlastQueriesFromFastaFileButtonClicked()
{
    QStringList fullFileNames = QFileDialog::getOpenFileNames(this, "Load queries FASTA", g_memory->rememberedPath);

    if (fullFileNames.empty()) //User did hit cancel
        return;

    for (const auto & fullFileName : fullFileNames)
        loadBlastQueriesFromFastaFile(fullFileName);
}

void BlastSearchDialog::loadBlastQueriesFromFastaFile(const QString& fullFileName)
{
    auto * progress = new MyProgressDialog(this, "Loading queries...", false);
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    int queriesLoaded = g_blastSearch->loadBlastQueriesFromFastaFile(fullFileName);
    if (queriesLoaded > 0)
    {
        clearBlastHits();
        fillQueriesTable();
        g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
        setUiStep(READY_FOR_BLAST_SEARCH);
    }

    progress->close();
    progress->deleteLater();

    if (queriesLoaded == 0)
        QMessageBox::information(this, "No queries loaded", "No queries could be loaded from the specified file.");
}



void BlastSearchDialog::enterQueryManually()
{
    EnterOneBlastQueryDialog enterOneBlastQueryDialog(this);
    if (!enterOneBlastQueryDialog.exec())
        return;

    QString queryName = g_blastSearch->cleanQueryName(enterOneBlastQueryDialog.getName());
    g_blastSearch->m_blastQueries.addQuery(new BlastQuery(queryName,
                                                          enterOneBlastQueryDialog.getSequence()));
    clearBlastHits();
    fillQueriesTable();

    setUiStep(READY_FOR_BLAST_SEARCH);
}



void BlastSearchDialog::clearAllQueries()
{
    g_blastSearch->m_blastQueries.clearAllQueries();
    ui->blastQueriesTableWidget->clearContents();
    ui->clearAllQueriesButton->setEnabled(false);

    while (ui->blastQueriesTableWidget->rowCount() > 0)
        ui->blastQueriesTableWidget->removeRow(0);

    clearBlastHits();
    setUiStep(BLAST_DB_BUILT_BUT_NO_QUERIES);
    emit blastChanged();
}


void BlastSearchDialog::clearSelectedQueries()
{
    //Use the table selection to figure out which queries are to be removed.
    //The table cell containing the query name also has a pointer to the
    //actual query, and that's what we use.
    std::vector<BlastQuery *> queriesToRemove;
    QItemSelectionModel * select = ui->blastQueriesTableWidget->selectionModel();
    QModelIndexList selection = select->selectedIndexes();
    QSet<int> rowsWithSelectionSet;
    for (const auto & i : selection)
        rowsWithSelectionSet.insert(i.row());
    for (auto row : rowsWithSelectionSet) {
        QTableWidgetItem * tableWidgetItem = ui->blastQueriesTableWidget->item(row, 2);
        auto * queryNameItem = dynamic_cast<TableWidgetItemName *>(tableWidgetItem);
        if (queryNameItem == nullptr)
            continue;
        BlastQuery * query = queryNameItem->getQuery();
        queriesToRemove.push_back(query);
    }

    if (queriesToRemove.size() == g_blastSearch->m_blastQueries.m_queries.size())
    {
        clearAllQueries();
        return;
    }

    g_blastSearch->clearSomeQueries(queriesToRemove);

    fillQueriesTable();
    fillHitsTable();
    emit blastChanged();
}

void BlastSearchDialog::runBlastSearchesInThread()
{
    runBlastSearches(true);
}


void BlastSearchDialog::runBlastSearches(bool separateThread)
{
    setUiStep(BLAST_SEARCH_IN_PROGRESS);

    if (!g_blastSearch->findProgram("blastn", &m_blastnCommand))
    {
        QMessageBox::warning(this, "Error", "The program blastn was not found.  Please install NCBI BLAST to use this feature.");
        setUiStep(READY_FOR_BLAST_SEARCH);
        return;
    }
    if (!g_blastSearch->findProgram("tblastn", &m_tblastnCommand))
    {
        QMessageBox::warning(this, "Error", "The program tblastn was not found.  Please install NCBI BLAST to use this feature.");
        setUiStep(READY_FOR_BLAST_SEARCH);
        return;
    }

    clearBlastHits();

    auto * progress = new MyProgressDialog(this, "Running BLAST search...", separateThread, "Cancel search", "Cancelling search...",
                                                       "Clicking this button will stop the BLAST search.");
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    if (separateThread)
    {
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
    }
    else
    {
        RunBlastSearchWorker runBlastSearchWorker(m_blastnCommand, m_tblastnCommand, ui->parametersLineEdit->text().simplified());
        runBlastSearchWorker.runBlastSearch();
        progress->close();
        delete progress;
        runBlastSearchFinished(runBlastSearchWorker.m_error);
    }
}



void BlastSearchDialog::runBlastSearchFinished(const QString& error)
{
    if (error != "")
    {
        QMessageBox::warning(this, "Error", error);
        setUiStep(READY_FOR_BLAST_SEARCH);
    }
    else
    {
        fillTablesAfterBlastSearch();
        g_settings->blastSearchParameters = ui->parametersLineEdit->text().simplified();
        setUiStep(BLAST_SEARCH_COMPLETE);
    }

    emit blastChanged();
}


void BlastSearchDialog::runBlastSearchCancelled()
{
    g_blastSearch->m_cancelRunBlastSearch = true;
    if (g_blastSearch->m_blast != nullptr)
        g_blastSearch->m_blast->kill();
}



void BlastSearchDialog::queryCellChanged(int row, int column)
{
    //Suspend signals for this function, as it is might change
    //the cell value again if the new name isn't unique.
    ui->blastQueriesTableWidget->blockSignals(true);

    //If a query name was changed, then we adjust that query name elsewhere.
    if (column == 2)
    {
        QString newName = ui->blastQueriesTableWidget->item(row, column)->text();
        BlastQuery * query = g_blastSearch->m_blastQueries.m_queries[row];

        if (newName != query->getName())
        {
            QString uniqueName = g_blastSearch->m_blastQueries.renameQuery(query, newName);

            //It's possible that the user gave the query a non-unique name, in which
            //case we now have to adjust it.
            if (uniqueName != newName)
                ui->blastQueriesTableWidget->item(row, column)->setText(uniqueName);

            //Resize the query table columns, as the name new might take up more or less space.
            ui->blastQueriesTableWidget->resizeColumns();

            //Rebuild the hits table, if necessary, to show the new name.
            if (query->hasHits())
                fillHitsTable();

            emit blastChanged();
        }
    }

    ui->blastQueriesTableWidget->blockSignals(false);
}


void BlastSearchDialog::queryTableSelectionChanged()
{
    //If there are any selected items, then the 'Clear selected' button
    //should be enabled.
    QItemSelectionModel * select = ui->blastQueriesTableWidget->selectionModel();
    bool hasSelection = select->hasSelection();

    ui->clearSelectedQueriesButton->setEnabled(hasSelection);
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
        ui->blastQueriesTableWidget->setEnabled(false);
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
        ui->blastHitsTableWidget->setEnabled(false);
        ui->blastSearchWidget->setEnabled(false);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_DB_BUILD_IN_PROGRESS:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(false);
        ui->loadQueriesFromFastaButton->setEnabled(false);
        ui->enterQueryManuallyButton->setEnabled(false);
        ui->blastQueriesTableWidget->setEnabled(false);
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
        ui->blastHitsTableWidget->setEnabled(false);
        ui->blastSearchWidget->setEnabled(false);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_DB_BUILT_BUT_NO_QUERIES:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTableWidget->setEnabled(true);
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
        ui->blastHitsTableWidget->setEnabled(false);
        ui->blastSearchWidget->setEnabled(false);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case READY_FOR_BLAST_SEARCH:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTableWidget->setEnabled(true);
        ui->blastQueriesTableInfoText->setEnabled(true);
        ui->step3Label->setEnabled(true);
        ui->parametersLabel->setEnabled(true);
        ui->parametersLineEdit->setEnabled(true);
        ui->runBlastSearchButton->setEnabled(true);
        ui->clearAllQueriesButton->setEnabled(true);
        queryTableSelectionChanged();
        ui->hitsLabel->setEnabled(false);
        ui->step1TickLabel->setPixmap(tick);
        ui->step2TickLabel->setPixmap(tick);
        ui->step3TickLabel->setPixmap(QPixmap());
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(true);
        ui->enterQueryManuallyInfoText->setEnabled(true);
        ui->clearAllQueriesInfoText->setEnabled(true);
        ui->clearSelectedQueriesInfoText->setEnabled(true);
        ui->blastHitsTableWidget->setEnabled(false);
        ui->blastSearchWidget->setEnabled(true);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_SEARCH_IN_PROGRESS:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTableWidget->setEnabled(true);
        ui->blastQueriesTableInfoText->setEnabled(true);
        ui->step3Label->setEnabled(true);
        ui->parametersLabel->setEnabled(true);
        ui->parametersLineEdit->setEnabled(true);
        ui->runBlastSearchButton->setEnabled(false);
        ui->clearAllQueriesButton->setEnabled(true);
        queryTableSelectionChanged();
        ui->hitsLabel->setEnabled(false);
        ui->step1TickLabel->setPixmap(tick);
        ui->step2TickLabel->setPixmap(tick);
        ui->step3TickLabel->setPixmap(QPixmap());
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(true);
        ui->enterQueryManuallyInfoText->setEnabled(true);
        ui->clearAllQueriesInfoText->setEnabled(true);
        ui->clearSelectedQueriesInfoText->setEnabled(true);
        ui->blastHitsTableWidget->setEnabled(false);
        ui->blastSearchWidget->setEnabled(true);
        ui->blastHitsTableInfoText->setEnabled(false);
        break;

    case BLAST_SEARCH_COMPLETE:
        ui->step1Label->setEnabled(true);
        ui->buildBlastDatabaseButton->setEnabled(false);
        ui->step2Label->setEnabled(true);
        ui->loadQueriesFromFastaButton->setEnabled(true);
        ui->enterQueryManuallyButton->setEnabled(true);
        ui->blastQueriesTableWidget->setEnabled(true);
        ui->blastQueriesTableInfoText->setEnabled(true);
        ui->step3Label->setEnabled(true);
        ui->parametersLabel->setEnabled(true);
        ui->parametersLineEdit->setEnabled(true);
        ui->runBlastSearchButton->setEnabled(true);
        ui->clearAllQueriesButton->setEnabled(true);
        queryTableSelectionChanged();
        ui->hitsLabel->setEnabled(true);
        ui->step1TickLabel->setPixmap(tick);
        ui->step2TickLabel->setPixmap(tick);
        ui->step3TickLabel->setPixmap(tick);
        ui->buildBlastDatabaseInfoText->setEnabled(true);
        ui->loadQueriesFromFastaInfoText->setEnabled(true);
        ui->enterQueryManuallyInfoText->setEnabled(true);
        ui->clearAllQueriesInfoText->setEnabled(true);
        ui->clearSelectedQueriesInfoText->setEnabled(true);
        ui->blastHitsTableWidget->setEnabled(true);
        ui->blastSearchWidget->setEnabled(true);
        ui->blastHitsTableInfoText->setEnabled(true);
        break;
    }
}

//This function is called whenever a user changed the 'Show' tick box for a
//query.  It does three things:
// 1) Updates the 'shown' status of the TableWidgetItem so the table can be
//    sorted by that column.
// 2) Colours the QTableWidgetItems in the query table to match the query's
//    'shown' status.
// 3) Colours the QTableWidgetItems in the hits table to match the hit's query's
//    'shown' status.
void BlastSearchDialog::queryShownChanged()
{
    ui->blastQueriesTableWidget->blockSignals(true);

    QTableWidgetItem tempItem;
    QColor shownColour = tempItem.foreground().color();
    QColor hiddenColour = QColor(150, 150, 150);

    for (int i = 0; i < ui->blastQueriesTableWidget->rowCount(); ++i)
    {
        QString queryName = ui->blastQueriesTableWidget->item(i, 2)->text();
        BlastQuery * query = g_blastSearch->m_blastQueries.getQueryFromName(queryName);
        if (query == nullptr)
            continue;
        
        QTableWidgetItem * item = ui->blastQueriesTableWidget->item(i, 1);
        auto * shownItem = dynamic_cast<TableWidgetItemShown *>(item);

        if (shownItem == nullptr)
            continue;
        shownItem->m_shown = query->isShown();

        QColor colour = shownColour;
        if (query->isHidden())
            colour = hiddenColour;
        ui->blastQueriesTableWidget->item(i, 2)->setForeground(colour);
        ui->blastQueriesTableWidget->item(i, 3)->setForeground(colour);
        ui->blastQueriesTableWidget->item(i, 4)->setForeground(colour);
        ui->blastQueriesTableWidget->item(i, 5)->setForeground(colour);
        ui->blastQueriesTableWidget->item(i, 6)->setForeground(colour);
        ui->blastQueriesTableWidget->item(i, 7)->setForeground(colour);
    }

    for (int i = 0; i < ui->blastHitsTableWidget->rowCount(); ++i)
    {
        QString queryName = ui->blastHitsTableWidget->item(i, 1)->text();
        BlastQuery * query = g_blastSearch->m_blastQueries.getQueryFromName(queryName);
        if (query == nullptr)
            continue;

        QColor colour = shownColour;
        if (query->isHidden())
            colour = hiddenColour;

        ui->blastHitsTableWidget->item(i, 1)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 2)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 3)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 4)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 5)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 6)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 7)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 8)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 9)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 10)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 11)->setForeground(colour);
        ui->blastHitsTableWidget->item(i, 12)->setForeground(colour);
    }

    ui->blastQueriesTableWidget->blockSignals(false);
    emit blastChanged();
}



void BlastSearchDialog::showPathsDialog(BlastQuery * query)
{
    deleteQueryPathsDialog();

    m_queryPathsDialog = new QueryPathsDialog(this, query);

    connect(m_queryPathsDialog, SIGNAL(selectionChanged()), this, SLOT(queryPathSelectionChangedSlot()));

    m_queryPathsDialog->show();
}

void BlastSearchDialog::deleteQueryPathsDialog()
{
    delete m_queryPathsDialog;
    m_queryPathsDialog = nullptr;
}

void BlastSearchDialog::queryPathSelectionChangedSlot()
{
    emit queryPathSelectionChanged();
}

void BlastSearchDialog::openFiltersDialog()
{
    BlastHitFiltersDialog filtersDialog(this);
    filtersDialog.setWidgetsFromSettings();

    if (!filtersDialog.exec())
        return; //The user did not click OK

    filtersDialog.setSettingsFromWidgets();
    setFilterText();
}

void BlastSearchDialog::setFilterText()
{
    ui->blastHitFiltersLabel->setText("Current filters: " + BlastHitFiltersDialog::getFilterText());
}
