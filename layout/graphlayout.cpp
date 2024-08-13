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

#include "graphlayout.h"

#include "graph/assemblygraph.h"
#include "graph/graphicsitemnode.h"

namespace layout {
    GraphLayout fromGraph(const AssemblyGraph &graph, bool simplified) {
        GraphLayout res(graph);

        for (auto *node : graph.m_deBruijnGraphNodes) {
            if (node->isNotDrawn())
                continue;
            if (!node->hasGraphicsItem())
                continue;

            const auto &segments = node->getGraphicsItemNode()->m_linePoints;
            if (segments.empty())
                continue;

            if (simplified) {
                size_t idx = (segments.size() - 1) / 2;
                res.add(node, segments[idx]);
            } else {
                res.segments(node) = segments;
            }
        }

        return res;
    }

    void apply(AssemblyGraph &graph, const GraphLayout &layout) {
        // Determine the set of nodes to be drawn from the layout
        graph.resetNodes();
        for (auto& entry : layout)
            entry.first->setAsDrawn();
    }
}
