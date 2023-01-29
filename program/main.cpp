// Copyright 2017 Ryan Wick
// Copyright 2022-2023 Anton Korobeynikov

// This file is part of Bandage

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


#include "CLI/Argv.hpp"
#include "graph/assemblygraph.h"

#include "program/settings.h"
#include "program/memory.h"
#include "program/globals.h"
#include "graphsearch/blast/blastsearch.h"

#include "ui/mainwindow.h"
#include "graph/annotationsmanager.h"
#include "ui/bandagegraphicsview.h"

#include "command_line/load.h"
#include "command_line/info.h"
#include "command_line/image.h"
#include "command_line/querypaths.h"
#include "command_line/reduce.h"
#include "command_line/settings.h"
#include "command_line/commoncommandlinefunctions.h"
#include <CLI/CLI.hpp>

#include <QApplication>
#include <QString>
#include <QTextStream>
#include <variant>

#ifndef Q_OS_WIN32
#include <sys/ioctl.h>
#endif //Q_OS_WIN32

#ifndef APP_VERSION
#define APP_VERSION "<unknown version>"
#endif

using SubCmd = std::variant<std::monostate,
                            LoadCmd,
                            ImageCmd,
                            InfoCmd,
                            ReduceCmd,
                            QueryPathsCmd>;

static SubCmd parseCmdLine(CLI::App &app) {
    SubCmd subcmd;

    app.description(getBandageTitleAsciiArt() + '\n' +
                    "Version: " + APP_VERSION + '\n');
    app.set_version_flag("--version", APP_VERSION);
    app.set_help_all_flag("--helpall");
    app.require_subcommand(0, 1);
    app.fallthrough();

    addSettings(app);

    // "BandageNG load"
    LoadCmd loadCmd;
    auto *load = addLoadSubcommand(app, loadCmd);

    // "BandgeNG image"
    ImageCmd imageCmd;
    auto *image = addImageSubcommand(app, imageCmd);

    // "BandageNG info"
    InfoCmd infoCmd;
    auto *info = addInfoSubcommand(app, infoCmd);

    // "BandageNG reduce"
    ReduceCmd reduceCmd;
    auto *reduce = addReduceSubcommand(app, reduceCmd);

    // "BandageNG rquerypaths"
    QueryPathsCmd qpCmd;
    auto *qp = addQueryPathsSubcommand(app, qpCmd);

    app.footer("Online Bandage help: https://github.com/asl/BandageNG/wiki");

    app.parse();

    if (app.got_subcommand(load)) {
        g_memory->commandLineCommand = BANDAGE_LOAD; // FIXME: not needed
        subcmd = loadCmd;
    } else if (app.got_subcommand(image)) {
        g_memory->commandLineCommand = BANDAGE_IMAGE; // FIXME: not needed
        subcmd = imageCmd;
    } else if (app.got_subcommand(info)) {
        g_memory->commandLineCommand = BANDAGE_INFO; // FIXME: not needed
        subcmd = infoCmd;
    } else if (app.got_subcommand(reduce)) {
        g_memory->commandLineCommand = BANDAGE_REDUCE; // FIXME: not needed
        subcmd = reduceCmd;
    } else if (app.got_subcommand(qp)) {
        g_memory->commandLineCommand = BANDAGE_QUERY_PATHS; // FIXME: not needed
        subcmd = qpCmd;
    }

    return subcmd;
}

static void chooseQtPlatform(const CLI::App &app, const SubCmd &cmd) {
    // Chose default Qt platform. Some ways of running Bandage require the
    // normal platform while other command line only ways use the minimal
    // platform. Frustratingly, Bandage image cannot render text properly with
    // the minimal platform, so we need to use the full platform if Bandage
    // image is run with text labels.
    bool imageWithText = false; //std::holds_alternative<ImageCmd>(cmd) &&
    //(app.count("--names") || app.count("--lengths") ||
    //                      app.count("--depth") || app.count("--blasthits"));
    bool guiNeeded = std::holds_alternative<std::monostate>(cmd) ||
                     std::holds_alternative<LoadCmd>(cmd) || imageWithText;

    // Only use minimal platform on Linux. Both Windows and MacOS X always have
    // full platform available (as there is no real headless mode)
#ifdef Q_OS_LINUX
    if (!guiNeeded && qgetenv("QT_QPA_PLATFORM").empty())
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("minimal"));
#endif
}

int main(int argc, char *argv[]) {
    CLI::App cli;
    SubCmd cmd;

    g_memory.reset(new Memory());
    g_settings.reset(new Settings());

    try {
        cmd = parseCmdLine(cli);
    } catch (const CLI::ParseError &e) {
        return cli.exit(e);
    }

    chooseQtPlatform(cli, cmd);
    QScopedPointer<QApplication> app(new QApplication(argc, argv));

    // Create the important global objects.
    g_blastSearch.reset(new search::BlastSearch());
    g_assemblyGraph.reset(new AssemblyGraph());
    g_graphicsView = new BandageGraphicsView();
    g_annotationsManager = std::make_shared<AnnotationsManager>();

    // Save the terminal width (useful for displaying help text neatly).
#ifndef Q_OS_WIN32
    struct winsize ws;
    ioctl(0, TIOCGWINSZ, &ws);
    g_memory->terminalWidth = ws.ws_col;
    if (g_memory->terminalWidth < 50) g_memory->terminalWidth = 50;
    if (g_memory->terminalWidth > 300) g_memory->terminalWidth = 300;
#endif //Q_OS_WIN32

    app->setApplicationName("Bandage-NG");
    app->setApplicationVersion(APP_VERSION);

    return std::visit([&](const auto &command) {
        using T = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<T, LoadCmd>) {
            return handleLoadCmd(app.get(), cli, command);
        } else  if constexpr (std::is_same_v<T, ImageCmd>) {
            return handleImageCmd(app.get(), cli, command);
        } else  if constexpr (std::is_same_v<T, InfoCmd>) {
            return handleInfoCmd(app.get(), cli, command);
        } else  if constexpr (std::is_same_v<T, ReduceCmd>) {
            return handleReduceCmd(app.get(), cli, command);
        } else  if constexpr (std::is_same_v<T, QueryPathsCmd>) {
            return handleQueryPathsCmd(app.get(), cli, command);
        } else {
            MainWindow w;
            w.show();

            return app->exec();
        }
    }, cmd);
}
