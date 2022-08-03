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


#include "querypathsdialog.h"
#include "ui_querypathsdialog.h"

#include "blast/blastquery.h"
#include "blast/blastquerypath.h"
#include "tablewidgetitemint.h"
#include "tablewidgetitemdouble.h"
#include "program/globals.h"
#include "program/memory.h"
#include <QTableWidgetItem>
#include "querypathsequencecopybutton.h"

QueryPathsDialog::QueryPathsDialog(QWidget * parent, BlastQuery * query) :
    QDialog(parent),
    ui(new Ui::QueryPathsDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() | Qt::Tool);

    connect(this, SIGNAL(rejected()), this, SLOT(hidden()));
    connect(ui->tableWidget, SIGNAL(itemSelectionChanged()), this, SLOT(tableSelectionChanged()));

    g_memory->queryPathDialogIsVisible = true;
    g_memory->queryPaths.clear();

    ui->tableWidget->setHorizontalHeaderLabels(QStringList() << "Path" << "Length\n(bp)" << "Query\ncovered\nby path" <<
                                               "Query\ncovered\nby hits" << "Mean hit\nidentity"  << "Total\nhit mis-\nmatches" <<
                                               "Total\nhit gap\nopens" << "Relative\nlength" << "Length\ndiscre-\npancy" <<
                                               "E-value\nproduct" << "Copy sequence\nto clipboard");

    QString queryDescription = "Query name: " + query->getName();
    queryDescription += "      type: " + query->getTypeString();
    queryDescription += "      length: " + formatIntForDisplay(query->getLength());
    if (query->getSequenceType() == PROTEIN)
        queryDescription += " (" + formatIntForDisplay(3 * query->getLength()) + " bp)";
    else
        queryDescription += " bp";
    ui->queryLabel->setText(queryDescription);

    ui->tableWidget->clearContents();
    ui->tableWidget->setSortingEnabled(false);

    int pathCount = query->getPathCount();
    ui->tableWidget->setRowCount(pathCount);

    if (pathCount == 0)
        return;

    QList<BlastQueryPath> paths = query->getPaths();

    for (int i = 0; i < pathCount; ++i)
    {
        BlastQueryPath * queryPath = &paths[i];

        auto * pathString = new QTableWidgetItem(queryPath->getPath().getString(true));
        pathString->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        int length = queryPath->getPath().getLength();
        auto * pathLength = new TableWidgetItemInt(formatIntForDisplay(length), length);
        pathLength->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        double queryCoveragePath = queryPath->getPathQueryCoverage();
        auto * pathQueryCoveragePath = new TableWidgetItemDouble(formatDoubleForDisplay(100.0 * queryCoveragePath, 2) + "%", queryCoveragePath);
        pathQueryCoveragePath->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        pathLength->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        double queryCoverageHits = queryPath->getHitsQueryCoverage();
        auto * pathQueryCoverageHits = new TableWidgetItemDouble(formatDoubleForDisplay(100.0 * queryCoverageHits, 2) + "%", queryCoverageHits);
        pathQueryCoverageHits->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        double percIdentity = queryPath->getMeanHitPercIdentity();
        auto * pathPercIdentity = new TableWidgetItemDouble(formatDoubleForDisplay(percIdentity, 2) + "%", percIdentity);
        pathPercIdentity->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        int mismatches = queryPath->getTotalHitMismatches();
        auto * pathMismatches = new TableWidgetItemInt(formatIntForDisplay(mismatches), mismatches);
        pathMismatches->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        int gapOpens = queryPath->getTotalHitGapOpens();
        auto * pathGapOpens = new TableWidgetItemInt(formatIntForDisplay(gapOpens), gapOpens);
        pathGapOpens->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        double relativeLength = queryPath->getRelativePathLength();
        auto * pathRelativeLength = new TableWidgetItemDouble(formatDoubleForDisplay(100.0 * relativeLength, 2) + "%", relativeLength);
        pathRelativeLength->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        int lengthDisc = queryPath->getAbsolutePathLengthDifference();
        QString lengthDiscString = queryPath->getAbsolutePathLengthDifferenceString(true);
        auto * pathLengthDisc = new TableWidgetItemInt(lengthDiscString, lengthDisc);
        pathLengthDisc->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        SciNot evalueProduct = queryPath->getEvalueProduct();
        auto * pathEvalueProduct = new TableWidgetItemDouble(evalueProduct.asString(false), evalueProduct.toDouble());
        pathEvalueProduct->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        QByteArray pathSequence = queryPath->getPath().getPathSequence();
        QString pathStart;
        if (pathSequence.length() <= 8)
            pathStart = pathSequence;
        else
            pathStart = pathSequence.left(8) + "...";

        auto * sequenceCopy = new QTableWidgetItem(pathStart);
        auto * sequenceCopyButton = new QueryPathSequenceCopyButton(pathSequence, pathStart);

        ui->tableWidget->setItem(i, 0, pathString);
        ui->tableWidget->setItem(i, 1, pathLength);
        ui->tableWidget->setItem(i, 2, pathQueryCoveragePath);
        ui->tableWidget->setItem(i, 3, pathQueryCoverageHits);
        ui->tableWidget->setItem(i, 4, pathPercIdentity);
        ui->tableWidget->setItem(i, 5, pathMismatches);
        ui->tableWidget->setItem(i, 6, pathGapOpens);
        ui->tableWidget->setItem(i, 7, pathRelativeLength);
        ui->tableWidget->setItem(i, 8, pathLengthDisc);
        ui->tableWidget->setItem(i, 9, pathEvalueProduct);
        ui->tableWidget->setItem(i, 10, sequenceCopy);
        ui->tableWidget->setCellWidget(i, 10, sequenceCopyButton);
    }

    ui->tableWidget->resizeColumns();
    ui->tableWidget->setSortingEnabled(true);
}

QueryPathsDialog::~QueryPathsDialog()
{
    delete ui;
    g_memory->queryPathDialogIsVisible = false;
}


void QueryPathsDialog::hidden()
{
    g_memory->queryPathDialogIsVisible = false;
    emit selectionChanged();
}


void QueryPathsDialog::tableSelectionChanged()
{
    QList<QTableWidgetSelectionRange> selection = ui->tableWidget->selectedRanges();
    int totalSelectedRows = 0;
    for (auto & i : selection)
        totalSelectedRows += i.rowCount();

    g_memory->queryPaths.clear();

    QList<int> selectedRows;
    for (auto & i : selection)
    {
        QTableWidgetSelectionRange * selectionRange = &i;
        int top = selectionRange->topRow();
        int bottom = selectionRange->bottomRow();

        for (int row = top; row <= bottom; ++row)
        {
            if (!selectedRows.contains(row))
                selectedRows.push_back(row);
        }
    }

    for (int row : selectedRows)
    {
        QString pathString = ui->tableWidget->item(row, 0)->text();
        QString pathStringFailure;
        g_memory->queryPaths.push_back(Path::makeFromString(pathString, *g_assemblyGraph,
                                                            false, &pathStringFailure));
    }

    emit selectionChanged();
}
