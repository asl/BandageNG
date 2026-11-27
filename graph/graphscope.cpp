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

#include "graphsearch/queries.h"

#include "program/settings.h"

#include "assemblygraph.h"
#include "path.h"

#include <unordered_set>
#include <vector>

namespace graph {
    static QStringList splitCommaSeparated(QString text) {
        text = text.simplified();
        QStringList tokens = text.split(',');
        QStringList out;
        for (QString token : tokens) {
            token = token.trimmed();
            if (!token.isEmpty())
                out.push_back(token);
        }
        return out;
    }

    static void appendUnique(std::vector<OrientedNode> &nodes,
                             std::unordered_set<OrientedNode> &seen,
                             const std::vector<OrientedNode> &more) {
        for (OrientedNode node : more) {
            if (seen.insert(node).second)
                nodes.push_back(node);
        }
    }

    static bandage::Expected<std::vector<OrientedNode>>
    textError(std::string title, std::string message) {
        return bandage::make_error<bandage::TextError>(std::move(title), std::move(message));
    }

    bandage::Expected<std::vector<OrientedNode>>
    getStartingNodes(const AssemblyGraph &graph, const Scope &graphScope) {
        switch (graphScope.graphScope()) {
            default:
                return std::vector<OrientedNode>{};
            case AROUND_NODE: {
                if (AssemblyGraph::checkIfStringHasNodes(graphScope.nodeList())) {
                    return textError("No starting nodes",
                                     "Please enter at least one node when drawing the graph using the 'Around node(s)' scope. "
                                     "Separate multiple nodes with commas.");
                }

                // Make sure the nodes the user typed in are actually in the graph.
                std::vector<QString> nodesNotInGraph;
                std::vector<OrientedNode> nodesInGraph =
                        graph.getNodesFromStringList(graphScope.nodeList(),
                                                     g_settings->startingNodesExactMatch,
                                                     &nodesNotInGraph);
                if (!nodesNotInGraph.empty()) {
                    return textError("Nodes not found",
                                     AssemblyGraph::generateNodesNotFoundErrorMessage(
                                             nodesNotInGraph, g_settings->startingNodesExactMatch));
                }

                return nodesInGraph;
            }
            case AROUND_PATHS: {
                auto pathIt = graph.m_deBruijnGraphPaths.find(graphScope.path().toStdString());
                if (pathIt == graph.m_deBruijnGraphPaths.end())
                    return textError("Invalid path", "No path with such name is loaded");

                return pathIt->nodes();
            }
            case AROUND_WALKS: {
                auto pathIt = graph.m_deBruijnGraphWalks.find(graphScope.walk().toStdString());
                if (pathIt == graph.m_deBruijnGraphWalks.end())
                    return textError("Invalid walk", "No walk with such sequence name is loaded");

                return pathIt->walk.nodes();
            }
            case AROUND_COMPONENT: {
                QStringList pathNames = splitCommaSeparated(graphScope.componentPaths());
                QStringList walkNames = splitCommaSeparated(graphScope.componentWalks());
                bool hasNodes = !AssemblyGraph::checkIfStringHasNodes(graphScope.componentNodes());

                if (!hasNodes && pathNames.empty() && walkNames.empty()) {
                    return textError("No starting nodes",
                                     "Please enter at least one node, path or walk when drawing the graph using the "
                                     "'Around connected component' scope. Separate multiple names with commas.");
                }

                std::unordered_set<OrientedNode> seen;
                std::vector<OrientedNode> nodesInGraph;

                if (hasNodes) {
                    std::vector<QString> nodesNotInGraph;
                    auto fromNodes = graph.getNodesFromStringList(graphScope.componentNodes(),
                                                                  g_settings->startingNodesExactMatch,
                                                                  &nodesNotInGraph);
                    if (!nodesNotInGraph.empty()) {
                        return textError("Nodes not found",
                                         AssemblyGraph::generateNodesNotFoundErrorMessage(
                                                 nodesNotInGraph, g_settings->startingNodesExactMatch));
                    }
                    appendUnique(nodesInGraph, seen, fromNodes);
                }

                QStringList missingPaths;
                for (const QString &name : pathNames) {
                    auto pathIt = graph.m_deBruijnGraphPaths.find(name.toStdString());
                    if (pathIt == graph.m_deBruijnGraphPaths.end()) {
                        missingPaths.push_back(name);
                        continue;
                    }
                    appendUnique(nodesInGraph, seen, pathIt->nodes());
                }
                if (!missingPaths.empty()) {
                    return textError("Invalid path",
                                     ("No path with such name is loaded: " + missingPaths.join(", ")).toStdString());
                }

                QStringList missingWalks;
                for (const QString &name : walkNames) {
                    auto walkIt = graph.m_deBruijnGraphWalks.find(name.toStdString());
                    if (walkIt == graph.m_deBruijnGraphWalks.end()) {
                        missingWalks.push_back(name);
                        continue;
                    }
                    appendUnique(nodesInGraph, seen, walkIt->walk.nodes());
                }
                if (!missingWalks.empty()) {
                    return textError("Invalid walk",
                                     ("No walk with such sequence name is loaded: " + missingWalks.join(", ")).toStdString());
                }

                if (nodesInGraph.empty()) {
                    return textError("No starting nodes",
                                     "The specified nodes, paths and walks did not match anything in the graph.");
                }

                return nodesInGraph;
            }
            case AROUND_QUERY_HITS: {
                std::vector<OrientedNode> startingNodes;
                if (const auto *queries = graphScope.queries())
                    startingNodes = queries->getNodesFromHits(graphScope.queryName());
                if (startingNodes.empty()) {
                    return textError("No hits",
                                     "To draw the graph around graph hits, you must first conduct a graph search.");
                }
                return startingNodes;
            }
            case DEPTH_RANGE: {
                if (graphScope.minDepth() > graphScope.maxDepth()) {
                    return textError("Invalid depth range",
                                     "The maximum depth must be greater than or equal to the minimum depth.");
                }

                auto startingNodes = graph.getNodesInDepthRange(graphScope.minDepth(), graphScope.maxDepth());
                if (startingNodes.empty()) {
                    return textError("No nodes in range",
                                     "There are no nodes with depths in the specified range.");
                }
                return startingNodes;
            }
        }

        return std::vector<OrientedNode>{};
    }

