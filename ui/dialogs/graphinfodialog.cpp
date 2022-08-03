#include "graphinfodialog.h"
#include "ui_graphinfodialog.h"

#include "program/globals.h"
#include "graph/assemblygraph.h"
#include <QPair>

GraphInfoDialog::GraphInfoDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GraphInfoDialog)
{
    ui->setupUi(this);

    setLabels();
}


GraphInfoDialog::~GraphInfoDialog()
{
    delete ui;
}



void GraphInfoDialog::setLabels()
{
    ui->filenameLabel->setText(g_assemblyGraph->m_filename);

    int nodeCount = g_assemblyGraph->m_nodeCount;

    ui->nodeCountLabel->setText(formatIntForDisplay(nodeCount));
    ui->edgeCountLabel->setText(formatIntForDisplay(g_assemblyGraph->m_edgeCount));

    if (g_assemblyGraph->m_edgeCount == 0)
        ui->edgeOverlapRangeLabel->setText("n/a");
    else
    {
        QPair<int, int> overlapRange = g_assemblyGraph->getOverlapRange();
        int smallestOverlap = overlapRange.first;
        int largestOverlap = overlapRange.second;
        if (smallestOverlap == largestOverlap)
            ui->edgeOverlapRangeLabel->setText(formatIntForDisplay(smallestOverlap) + " bp");
        else
            ui->edgeOverlapRangeLabel->setText(formatIntForDisplay(smallestOverlap) + " to " + formatIntForDisplay(largestOverlap) + " bp");
    }

    ui->totalLengthLabel->setText(formatIntForDisplay(g_assemblyGraph->m_totalLength) + " bp");
    ui->totalLengthNoOverlapsLabel->setText(formatIntForDisplay(g_assemblyGraph->getTotalLengthMinusEdgeOverlaps()) + " bp");

    int deadEnds = g_assemblyGraph->getDeadEndCount();
    double percentageDeadEnds = 100.0 * double(deadEnds) / (2 * nodeCount);

    ui->deadEndsLabel->setText(formatIntForDisplay(deadEnds));
    ui->percentageDeadEndsLabel->setText(formatDoubleForDisplay(percentageDeadEnds, 2) + "%");


    int componentCount = 0;
    int largestComponentLength = 0;
    g_assemblyGraph->getGraphComponentCountAndLargestComponentSize(&componentCount, &largestComponentLength);
    QString percentageLargestComponent;
    if (g_assemblyGraph->m_totalLength > 0)
        percentageLargestComponent = formatDoubleForDisplay(100.0 * double(largestComponentLength) / g_assemblyGraph->m_totalLength, 2);
    else
        percentageLargestComponent = "n/a";

    long long totalLengthOrphanedNodes = g_assemblyGraph->getTotalLengthOrphanedNodes();
    QString percentageOrphaned;
    if (g_assemblyGraph->m_totalLength > 0)
        percentageOrphaned = formatDoubleForDisplay(100.0 * double(totalLengthOrphanedNodes) / g_assemblyGraph->m_totalLength, 2);
    else
        percentageOrphaned = "n/a";

    ui->connectedComponentsLabel->setText(formatIntForDisplay(componentCount));
    ui->largestComponentLabel->setText(formatIntForDisplay(largestComponentLength) + " bp (" + percentageLargestComponent + "%)");
    ui->orphanedLengthLabel->setText(formatIntForDisplay(totalLengthOrphanedNodes) + " bp (" + percentageOrphaned + "%)");

    int n50 = 0;
    int shortestNode = 0;
    int firstQuartile = 0;
    int median = 0;
    int thirdQuartile = 0;
    int longestNode = 0;
    g_assemblyGraph->getNodeStats(&n50, &shortestNode, &firstQuartile, &median, &thirdQuartile, &longestNode);

    ui->n50Label->setText(formatIntForDisplay(n50) + " bp");
    ui->shortestNodeLabel->setText(formatIntForDisplay(shortestNode) + " bp");
    ui->lowerQuartileNodeLabel->setText(formatIntForDisplay(firstQuartile) + " bp");
    ui->medianNodeLabel->setText(formatIntForDisplay(median) + " bp");
    ui->upperQuartileNodeLabel->setText(formatIntForDisplay(thirdQuartile) + " bp");
    ui->longestNodeLabel->setText(formatIntForDisplay(longestNode) + " bp");

    double medianDepthByBase = g_assemblyGraph->getMedianDepthByBase();
    long long estimatedSequenceLength = g_assemblyGraph->getEstimatedSequenceLength(medianDepthByBase);

    ui->medianDepthLabel->setText(formatDepthForDisplay(medianDepthByBase));
    if (medianDepthByBase == 0.0)
        ui->estimatedSequenceLengthLabel->setText("unavailable");
    else
        ui->estimatedSequenceLengthLabel->setText(formatIntForDisplay(estimatedSequenceLength) + " bp");
}
