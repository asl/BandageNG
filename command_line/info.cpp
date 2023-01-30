// Copyright 2023 Anton Korobeynikov

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

#include "info.h"

#include "commoncommandlinefunctions.h"
#include "graph/assemblygraph.h"
#include "program/settings.h"

#include <CLI/CLI.hpp>

CLI::App *addInfoSubcommand(CLI::App &app, InfoCmd &cmd) {
    auto *info = app.add_subcommand("info", "Display information about a graph");
    info->add_option("<graph>", cmd.m_graph, "A graph file of any type supported by Bandage")
            ->required()->check(CLI::ExistingFile);
    info->add_flag("--tsv", cmd.m_tsv, "Output the information in a single tab-delimited line starting with the graph file");

    info->footer(
        "Bandage info takes a graph file as input and outputs (to stdout) the following statistics about the graph:\n"
        "  * Node count: The number of nodes in the graph. Only positive nodes are counted (i.e. each complementary pair counts as one).\n"
        "  * Edge count: The number of edges in the graph. Only one edge in each complementary pair is counted.\n"
        "  * Smallest edge overlap: The smallest overlap size (in bp) for the edges in the graph.\n"
        "  * Largest edge overlap: The smallest overlap size (in bp) for the edges in the graph. For most graphs this will be the same as the smallest edge overlap (i.e. all edges have the same overlap).\n"
        "  * Total length: The total number of base pairs in the graph.\n"
        "  * Total length no overlaps: The total number of base pairs in the graph, subtracting bases that are duplicated in edge overlaps.\n"
        "  * Dead ends: The number of instances where an end of a node does not connect to any other nodes.\n"
        "  * Percentage dead ends: The proportion of possible dead ends. The maximum number of dead ends is twice the number of nodes (occurs when there are no edges), so this value is the number of dead ends divided by twice the node count.\n"
        "  * Connected components: The number of regions of the graph which are disconnected from each other.\n"
        "  * Largest component: The total number of base pairs in the largest connected component.\n"
        "  * Total length orphaned nodes: The total number of base pairs in orphan nodes (nodes with no edges).\n"
        "  * N50: Nodes that are this length or greater will collectively add up to at least half of the total length.\n"
        "  * Shortest node: The length of the shortest node in the graph.\n"
        "  * Lower quartile node: The median node length for the shorter half of the nodes.\n"
        "  * Median node: The median node length for the graph.\n"
        "  * Upper quartile node: The median node length for the longer half of the nodes.\n"
        "  * Longest node: The length of the longest node in the graph.\n"
        "  * Median depth: The median depth of the graph, by base.\n"
        "  * Estimated sequence length: An estimate of the total number of bases in the original sequence, calculated by multiplying each node's length (minus overlaps) by its depth relative to the median.");

    return info;
}

int handleInfoCmd(QApplication *app,
                  const CLI::App &cli, const InfoCmd &cmd) {
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (!g_assemblyGraph->loadGraphFromFile(cmd.m_graph.c_str())) {
        err << "Bandage-NG error: could not load " << cmd.m_graph.c_str() << Qt::endl;
        return 1;
    }

    int nodeCount = g_assemblyGraph->m_nodeCount;
    int edgeCount = g_assemblyGraph->m_edgeCount;
    QPair<int, int> overlapRange = g_assemblyGraph->getOverlapRange();
    int smallestOverlap = overlapRange.first;
    int largestOverlap = overlapRange.second;
    int totalLength = g_assemblyGraph->m_totalLength;
    int totalLengthNoOverlaps = g_assemblyGraph->getTotalLengthMinusEdgeOverlaps();
    int deadEnds = g_assemblyGraph->getDeadEndCount();
    double percentageDeadEnds = 100.0 * double(deadEnds) / (2 * nodeCount);

    int n50 = 0;
    int shortestNode = 0;
    int firstQuartile = 0;
    int median = 0;
    int thirdQuartile = 0;
    int longestNode = 0;
    g_assemblyGraph->getNodeStats(&n50, &shortestNode, &firstQuartile, &median, &thirdQuartile, &longestNode);

    int componentCount = 0;
    int largestComponentLength = 0;
    g_assemblyGraph->getGraphComponentCountAndLargestComponentSize(&componentCount, &largestComponentLength);
    long long totalLengthOrphanedNodes = g_assemblyGraph->getTotalLengthOrphanedNodes();

    double medianDepthByBase = g_assemblyGraph->getMedianDepthByBase();
    long long estimatedSequenceLength = g_assemblyGraph->getEstimatedSequenceLength(medianDepthByBase);

    if (cmd.m_tsv) {
        out << cmd.m_graph.c_str() << "\t"
            << nodeCount << "\t"
            << edgeCount << "\t"
            << smallestOverlap << "\t"
            << largestOverlap << "\t"
            << totalLength << "\t"
            << totalLengthNoOverlaps << "\t"
            << deadEnds << "\t"
            << percentageDeadEnds << "%\t"
            << componentCount << "\t"
            << largestComponentLength << "\t"
            << totalLengthOrphanedNodes << "\t"
            << n50 << "\t"
            << shortestNode << "\t"
            << firstQuartile << "\t"
            << median << "\t"
            << thirdQuartile << "\t"
            << longestNode << "\t"
            << medianDepthByBase << "\t"
            << estimatedSequenceLength << "\n";
    } else {
        out << "Node count:                       " << nodeCount << "\n"
            << "Edge count:                       " << edgeCount << "\n"
            << "Smallest edge overlap (bp):       " << smallestOverlap << "\n"
            << "Largest edge overlap (bp):        " << largestOverlap << "\n"
            << "Total length (bp):                " << totalLength << "\n"
            << "Total length no overlaps (bp):    " << totalLengthNoOverlaps << "\n"
            << "Dead ends:                        " << deadEnds << "\n"
            << "Percentage dead ends:             " << percentageDeadEnds << "%\n"
            << "Connected components:             " << componentCount << "\n"
            << "Largest component (bp):           " << largestComponentLength << "\n"
            << "Total length orphaned nodes (bp): " << totalLengthOrphanedNodes << "\n"
            << "N50 (bp):                         " << n50 << "\n"
            << "Shortest node (bp):               " << shortestNode << "\n"
            << "Lower quartile node (bp):         " << firstQuartile << "\n"
            << "Median node (bp):                 " << median << "\n"
            << "Upper quartile node (bp):         " << thirdQuartile << "\n"
            << "Longest node (bp):                " << longestNode << "\n"
            << "Median depth:                     " << medianDepthByBase << "\n"
            << "Estimated sequence length (bp):   " << estimatedSequenceLength << "\n";
    }

    return 0;
}
