#ifndef BANDAGE_ANNOTATIONSWIDGET_H
#define BANDAGE_ANNOTATIONSWIDGET_H


#include <QWidget>
#include <QVBoxLayout>
#include <QListWidget>

#include "graph/annotationsmanager.hpp"

class AnnotationsWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnnotationsWidget(QWidget *parent);

public slots:
    void updateAnnotationGroups();

private:
    static void addItemToList(QListWidget *listWidget, const AnnotationGroup &annotationGroup, bool checked);

    QVBoxLayout *m_vBoxLayout;
    QListWidget *m_annotationsDrawList;
    QListWidget *m_annotationsTextList;
};


#endif //BANDAGE_ANNOTATIONSWIDGET_H
