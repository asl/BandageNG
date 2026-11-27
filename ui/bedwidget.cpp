#include "bedwidget.h"

#include "io/bedloader.h"
#include "graph/annotationsmanager.h"
#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"

#include "program/memory.h"
#include "program/settings.h"

#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <exception>

BedWidget::BedWidget(QWidget *parent) : QWidget(parent) {
    static const QString label = "Load BED file";
    auto *button = new QPushButton(label, this);

    auto *layout = new QVBoxLayout();
    layout->addWidget(button);

    connect(button, &QPushButton::clicked, [this]() {
        g_annotationsManager->removeGroupByName(g_settings->bedAnnotationGroupName);
        QString bedFileName = QFileDialog::getOpenFileName(this, label, g_memory->rememberedPath);
        if (bedFileName.isNull()) return;
        try {
            auto bedLines = bed::load(bedFileName.toStdString());
            auto &annotationGroup = g_annotationsManager->createAnnotationGroup(g_settings->bedAnnotationGroupName);
            for (const auto &bedLine : bedLines) {
                auto nodeName = g_assemblyGraph->getNodeNameFromString(bedLine.chrom.c_str());
                OrientedNode node = g_assemblyGraph->orientedNode(nodeName.toStdString());
                if (!node)
                    continue;
                if (bedLine.strand == bed::Strand::REVERSE_COMPLEMENT)
                    node = node.reverse();
                addBedAnnotation(annotationGroup, node, bedLine);
            }
        } catch (std::exception &err) {
            QString errorTitle = "Error loading BED file";
            QString errorMessage = "There was an error when attempting to load:\n"
                                   + bedFileName + ":\n"
                                   + err.what() + "\n\n"
                                   "Please verify that this file has the correct format.";
            QMessageBox::warning(this, errorTitle, errorMessage);
        }
    });

    setLayout(layout);
}
