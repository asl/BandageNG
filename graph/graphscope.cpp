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

#include "graphscope.h"

#include "blast/queries.h"

#include "program/settings.h"

#include "assemblygraph.h"
#include <vector>

namespace graph {
    std::vector<DeBruijnNode *>
    getStartingNodes(QString *errorTitle, QString *errorMessage,
                     const AssemblyGraph &graph, const Scope &graphScope) {
        switch (graphScope.graphScope()) {
            default:
                return {};
            case AROUND_NODE: {
                if (AssemblyGraph::checkIfStringHasNodes(graphScope.nodeList())) {
                    *errorTitle = "No starting nodes";
                    *errorMessage = "Please enter at least one node when drawing the graph using the 'Around node(s)' scope. "
                                    "Separate multiple nodes with commas.";
                    return {};
                }

                // Make sure the nodes the user typed in are actually in the graph.
                std::vector<QString> nodesNotInGraph;
                std::vector<DeBruijnNode *> nodesInGraph = graph.getNodesFromString(graphScope.nodeList(),
                                                                                    g_settings->startingNodesExactMatch,
                                                                                    &nodesNotInGraph);
                if (!nodesNotInGraph.empty()) {
                    *errorTitle = "Nodes not found";
                    *errorMessage = AssemblyGraph::generateNodesNotFoundErrorMessage(nodesNotInGraph,
                                                                                     g_settings->startingNodesExactMatch);
                }

                return nodesInGraph;
            }
            case AROUND_PATHS: {
                auto pathIt = graph.m_deBruijnGraphPaths.find(graphScope.path().toStdString());
                if (pathIt == graph.m_deBruijnGraphPaths.end()) {
                    *errorTitle = "Invalid path";
                    *errorMessage = "No path with such name is loaded";
                    return {};
                }

                return (*pathIt)->nodes();
            }
            case AROUND_BLAST_HITS: {
                std::vector<DeBruijnNode *> startingNodes = graphScope.queries().getNodesFromHits(graphScope.queryName());
                if (startingNodes.empty()) {
                    *errorTitle = "No BLAST hits";
                    *errorMessage = "To draw the graph around BLAST hits, you must first conduct a BLAST search.";
                }
                return startingNodes;
            }
            case DEPTH_RANGE: {
                if (graphScope.minDepth() > graphScope.maxDepth()) {
                    *errorTitle = "Invalid depth range";
                    *errorMessage = "The maximum depth must be greater than or equal to the minimum depth.";
                    return {};
                }

                auto startingNodes = graph.getNodesInDepthRange(graphScope.minDepth(), graphScope.maxDepth());
                if (startingNodes.empty()) {
                    *errorTitle = "No nodes in range";
                    *errorMessage = "There are no nodes with depths in the specified range.";
                }
                return startingNodes;
            }
        }

        return {};
    }

    Scope scope(GraphScope graphScope, const QString &nodesList,
                double minDepthRange, double maxDepthRange,
                const search::Queries &blastQueries, const QString &blastQueryName,
                const QString &pathName, unsigned distance) {
        switch (graphScope) {
            case WHOLE_GRAPH:
                return Scope::wholeGraph();
            case AROUND_NODE:
                return Scope::aroundNodes(nodesList, distance);
            case AROUND_PATHS:
                return Scope::aroundPath(pathName, distance);
            case AROUND_BLAST_HITS:
                return Scope::aroundHits(blastQueries, blastQueryName, distance);
            case DEPTH_RANGE:
                return Scope::depthRange(minDepthRange, maxDepthRange);
        }

        assert(0 && "Invalid scope!");
        return Scope::wholeGraph();
    }

    Scope Scope::aroundHits(const search::Queries &queries, const QString& queryName,
                            unsigned distance) {
        Scope res;
        res.m_scope = AROUND_BLAST_HITS;
        res.m_opt = std::make_pair(std::ref(queries), queryName);
        res.m_distance = distance;

        return res;
    }
}
