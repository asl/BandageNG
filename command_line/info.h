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

#include <QApplication>
#include <filesystem>

namespace CLI {
    class App;
}

struct InfoCmd {
    std::filesystem::path m_graph;
    bool m_tsv = false;
};

CLI::App *addInfoSubcommand(CLI::App &app,
                            InfoCmd &cmd);
int handleInfoCmd(QApplication *app,
                  const CLI::App &cli, const InfoCmd &cmd);
