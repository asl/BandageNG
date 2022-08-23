//Copyright 2017 Ryan Wick

//This file is part of Bandage

//Bandage is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//Bandage is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with Bandage.  If not, see <http://www.gnu.org/licenses/>.


#include "reduce.h"
#include "commoncommandlinefunctions.h"

#include "graph/assemblygraph.h"
#include "graph/gfawriter.h"
#include "graphsearch/blast/blastsearch.h"

#include "ui/bandagegraphicsscene.h"
#include "ui/bandagegraphicsview.h"

#include "program/globals.h"

#include <QDir>
#include <QPainter>
#include <QSvgGenerator>

#include <vector>

int bandageReduce(QStringList arguments)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (checkForHelp(arguments))
    {
        printReduceUsage(&out, false);
        return 0;
    }

    if (checkForHelpAll(arguments))
    {
        printReduceUsage(&out, true);
        return 0;
    }

    if (arguments.size() < 2)
    {
        printReduceUsage(&err, false);
        return 1;
    }

    QString inputFilename = arguments.at(0);
    arguments.pop_front();

    if (!checkIfFileExists(inputFilename))
    {
        outputText("Bandage-NG error: " + inputFilename + " does not exist", &err);
        return 1;
    }

    QString outputFilename = arguments.at(0);
    arguments.pop_front();
    if (!outputFilename.endsWith(".gfa"))
        outputFilename += ".gfa";

    QString error = checkForInvalidReduceOptions(arguments);
    if (!error.isEmpty()) {
        outputText("Bandage-NG error: " + error, &err);
        return 1;
    }

    if (!g_assemblyGraph->loadGraphFromFile(inputFilename)) {
        outputText("Bandage-NG error: could not load " + inputFilename, &err);
        return 1;
    }

    parseSettings(arguments);

    if (isOptionPresent("--query", &arguments)) {
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


void printReduceUsage(QTextStream * out, bool all)
{
    QStringList text;

    text << "Bandage reduce takes an input graph and saves a reduced subgraph using the graph scope settings. The saved graph will be in GFA format.";
    text << "";
    text << "If a graph scope is not specified, then the 'entire' scope will be used, in which case this will simply convert the input graph to GFA format.";
    text << "";
    text << "Usage:    Bandage reduce <inputgraph> <outputgraph> [options]";
    text << "";
    text << "Positional parameters:";
    text << "<inputgraph>        A graph file of any type supported by Bandage";
    text << "<outputgraph>       The filename for the GFA graph to be made (if it does not end in '.gfa', that extension will be added)";
    text << "";

    int nextLineIndex = text.size();
    getCommonHelp(&text);
    text[nextLineIndex] = "Options:  " + text[nextLineIndex];

    if (all)
        getSettingsUsage(&text);
    else
    {
        nextLineIndex = text.size();
        getGraphScopeOptions(&text);
        text[nextLineIndex] = "Settings: " + text[nextLineIndex];
    }
    text << "";
    getOnlineHelpMessage(&text);

    outputText(text, out);
}



QString checkForInvalidReduceOptions(QStringList arguments)
{
    return checkForInvalidOrExcessSettings(&arguments);
}
