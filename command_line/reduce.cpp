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


#include "reduce.h"
#include "commoncommandlinefunctions.h"

#include "graph/assemblygraph.h"
#include "graph/gfawriter.h"
#include "graphsearch/blast/blastsearch.h"

#include "program/globals.h"
#include "program/settings.h"

#include <CLI/CLI.hpp>
#include <vector>

CLI::App *addReduceSubcommand(CLI::App &app, ReduceCmd &cmd) {
    auto *reduce = app.add_subcommand("reduce", "Save a subgraph of a larger graph");
    reduce->add_option("<inputgraph>", cmd.m_graph, "A graph file of any type supported by Bandage")
            ->required()->check(CLI::ExistingFile);
    reduce->add_option("<outputgraph>", cmd.m_out, "The filename for the GFA graph to be made (if it does not end in '.gfa', that extension will be added)")
            ->required();

    reduce->footer("Bandage reduce takes an input graph and saves a reduced subgraph using the graph scope settings. The saved graph will be in GFA format.\n"
                   "If a graph scope is not specified, then the 'entire' scope will be used, in which case this will simply convert the input graph to GFA format.");

    return reduce;
}

int handleReduceCmd(QApplication *app,
                    const CLI::App &cli, const ReduceCmd &cmd) {
    QTextStream out(stdout);
    QTextStream err(stderr);

    QString outputFilename = cmd.m_out.c_str();
    if (!outputFilename.endsWith(".gfa"))
        outputFilename += ".gfa";

    if (!g_assemblyGraph->loadGraphFromFile(cmd.m_graph.c_str())) {
        outputText(("Bandage-NG error: could not load " + cmd.m_graph.native()).c_str(), &err);
        return 1;
    }

    if (cli.count("--query")) {
        if (!g_blastSearch->ready()) {
            err << g_blastSearch->lastError() << Qt::endl;
            return 1;
        }

        QString blastError = g_blastSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                              g_settings->blastQueryFilename,
                                                              false, /* include paths */
                                                              g_settings->blastSearchParameters);
        if (!blastError.isEmpty()) {
            err << blastError << Qt::endl;
            return 1;
        }
    }

    QString errorTitle;
    QString errorMessage;
    auto scope = graph::scope(g_settings->graphScope,
                              g_settings->startingNodes,
                              g_settings->minDepthRange, g_settings->maxDepthRange,
                              &g_blastSearch->queries(), "all",
                              "", g_settings->nodeDistance);
    auto startingNodes = graph::getStartingNodes(&errorTitle, &errorMessage,
                                                 *g_assemblyGraph, scope);
    if (!errorMessage.isEmpty()) {
        err << errorMessage << Qt::endl;
        return 1;
    }

    g_assemblyGraph->markNodesToDraw(scope, startingNodes);

    if (!gfa::saveVisibleGraph(outputFilename, *g_assemblyGraph)) {
        err << "Bandage was unable to save the graph file." << Qt::endl;
        return 1;
    }

    return 0;
}
