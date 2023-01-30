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

#include "load.h"
#include "commoncommandlinefunctions.h"
#include "program/settings.h"
#include "ui/mainwindow.h"
#include <QApplication>

#include <CLI/CLI.hpp>
#include <filesystem>

CLI::App *addLoadSubcommand(CLI::App &app, LoadCmd &cmd) {
    auto *load = app.add_subcommand("load", "Launch the BandageNG GUI and load a graph file");
    load->add_flag("--draw", cmd.m_draw, "Draw graph after loading");
    load->add_option("<graph>", cmd.m_graph, "A graph file of any type supported by Bandage")
            ->required()->check(CLI::ExistingFile);

    return load;
}

int handleLoadCmd(QApplication *app,
                  const CLI::App &cli, const LoadCmd &cmd) {
    MainWindow w{cmd.m_graph.c_str(), cmd.m_draw};

    w.show();
    return app->exec();
}
