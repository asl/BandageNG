// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

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
        GraphScope scope;
        std::variant<
                std::nullptr_t, // whole graph
                QString, // path or node
                std::pair<std::reference_wrapper<const BlastQueries>, QString>, // hits
                std::pair<double, double> // depth
        > opt;
    public:
        GraphScope graphScope() const { return scope; }

        double minDepth() const {
            return std::get<std::pair<double, double>>(opt).first;
        }

        double maxDepth() const {
            return std::get<std::pair<double, double>>(opt).second;
        }

        const BlastQueries &queries() const {
            return std::get<std::pair<std::reference_wrapper<const BlastQueries>, QString>>(opt).first.get();
        };

        QString queryName() const {
            return std::get<std::pair<std::reference_wrapper<const BlastQueries>, QString>>(opt).second;
        };

        QString nodeList() const {
            return std::get<QString>(opt);
        }

        QString path() const {
            return std::get<QString>(opt);
        }

        static Scope wholeGraph() {
            return {};
        }

        static Scope aroundNodes(QString nodeList) {
            Scope res;
            res.scope = AROUND_NODE;
            res.opt = nodeList;

            return res;
        }

        static Scope aroundPath(QString pathList) {
            Scope res;
            res.scope = AROUND_PATHS;
            res.opt = pathList;

            return res;
        }

        static Scope depthRange(double min, double max) {
            Scope res;
            res.scope = DEPTH_RANGE;
            res.opt = std::make_pair(min, max);

            return res;
        }

        static Scope aroundHits(const BlastQueries &queries, QString queryName);

    private:
        Scope()
                : scope(WHOLE_GRAPH), opt{nullptr} {}
    };

    Scope scope(GraphScope graphScope,
                const QString& nodesList,
                const BlastQueries &blastQueries, const QString& blastQueryName,
                const QString& pathName);

    std::vector<DeBruijnNode *>
    getStartingNodes(QString * errorTitle, QString * errorMessage,
                     const AssemblyGraph &graph, const Scope &graphScope = Scope::wholeGraph());
}