#ifndef BANDAGE_UI_ANNOTATIONSMANAGER_HPP_
#define BANDAGE_UI_ANNOTATIONSMANAGER_HPP_

#include <QObject>
#include "graph/annotation.hpp"


struct AnnotationGroup {
    using AnnotationVector = std::vector<std::unique_ptr<IAnnotation>>;
    using AnnotationMap = std::map<const DeBruijnNode *, AnnotationVector>;

    const AnnotationGroupId id;
    const QString name;
    AnnotationMap annotationMap;

    const AnnotationVector &getAnnotations(const DeBruijnNode *node) const {
        return getFromMapOrDefaultConstructed(annotationMap, node);
    }
};

class AnnotationsManager : public QObject {
Q_OBJECT

public:
    explicit AnnotationsManager(QObject *parent = nullptr) : QObject(parent) {}

    //todo: replace with map???
    using AnnotationGroupVector = std::vector<std::shared_ptr<AnnotationGroup>>;

    // todo replace with ref + store unique_ptr
    std::shared_ptr<AnnotationGroup> createAnnotationGroup(QString name);
    const AnnotationGroupVector &getGroups() const;
    void removeGroupByName(const QString &name);
    const AnnotationGroup &findGroupByName(const QString &name) const;

public:
signals:
    void annotationGroupsUpdated();

private:
    AnnotationGroupVector m_annotationGroups;
    AnnotationGroupId nextFreeId = 0;
};

#endif //BANDAGE_UI_ANNOTATIONSMANAGER_HPP_
