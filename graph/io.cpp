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

#include <csv/csv.hpp>

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

            // FIXME: handle orientation
            // FIXME: handle start / end positions
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

    bool loadSPAlignerPaths(AssemblyGraph &graph,
                            QString fileName) {
        csv::CSVFormat format;
        format.delimiter('\t')
                .quote('"')
                .no_header();  // Parse TSVs without a header row
        format.column_names({
                                    "name",
                                    "qstart", "qend",
                                    "pstart", "pend",
                                    "length",
                                    "path", "alnlength",
                                    "pathseq"
                            });
        csv::CSVReader csvReader(fileName.toStdString(), format);

        for (csv::CSVRow &row: csvReader) {
            if (row.size() != 9)
                throw std::logic_error("Mandatory columns were not found");

            std::string name(row["name"].get_sv());
            auto pathSv = row["path"].get_sv();
            QStringList pathParts = QString(QByteArray(pathSv.data(), pathSv.size())).split(";");

            auto addPath = [&](const std::string &name, const QString &pathPart) {
                QList<DeBruijnNode *> pathNodes;
                for (const auto &nodeName: pathPart.split(","))
                    pathNodes.push_back(graph.m_deBruijnGraphNodes.at(nodeName.toStdString()));

                graph.m_deBruijnGraphPaths[name] =
                        new Path(Path::makeFromOrderedNodes(pathNodes, false));
            };
            if (pathParts.size() == 1) {
                // Keep the name as-is
                addPath(name, pathParts.front());
            } else {
                size_t pathIdx = 0;
                for (const auto &pathPart: pathParts) {
                    addPath(name + "_" + std::to_string(pathIdx), pathPart);
                    pathIdx += 1;
                }
            }
        }

        return true;
    }

}