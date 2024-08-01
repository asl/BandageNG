// Copyright 2024 Anton Korobeynikov

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
// along with Bandage-NG.  If not, see <http://www.gnu.org/licenses/>.


#ifndef GRAPHICSITEMLINK_H
#define GRAPHICSITEMLINK_H

#include "graphicsitemedge.h"
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPointF>

class DeBruijnEdge;
class AssemblyGraph;

class GraphicsItemLink : public GraphicsItemEdge {
public:
    explicit GraphicsItemLink(DeBruijnEdge *deBruijnEdge, const AssemblyGraph &graph,
                              QGraphicsItem *parent = nullptr);

    void remakePath() override;
};

#endif // GRAPHICSITEMEDGE_H
