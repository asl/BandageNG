// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#include "io.h"

#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"
#include "graphlayout.h"

#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

namespace layout::io {
    bool save(const QString &filename,
              const GraphLayout &layout) {
        QJsonObject jsonLayout;
        for (const auto &entry: layout) {
            QJsonArray segments;
            for (QPointF point: entry.second)
                segments.append(QJsonArray{point.x(), point.y()});

            jsonLayout[entry.first->getName()] = segments;
        }

        QFile saveFile(filename);
        if (!saveFile.open(QIODevice::WriteOnly))
            return false;

        saveFile.write(QJsonDocument(jsonLayout).toJson());

        return true;
    }

    bool saveTSV(const QString &filename,
                 const GraphLayout &layout) {
        QFile saveFile(filename);
        if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&saveFile);
        for (const auto &entry: layout) {
            const DeBruijnNode *node = entry.first;
            QPointF pos = entry.second.front();
            out << entry.first->getName() << '\t' << pos.x() << '\t' << pos.y() << '\n';
        }

        return true;
    }

    bool load(const QString &filename,
              GraphLayout &layout) {
        QFile loadFile(filename);
        // FIXME: Switch to Error return object stuff!
        if (!loadFile.open(QIODevice::ReadOnly | QIODevice::Text))
            throw std::runtime_error("cannot open file: " + filename.toStdString());

        QJsonParseError error;
        auto jsonLayoutDoc = QJsonDocument::fromJson(loadFile.readAll(), &error);
        if (error.error != QJsonParseError::NoError)
            throw std::runtime_error(error.errorString().toStdString());

        if (!jsonLayoutDoc.isObject())
            throw std::runtime_error("invalid layout format");

        QJsonObject jsonLayout = jsonLayoutDoc.object();
        const AssemblyGraph &graph = layout.graph();
        for (auto it = jsonLayout.begin(); it != jsonLayout.end(); ++it) {
            QString name = it.key();
            auto node = graph.m_deBruijnGraphNodes.find(name.toStdString());
            if (node == graph.m_deBruijnGraphNodes.end())
                throw std::runtime_error("graph does not contain node: " + name.toStdString());
            if (!it.value().isArray())
                throw std::runtime_error("invalid layout format");
            for (const auto &point : it.value().toArray()) {
                QJsonArray pointArray = point.toArray();
                if (pointArray.size() != 2)
                    throw std::runtime_error("invalid layout format: point size is " + std::to_string(pointArray.size()));
                layout.add(*node, { pointArray[0].toDouble(), pointArray[1].toDouble() });
            }
        }

        return true;
    }
}