    Scope scope(GraphScope graphScope, const QString &nodesList,
                double minDepthRange, double maxDepthRange,
                const search::Queries *queries, const QString &queryName,
                const QString &pathName, unsigned distance,
                const QString &walkName) {
        switch (graphScope) {
            case WHOLE_GRAPH:
                return Scope::wholeGraph();
            case AROUND_NODE:
                return Scope::aroundNodes(nodesList, distance);
            case AROUND_PATHS:
                return Scope::aroundPath(pathName, distance);
            case AROUND_WALKS:
                return Scope::aroundWalk(walkName.isEmpty() ? pathName : walkName, distance);
            case AROUND_QUERY_HITS:
                return Scope::aroundHits(queries, queryName, distance);
            case DEPTH_RANGE:
                return Scope::depthRange(minDepthRange, maxDepthRange);
            case AROUND_COMPONENT:
                return Scope::aroundComponent(nodesList, pathName, walkName);
        }

        assert(0 && "Invalid scope!");
        return Scope::wholeGraph();
    }

    Scope Scope::aroundHits(const search::Queries &queries, const QString& queryName,
                            unsigned distance) {
        return aroundHits(&queries, queryName, distance);
    }

    Scope Scope::aroundHits(const search::Queries *queries, const QString& queryName,
                            unsigned distance) {
        Scope res;
        res.m_scope = AROUND_QUERY_HITS;
        res.m_opt = std::make_pair(queries, queryName);
        res.m_distance = distance;

        return res;
    }

    Scope Scope::aroundComponent(QString nodes, QString paths, QString walks) {
        Scope res;
        res.m_scope = AROUND_COMPONENT;
        res.m_opt = ComponentSeed{std::move(nodes), std::move(paths), std::move(walks)};
        return res;
    }
}
