#include "annotationswidget.h"

#include <QLabel>

#include "mygraphicsview.h"


struct ItemChangedCallback {
    bool isText;

    void operator()(QListWidgetItem *item) const {
        auto groupId = item->data(Qt::UserRole).value<AnnotationGroupId>();
        auto &setting = g_settings->annotationsSettings[groupId];
        (isText ? setting.showText : setting.showLine) = (item->checkState() == Qt::Checked);
        g_graphicsView->viewport()->update();
    }
};


AnnotationsWidget::AnnotationsWidget(QWidget *parent) : QWidget(parent) {
    m_vBoxLayout = new QVBoxLayout(this);
    m_annotationsDrawList = new QListWidget(this);
    m_annotationsTextList = new QListWidget(this);

    auto *labelDraw = new QLabel("Annotations");
    auto *labelText = new QLabel("Annotations (Text)");
    m_vBoxLayout->addWidget(labelDraw);
    m_vBoxLayout->addWidget(m_annotationsDrawList);
    m_vBoxLayout->addWidget(labelText);
    m_vBoxLayout->addWidget(m_annotationsTextList);

    connect(g_annotationsManager.get(), SIGNAL(annotationGroupsUpdated()), this, SLOT(updateAnnotationGroups()));
    connect(m_annotationsDrawList, &QListWidget::itemChanged, ItemChangedCallback{false});
    connect(m_annotationsTextList, &QListWidget::itemChanged, ItemChangedCallback{true});

    setLayout(m_vBoxLayout);
}


void AnnotationsWidget::updateAnnotationGroups() {
    m_annotationsDrawList->clear();
    m_annotationsTextList->clear();
    for (const auto &annotationGroup : g_annotationsManager->getGroups()) {
        auto &setting = g_settings->annotationsSettings[annotationGroup->id];
        addItemToList(m_annotationsDrawList, *annotationGroup, setting.showLine);
        addItemToList(m_annotationsTextList, *annotationGroup, setting.showText);
    }
}


void AnnotationsWidget::addItemToList(QListWidget *listWidget, const AnnotationGroup &annotationGroup, bool checked) {
    auto *checkBoxItem = new QListWidgetItem(annotationGroup.name);
    checkBoxItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    checkBoxItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    checkBoxItem->setData(Qt::UserRole, annotationGroup.id);
    listWidget->addItem(checkBoxItem);
}
