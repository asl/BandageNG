#include "annotationswidget.h"

#include <QLabel>

#include "mygraphicsview.h"


AnnotationSettingsDialog::AnnotationSettingsDialog(const AnnotationGroup &annotationGroup, QWidget *parent)
        : QDialog(parent), m_annotationGroupId(annotationGroup.id) {
    auto &annotationSettings = g_settings->annotationsSettings[annotationGroup.id];
    setWindowTitle(annotationGroup.name);
    setWindowFlags(windowFlags() | Qt::Tool);

    auto *formLayout = new QFormLayout;
    auto textCheckBox = new QCheckBox("Text");
    textCheckBox->setCheckState(annotationSettings.showText ? Qt::Checked : Qt::Unchecked);

    connect(textCheckBox, &QCheckBox::stateChanged,
            [this](int newState) {
                g_settings->annotationsSettings[m_annotationGroupId].showText = newState == Qt::Checked;
                g_graphicsView->viewport()->update();
            });

    formLayout->addRow(textCheckBox);
    if (!annotationGroup.annotationMap.empty() && !annotationGroup.annotationMap.begin()->second.empty()) {
        // All annotations in group have the same types of views, so we can get view's names from any annotation
        ViewId i = 0;
        for (const auto &view: annotationGroup.annotationMap.begin()->second.front()->getViews()) {
            auto viewCheckBox = new QCheckBox(view->getTypeName());
            viewCheckBox->setCheckState(annotationSettings.viewsToShow.count(i) != 0 ? Qt::Checked : Qt::Unchecked);
            formLayout->addRow(viewCheckBox);

            connect(viewCheckBox, &QCheckBox::stateChanged,
                    [i, this](int newState) {
                        auto &viewsToShow = g_settings->annotationsSettings[m_annotationGroupId].viewsToShow;
                        switch (newState) {
                            case Qt::Checked:
                                viewsToShow.insert(i);
                                break;
                            case Qt::Unchecked:
                                viewsToShow.erase(i);
                                break;
                            default: break;
                        }
                        g_graphicsView->viewport()->update();
                    });

            i++;
        }
    }

    setLayout(formLayout);
}


AnnotationsListWidget::AnnotationsListWidget(QWidget *parent) : QListWidget(parent) {
    connect(g_annotationsManager.get(), &AnnotationsManager::annotationGroupsUpdated,
            this, &AnnotationsListWidget::updateAnnotationGroups);

    connect(this, &QListWidget::itemDoubleClicked, [this](QListWidgetItem *listWidgetItem) {
        const auto &annotationGroupPtr = g_annotationsManager->findGroupById(
                listWidgetItem->data(Qt::UserRole).value<AnnotationGroupId>());
        auto *settingsDialog = new AnnotationSettingsDialog(annotationGroupPtr, this);
        settingsDialog->open();
    });
}


void AnnotationsListWidget::updateAnnotationGroups() {
    clear();
    for (const auto &annotationGroupPtr: g_annotationsManager->getGroups()) {
        auto &setting = g_settings->annotationsSettings[annotationGroupPtr->id];
        auto *listWidgetItem = new QListWidgetItem(annotationGroupPtr->name);
        addItem(listWidgetItem);
        listWidgetItem->setData(Qt::UserRole, annotationGroupPtr->id);
    }
}
