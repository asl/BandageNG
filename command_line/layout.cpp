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


#include "layout.h"
#include "commoncommandlinefunctions.h"

#include "graph/assemblygraph.h"

#include "graphsearch/blast/blastsearch.h"

#include "program/globals.h"
#include "program/settings.h"

#include "layout/graphlayout.h"
#include "layout/graphlayoutworker.h"
#include "layout/io.h"

#include <vector>

#include <CLI/CLI.hpp>

CLI::App *addLayoutSubcommand(CLI::App &app, LayoutCmd &cmd) {
    auto *layout = app.add_subcommand("layout", "Layout the graph");
    layout->add_option("<graph>", cmd.m_graph, "A graph file of any type supported by Bandage")
            ->required()->check(CLI::ExistingFile);
    layout->add_option("<layout>", cmd.m_layout, "The layout file to be created (must end with .tsv or .layout)")
            ->required();

    return layout;
}

int handleLayoutCmd(QApplication *app,
                   const CLI::App &cli, const LayoutCmd &cmd) {
    auto layoutFileExtension = cmd.m_layout.extension();
    bool isTSV;

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (layoutFileExtension == ".tsv")
        isTSV = true;
    else if (layoutFileExtension == ".layout")
        isTSV = false;
    else {
        outputText("Bandage-NG error: the output filename must end in .tsv or .layout", &err);
        return 1;
    }

    bool loadSuccess = g_assemblyGraph->loadGraphFromFile(cmd.m_graph.c_str());
    if (!loadSuccess) {
        outputText(("Bandage-NG error: could not load " + cmd.m_graph.native()).c_str(), &err); // FIXME
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
    std::vector<DeBruijnNode *> startingNodes = graph::getStartingNodes(&errorTitle, &errorMessage,
                                                                        *g_assemblyGraph, scope);
    if (!errorMessage.isEmpty()) {
        err << errorMessage << Qt::endl;
        return 1;
    }

    g_assemblyGraph->markNodesToDraw(scope, startingNodes);

    GraphLayoutStorage layout =
            GraphLayoutWorker(g_settings->graphLayoutQuality,
                              g_settings->linearLayout,
                              g_settings->componentSeparation).layoutGraph(*g_assemblyGraph);

    bool success = (isTSV ?
                    layout::io::saveTSV(cmd.m_layout.c_str(), layout) :
                    layout::io::save(cmd.m_layout.c_str(), layout));
    
    if (!success) {
        out << "There was an error writing the layout to file." << Qt::endl;
        return 1;
    }

    return 0;
}
