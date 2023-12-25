// Copyright 2022 Anton Korobeynikov
// Copyright 2022 Andrei Zakharov

// This file is part of Bandage-NG

// Bandage-NG is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage-NG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "annotation.h"
#include <QObject>
#include <unordered_map>
#include <vector>
#include <utility>

class DeBruijnNode;
struct AnnotationSetting;
namespace search {
    class Query;
    class Queries;
}

// Often used function for access const reference to value in map even if it doesn't exist
// FIXME: Make it work with MSVC
#if 0
template <typename K, typename V, template<typename, typename, typename...> typename MapLike>
const V &getFromMapOrDefaultConstructed(const MapLike<K, V> &map, const K &key) {
    auto it = map.find(key);
    if (it != map.end()) {
        return it->second;
    } else {
        static const V defaultConstructed{};
        return defaultConstructed;
    }
}
#endif

struct AnnotationGroup {
    using AnnotationVector = std::vector<std::unique_ptr<Annotation>>;
    using AnnotationMap = std::unordered_map<const DeBruijnNode *, AnnotationVector>;

    const AnnotationGroupId id;
    const QString name;
    AnnotationMap annotationMap;

    const AnnotationVector &getAnnotations(const DeBruijnNode *node) const {
            auto it = annotationMap.find(node);
            if (it != annotationMap.end())
                return it->second;

            static const AnnotationVector defaultConstructed{};
            return defaultConstructed;
    }
};

class AnnotationsManager : public QObject {
Q_OBJECT

public:
    explicit AnnotationsManager(QObject *parent = nullptr) : QObject(parent) {}

    using AnnotationGroupVector = std::vector<std::unique_ptr<AnnotationGroup>>;

    AnnotationGroup &createAnnotationGroup(QString name);
    AnnotationGroup &createAnnotationGroup(QString name, const AnnotationSetting &setting);

    const AnnotationGroupVector &getGroups() const;
    void removeGroupByName(const QString &name);
    const AnnotationGroup *findGroupByName(const QString &name) const;
    const AnnotationGroup *findGroupById(AnnotationGroupId id) const;
    void updateGroupFromHits(const QString &name, const std::vector<search::Query*> &queries);

public:
signals:
    void annotationGroupsUpdated();

private:
    AnnotationGroupVector m_annotationGroups;
    AnnotationGroupId nextFreeId = 0;
};
