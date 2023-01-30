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


#include "image.h"
#include "commoncommandlinefunctions.h"

#include "graph/assemblygraph.h"

#include "graphsearch/blast/blastsearch.h"

#include "program/globals.h"
#include "program/settings.h"

#include "layout/graphlayout.h"
#include "layout/graphlayoutworker.h"

#include "ui/bandagegraphicsscene.h"
#include "ui/bandagegraphicsview.h"

#include <vector>
#include <QPainter>
#include <QSvgGenerator>

#include <CLI/CLI.hpp>

CLI::App *addImageSubcommand(CLI::App &app, ImageCmd &cmd) {
    auto *image = app.add_subcommand("image", "Generate an image file of a graph");
    image->add_option("<graph>", cmd.m_graph, "A graph file of any type supported by Bandage")
            ->required()->check(CLI::ExistingFile);
    image->add_option("<output_file>", cmd.m_image, "The image file to be created (must end in '.jpg', '.png' or '.svg')")
            ->required();
    image->add_option("--height", cmd.m_height, "Image height")
            ->default_val(cmd.m_height)->check(CLI::Range(1, 32767));
    image->add_option("--width", cmd.m_width, "Image width")
            ->check(CLI::Range(1, 32767));
    image->add_option("--color", cmd.m_color, "csv file with 2 columns: first the node name second the node color")
            ->check(CLI::ExistingFile);

    image->footer("If only height or width is set, the other will be determined automatically. If both are set, the image will be exactly that size");

    return image;
}

int handleImageCmd(QApplication *app,
                   const CLI::App &cli, const ImageCmd &cmd) {
    auto imageFileExtension = cmd.m_image.extension();
    bool pixelImage;

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (imageFileExtension == ".png" || imageFileExtension == ".jpg")
        pixelImage = true;
    else if (imageFileExtension == ".svg")
        pixelImage = false;
    else {
        outputText("Bandage-NG error: the output filename must end in .png, .jpg or .svg", &err);
        return 1;
    }

    bool loadSuccess = g_assemblyGraph->loadGraphFromFile(cmd.m_graph.c_str());
    if (!loadSuccess) {
        outputText(("Bandage-NG error: could not load " + cmd.m_graph.native()).c_str(), &err); // FIXME
        return 1;
    }

    // Since frame rate performance doesn't matter for a fixed image, set the
    // default node outline to a nonzero value.
    g_settings->outlineThickness = 0.3;

    // For Bandage image, it is necessary to position node labels at the
    // centre of the node, not the visible centre(s).  This is because there
    // is no viewport.
    g_settings->positionTextNodeCentre = true;

    // The zoom level needs to be set so rainbow-style BLAST hits are rendered
    // properly.
    g_absoluteZoom = 10.0;

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

    if (!cmd.m_color.empty()) {
        QString errormsg;
        QStringList columns;
        bool coloursLoaded = false;

        if (!g_assemblyGraph->loadCSV(cmd.m_color.c_str(), &columns, &errormsg, &coloursLoaded)) {
            err << errormsg << Qt::endl;
            return 1;
        }

        if (!coloursLoaded) {
            err << cmd.m_color.c_str() << " didn't contain color" << Qt::endl;
            return 1;
        }
         g_settings->initializeColorer(CUSTOM_COLOURS);
    }


    g_assemblyGraph->markNodesToDraw(scope, startingNodes);
    BandageGraphicsScene scene;
    {
        GraphLayoutStorage layout =
                GraphLayoutWorker(g_settings->graphLayoutQuality,
                                  g_settings->linearLayout,
                                  g_settings->componentSeparation).layoutGraph(*g_assemblyGraph);

        scene.addGraphicsItemsToScene(*g_assemblyGraph, layout);
        scene.setSceneRectangle();
    }
    double sceneRectAspectRatio = scene.sceneRect().width() / scene.sceneRect().height();

    // Determine image size
    // If neither height nor width set, use a default of height = 1000.
    unsigned height = cmd.m_height, width = cmd.m_width;
    if (height == 0 && width == 0)
        height = 1000;

    //If only height or width is set, scale the other to fit.
    if (height > 0 && width == 0)
        width = height * sceneRectAspectRatio;
    else if (height == 0 && width > 0)
        height = width / sceneRectAspectRatio;

    bool success = true;
    QPainter painter;
    if (pixelImage) {
        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(Qt::white);
        painter.begin(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        scene.render(&painter);
        success = image.save(cmd.m_image.c_str());
        painter.end();
    } else { //SVG
        QSvgGenerator generator;
        generator.setFileName(cmd.m_image.c_str());
        generator.setSize(QSize(width, height));
        generator.setViewBox(QRect(0, 0, width, height));
        painter.begin(&generator);
        painter.fillRect(0, 0, width, height, Qt::white);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        scene.render(&painter);
        painter.end();
    }

    if (!success) {
        out << "There was an error writing the image to file." << Qt::endl;
        return 1;
    }

    return 0;
}
