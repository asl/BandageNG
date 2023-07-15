// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your m_option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <QString>
#include <vector>
#include <variant>

class DeBruijnNode;
class AssemblyGraph;
namespace search {
    class Queries;
}

enum GraphScope {
    WHOLE_GRAPH,
    AROUND_NODE,
    AROUND_PATHS,
    AROUND_WALKS,
    AROUND_BLAST_HITS,
    DEPTH_RANGE
};

namespace graph {
    class Scope {
        GraphScope m_scope;
        std::variant<
                std::nullptr_t, // whole graph
                QString, // path or node
                std::pair<const search::Queries*, QString>, // hits
                std::pair<double, double> // depth
        > m_opt;
        unsigned m_distance;
    public:
        GraphScope graphScope() const { return m_scope; }

        unsigned distance() const { return m_distance; }

        double minDepth() const {
            return std::get<std::pair<double, double>>(m_opt).first;
        }

        double maxDepth() const {
            return std::get<std::pair<double, double>>(m_opt).second;
        }

        const search::Queries *queries() const {
            return std::get<std::pair<const search::Queries*, QString>>(m_opt).first;
        };

        QString queryName() const {
            return std::get<std::pair<const search::Queries*, QString>>(m_opt).second;
        };

        QString nodeList() const {
            return std::get<QString>(m_opt);
        }

        QString path() const {
            return std::get<QString>(m_opt);
        }

        QString walk() const {
            return std::get<QString>(m_opt);
        }

        static Scope wholeGraph() {
            return {};
        }

        static Scope aroundNodes(QString nodeList, unsigned distance = 0) {
            Scope res;
            res.m_scope = AROUND_NODE;
            res.m_opt = nodeList;
            res.m_distance = distance;

            return res;
        }

        static Scope aroundPath(QString pathList, unsigned distance = 0) {
            Scope res;
            res.m_scope = AROUND_PATHS;
            res.m_opt = pathList;
            res.m_distance = distance;

            return res;
        }

        static Scope aroundWalk(QString pathList, unsigned distance = 0) {
            Scope res;
            res.m_scope = AROUND_WALKS;
            res.m_opt = pathList;
            res.m_distance = distance;

            return res;
        }

        static Scope depthRange(double min, double max) {
            Scope res;
            res.m_scope = DEPTH_RANGE;
            res.m_opt = std::make_pair(min, max);

            return res;
        }

        static Scope aroundHits(const search::Queries *queries, const QString& queryName,
                                unsigned distance = 0);

        static Scope aroundHits(const search::Queries &queries, const QString& queryName,
                                unsigned distance = 0);

    private:
        Scope()
                : m_scope(WHOLE_GRAPH), m_opt{nullptr}, m_distance(0) {}
    };

    // All-in-one function intended to be used in UIs and such
    Scope scope(GraphScope graphScope,
                const QString &nodesList,
                double minDepthRange, double maxDepthRange,
                const search::Queries *queries, const QString& blastQueryName,
                const QString &pathName, unsigned distance = 0);

    std::vector<DeBruijnNode *>
    getStartingNodes(QString *errorTitle, QString *errorMessage,
                     const AssemblyGraph &graph, const Scope &graphScope = Scope::wholeGraph());
}
