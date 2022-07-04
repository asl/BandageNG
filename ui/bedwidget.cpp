#include "bedwidget.h"

#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <exception>

#include "program/memory.h"
#include "io/bedloader.hpp"
#include "graph/annotationsmanager.h"
#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"

inline constexpr double BED_MAIN_WIDTH = 1;
inline constexpr double BED_THICK_WIDTH = 1.3;
inline constexpr double BED_BLOCK_WIDTH = 1.6;


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
                auto it = g_assemblyGraph->m_deBruijnGraphNodes.find(nodeName.toStdString());
                if (it != g_assemblyGraph->m_deBruijnGraphNodes.end()) {
                    DeBruijnNode *node = it.value();
                    if (bedLine.strand == bed::Strand::REVERSE_COMPLEMENT)
                        node = node->getReverseComplement();
                    auto &annotation = annotationGroup.annotationMap[node].emplace_back(
                        std::make_unique<Annotation>(bedLine.chromStart, bedLine.chromEnd, bedLine.name));
                    annotation->addView(std::make_unique<SolidView>(BED_MAIN_WIDTH, bedLine.itemRgb.toQColor()));
                    annotation->addView(std::make_unique<BedThickView>(BED_THICK_WIDTH, bedLine.itemRgb.toQColor(), bedLine.thickStart, bedLine.thickEnd));
                    annotation->addView(std::make_unique<BedBlockView>(BED_BLOCK_WIDTH, bedLine.itemRgb.toQColor(), bedLine.blocks));
                }
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
