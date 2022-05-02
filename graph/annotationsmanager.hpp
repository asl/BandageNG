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
    std::shared_ptr<AnnotationGroup> createAnnotationGroup(QString name) {
        auto res = std::make_shared<AnnotationGroup>(AnnotationGroup{nextFreeId, std::move(name), {}});
        m_annotationGroups.emplace_back(res);
        nextFreeId++;
        emit annotationGroupsUpdated();
        return res;
    }

    const AnnotationGroupVector &getGroups() const {
        return m_annotationGroups;
    }

    void removeGroupByName(const QString &name) {
        auto newEnd = std::remove_if(
                m_annotationGroups.begin(),
                m_annotationGroups.end(),
                [&name](const std::shared_ptr<AnnotationGroup> &group) {
                    return group->name == name;
                });
        m_annotationGroups.erase(newEnd, m_annotationGroups.end());
    }

    const AnnotationGroup &findGroupByName(const QString &name) const {
        return **std::find_if(
                m_annotationGroups.begin(),
                m_annotationGroups.end(),
                [&name](const std::shared_ptr<AnnotationGroup> &group) {
                    return group->name == name;
                });
    }

public:
signals:
    void annotationGroupsUpdated();

private:
    AnnotationGroupVector m_annotationGroups;
    AnnotationGroupId nextFreeId = 0;
};

#endif //BANDAGE_UI_ANNOTATIONSMANAGER_HPP_
