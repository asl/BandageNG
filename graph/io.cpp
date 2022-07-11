// Copyright 2022 Anton Korobeynikov

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

#include "io.h"
#include "assemblygraph.h"

#include "io/gfa.h"
#include "io/gaf.h"

#include <QFile>
#include <QTextStream>

namespace io {
    bool loadGFAPaths(AssemblyGraph &graph,
                      QString fileName) {
        QFile inputFile(fileName);
        if (!inputFile.open(QIODevice::ReadOnly))
            return false;

        QTextStream in(&inputFile);
        while (!in.atEnd()) {
            QByteArray line = in.readLine().toLatin1();
            if (line.length() == 0)
                continue;

            auto val = gfa::parseRecord(line.data(), line.length());
            if (!val)
                continue;

            if (auto *path = std::get_if<gfa::path>(&*val)) {
                QList<DeBruijnNode *> pathNodes;
                pathNodes.reserve(path->segments.size());

                for (const auto &node: path->segments)
                    pathNodes.push_back(graph.m_deBruijnGraphNodes.at(node));
                graph.m_deBruijnGraphPaths[path->name] = new Path(
                        Path::makeFromOrderedNodes(pathNodes, false));
            }
        }

        return true;
    }

    bool loadGAFPaths(AssemblyGraph &graph,
                      QString fileName) {
        QFile inputFile(fileName);
        if (!inputFile.open(QIODevice::ReadOnly))
            return false;

        QTextStream in(&inputFile);
        while (!in.atEnd()) {
            QByteArray line = in.readLine().toLatin1();
            if (line.length() == 0)
                continue;

            auto path = gaf::parseRecord(line.data(), line.length());
            if (!path)
                continue;

            QList<DeBruijnNode *> pathNodes;
            pathNodes.reserve(path->segments.size());

            for (const auto &node: path->segments) {
                char orientation = node.front();
                std::string nodeName;
                nodeName.reserve(node.size());
                nodeName = node.substr(1);
                if (orientation == '>')
                    nodeName.push_back('+');
                else if (orientation == '<')
                    nodeName.push_back('-');
                else
                    throw std::runtime_error(std::string("invalid path string: ").append(node));

                pathNodes.push_back(graph.m_deBruijnGraphNodes.at(nodeName));
            }

            graph.m_deBruijnGraphPaths[path->name] =
                    new Path(Path::makeFromOrderedNodes(pathNodes, false));
        }

        return true;
    }
}