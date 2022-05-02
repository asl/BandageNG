//
// Created by wafemand on 02/05/2022.
//

#ifndef BANDAGE_ANNOTATIONSWIDGET_H
#define BANDAGE_ANNOTATIONSWIDGET_H


#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>

#include "graph/annotationsmanager.hpp"
#include "mygraphicsview.h"

class AnnotationsWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnnotationsWidget(QWidget *parent) : QWidget(parent) {
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

public slots:
    void updateAnnotationGroups() {
        m_annotationsDrawList->clear();
        m_annotationsTextList->clear();
        for (const auto &annotationGroup : g_annotationsManager->getGroups()) {
            auto &setting = g_settings->annotationsSettings[annotationGroup->id];
            addItemToList(m_annotationsDrawList, *annotationGroup, setting.showLine);
            addItemToList(m_annotationsTextList, *annotationGroup, setting.showText);
        }
    }

private:
    struct ItemChangedCallback {
        bool isText;

        void operator()(QListWidgetItem *item) const {
            auto groupId = item->data(Qt::UserRole).value<AnnotationGroupId>();
            auto &setting = g_settings->annotationsSettings[groupId];
            (isText ? setting.showText : setting.showLine) = (item->checkState() == Qt::Checked);
            g_graphicsView->viewport()->update();
        }
    };

    static void addItemToList(QListWidget *listWidget, const AnnotationGroup &annotationGroup, bool checked) {
        auto *checkBoxItem = new QListWidgetItem(annotationGroup.name);
        checkBoxItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkBoxItem->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        checkBoxItem->setData(Qt::UserRole, annotationGroup.id);
        listWidget->addItem(checkBoxItem);
    }

    QVBoxLayout *m_vBoxLayout;
    QListWidget *m_annotationsDrawList;
    QListWidget *m_annotationsTextList;
};


#endif //BANDAGE_ANNOTATIONSWIDGET_H
