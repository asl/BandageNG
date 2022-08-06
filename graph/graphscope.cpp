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

#include "blast/blastsearch.h"

#include "program/settings.h"

#include "assemblygraph.h"
#include <vector>

namespace graph {
    std::vector<DeBruijnNode *>
    getStartingNodes(QString *errorTitle, QString *errorMessage,
                     const AssemblyGraph &graph, GraphScope graphScope,
                     const QString &nodesList,
                     const BlastQueries &blastQueries, const QString &blastQueryName,
                     const QString &pathName) {
        switch (graphScope) {
            default:
                return {};
            case AROUND_NODE: {
                if (AssemblyGraph::checkIfStringHasNodes(nodesList)) {
                    *errorTitle = "No starting nodes";
                    *errorMessage = "Please enter at least one node when drawing the graph using the 'Around node(s)' scope. "
                                    "Separate multiple nodes with commas.";
                    return {};
                }

                // Make sure the nodes the user typed in are actually in the graph.
                std::vector<QString> nodesNotInGraph;
                std::vector<DeBruijnNode *> nodesInGraph = graph.getNodesFromString(nodesList,
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
                auto pathIt = graph.m_deBruijnGraphPaths.find(pathName.toStdString());
                if (pathIt == graph.m_deBruijnGraphPaths.end()) {
                    *errorTitle = "Invalid path";
                    *errorMessage = "No path with such name is loaded";
                    return {};
                }

                return (*pathIt)->nodes();
            }
            case AROUND_BLAST_HITS: {
                std::vector<DeBruijnNode *> startingNodes = blastQueries.getNodesFromHits(blastQueryName);
                if (startingNodes.empty()) {
                    *errorTitle = "No BLAST hits";
                    *errorMessage = "To draw the graph around BLAST hits, you must first conduct a BLAST search.";
                }
                return startingNodes;
            }
            case DEPTH_RANGE: {
                if (g_settings->minDepthRange > g_settings->maxDepthRange) {
                    *errorTitle = "Invalid depth range";
                    *errorMessage = "The maximum depth must be greater than or equal to the minimum depth.";
                    return {};
                }

                auto startingNodes = graph.getNodesInDepthRange(g_settings->minDepthRange, g_settings->maxDepthRange);
                if (startingNodes.empty()) {
                    *errorTitle = "No nodes in range";
                    *errorMessage = "There are no nodes with depths in the specified range.";
                }
                return startingNodes;
            }
        }

        return {};
    }

}