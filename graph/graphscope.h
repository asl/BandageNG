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
class BlastQueries;

enum GraphScope {
    WHOLE_GRAPH,
    AROUND_NODE,
    AROUND_PATHS,
    AROUND_BLAST_HITS,
    DEPTH_RANGE
};

namespace graph {
    class Scope {
        GraphScope m_scope;
        std::variant<
                std::nullptr_t, // whole graph
                QString, // path or node
                std::pair<std::reference_wrapper<const BlastQueries>, QString>, // hits
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

        const BlastQueries &queries() const {
            return std::get<std::pair<std::reference_wrapper<const BlastQueries>, QString>>(m_opt).first.get();
        };

        QString queryName() const {
            return std::get<std::pair<std::reference_wrapper<const BlastQueries>, QString>>(m_opt).second;
        };

        QString nodeList() const {
            return std::get<QString>(m_opt);
        }

        QString path() const {
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

        static Scope depthRange(double min, double max) {
            Scope res;
            res.m_scope = DEPTH_RANGE;
            res.m_opt = std::make_pair(min, max);

            return res;
        }

        static Scope aroundHits(const BlastQueries &queries, const QString& queryName,
                                unsigned distance = 0);

    private:
        Scope()
                : m_scope(WHOLE_GRAPH), m_opt{nullptr}, m_distance(0) {}
    };

    Scope scope(GraphScope graphScope,
                const QString& nodesList,
                const BlastQueries &blastQueries, const QString& blastQueryName,
                const QString& pathName, unsigned distance = 0);

    std::vector<DeBruijnNode *>
    getStartingNodes(QString * errorTitle, QString * errorMessage,
                     const AssemblyGraph &graph, const Scope &graphScope = Scope::wholeGraph());
}