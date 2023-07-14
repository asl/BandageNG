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
#include <stdexcept>

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
                std::vector<DeBruijnNode*> pathNodes;
                pathNodes.reserve(path->segments.size());

                for (const auto &node: path->segments)
                    pathNodes.push_back(graph.m_deBruijnGraphNodes.at(node));
                graph.m_deBruijnGraphPaths.emplace(path->name,
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

            std::vector<DeBruijnNode *> pathNodes;
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

            Path p(Path::makeFromOrderedNodes(pathNodes, false));
            // Start / end positions on path are zero-based, graph location is 1-based. So we'd just trim
            // the corresponding amounts
            p.trim(path->pstart, path->plen - path->pend - 1);
            graph.m_deBruijnGraphPaths.emplace(path->name, std::move(p));
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
            auto pstartSv = row["pstart"].get_sv();
            auto pendSv = row["pend"].get_sv();
            QStringList pathParts = QString(QByteArray(pathSv.data(), pathSv.size())).split(";");
            QStringList startParts = QString(QByteArray(pstartSv.data(), pstartSv.size())).split(",");
            QStringList endParts = QString(QByteArray(pendSv.data(), pendSv.size())).split(",");

            if (pathParts.size() != startParts.size() ||
                pathParts.size() != endParts.size())
                throw std::logic_error("Invalid path start / end components");

            auto addPath =
                    [&](const std::string &name,
                        const QString &pathPart, const QString &start, const QString &end) {
                std::vector<DeBruijnNode *> pathNodes;
                for (const auto &nodeName: pathPart.split(","))
                    pathNodes.push_back(graph.m_deBruijnGraphNodes.at(nodeName.toStdString()));

                Path p(Path::makeFromOrderedNodes(pathNodes, false));
                int sPos = start.toInt(); // if conversion fails, we'd end with zero, we're ok with it.
                bool ok = false;
                int ePos = end.toInt(&ok); // if conversion fails, we'd need end of the node, not zero here.
                if (!ok)
                    ePos = pathNodes.back()->getLength() - 1;
                p.trim(sPos, pathNodes.back()->getLength() - ePos - 1);
                graph.m_deBruijnGraphPaths.emplace(name, std::move(p));
            };
            if (pathParts.size() == 1) {
                // Keep the name as-is
                addPath(name,
                        pathParts.front(), startParts.front(), endParts.front());
            } else {
                size_t pathIdx = 0;
                for (size_t pathIdx = 0; pathIdx < pathParts.size(); ++pathIdx) {
                    addPath(name + "_" + std::to_string(pathIdx),
                            pathParts[pathIdx], startParts[pathIdx], endParts[pathIdx]);
                }
            }
        }

        return true;
    }

    bool loadSPAdesPaths(AssemblyGraph &graph,
                         QString fileName) {
        enum class State {
            PathName, // e.g. NODE_1_length_348461_cov_16.477994, we verify NODE_ prefix
            Segment,  // Ends with ";" or newline
        };

        QFile inputFile(fileName);
        if (!inputFile.open(QIODevice::ReadOnly))
            return false;

        QTextStream in(&inputFile);
        State state = State::PathName;

        std::string pathName;
        unsigned pathIdx;
        while (!in.atEnd()) {
            QByteArray line = in.readLine().toLatin1();
            if (line.length() == 0)
                continue;

            switch (state) {
                case State::PathName: {
                    if (!line.startsWith("NODE_"))
                        throw std::logic_error("invalid path name: does not start with NODE");
                    pathName = line.toStdString(); pathIdx = 1;
                    state = State::Segment;
                    break;
                }
                case State::Segment: {
                    if (line.endsWith(";")) {
                        state = State::Segment;
                        line = line.chopped(1);
                    } else
                        state = State::PathName;

                    auto addPath = [&](const std::string &name,
                                       const QString &path) {
                        std::vector<DeBruijnNode *> pathNodes;
                        for (const auto &nodeName: path.split(","))
                            pathNodes.push_back(graph.m_deBruijnGraphNodes.at(nodeName.toStdString()));

                        graph.m_deBruijnGraphPaths.emplace(name,
                                                           Path::makeFromOrderedNodes(pathNodes, false));
                    };

                    // Parse but do not add reverse-complementary paths
                    if (pathName.at(pathName.size() - 1) == '\'')
                        continue;

                    if (pathIdx == 1 && state == State::PathName)
                        // Keep the name as-is in case of single-segment path
                        addPath(pathName, line);
                    else
                        addPath(pathName + "_" + std::to_string(pathIdx++), line);

                    break;
                }
            }
        }

        return true;
    }

}
