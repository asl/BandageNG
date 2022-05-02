#include "annotationsmanager.hpp"

AnnotationGroup &AnnotationsManager::createAnnotationGroup(QString name) {
    m_annotationGroups.emplace_back(
            std::make_unique<AnnotationGroup>(AnnotationGroup{nextFreeId, std::move(name), {}}));
    nextFreeId++;
    emit annotationGroupsUpdated();
    return *m_annotationGroups.back();
}


const AnnotationsManager::AnnotationGroupVector &AnnotationsManager::getGroups() const {
    return m_annotationGroups;
}


void AnnotationsManager::removeGroupByName(const QString &name) {
    auto newEnd = std::remove_if(
            m_annotationGroups.begin(),
            m_annotationGroups.end(),
            [&name](const std::shared_ptr<AnnotationGroup> &group) {
                return group->name == name;
            });
    m_annotationGroups.erase(newEnd, m_annotationGroups.end());
}


const AnnotationGroup &AnnotationsManager::findGroupByName(const QString &name) const {
    return **std::find_if(
            m_annotationGroups.begin(),
            m_annotationGroups.end(),
            [&name](const std::shared_ptr<AnnotationGroup> &group) {
                return group->name == name;
            });
}
