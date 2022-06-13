#ifndef BANDAGE_UI_ANNOTATIONSMANAGER_HPP_
#define BANDAGE_UI_ANNOTATIONSMANAGER_HPP_

#include <QObject>
#include "graph/annotation.hpp"


struct AnnotationGroup {
    using AnnotationVector = std::vector<std::unique_ptr<Annotation>>;
    using AnnotationMap = std::unordered_map<const DeBruijnNode *, AnnotationVector>;

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

    using AnnotationGroupVector = std::vector<std::unique_ptr<AnnotationGroup>>;

    AnnotationGroup &createAnnotationGroup(QString name);
    const AnnotationGroupVector &getGroups() const;
    void removeGroupByName(const QString &name);
    const AnnotationGroup &findGroupByName(const QString &name) const;
    const AnnotationGroup &findGroupById(AnnotationGroupId id) const;

public:
signals:
    void annotationGroupsUpdated();

private:
    AnnotationGroupVector m_annotationGroups;
    AnnotationGroupId nextFreeId = 0;
};


#endif //BANDAGE_UI_ANNOTATIONSMANAGER_HPP_
