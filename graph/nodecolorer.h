// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

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

#pragma once

#include <QColor>
#include <QSharedPointer>

class AssemblyGraph;
class GraphicsItemNode;

// This needs to be synchronizes with selection combo box!
enum NodeColorScheme : int {
    GRAY_COLOR = 0,
    RANDOM_COLOURS = 1,
    UNIFORM_COLOURS = 2,
    DEPTH_COLOUR = 3,
    CONTIGUITY_COLOUR = 4,
    CUSTOM_COLOURS = 5,
    GC_CONTENT = 6,
    TAG_VALUE = 7,
    CSV_COLUMN = 8,
    LAST_SCHEME = CSV_COLUMN
};

class INodeColorer {
public:
    explicit INodeColorer(NodeColorScheme scheme);
    virtual ~INodeColorer() = default;

    [[nodiscard]] virtual QColor get(const GraphicsItemNode *node) = 0;
    [[nodiscard]] virtual std::pair<QColor, QColor> get(const GraphicsItemNode *node,
                                                        const GraphicsItemNode *rcNode);
    virtual void reset() {};
    [[nodiscard]] virtual const char* name() const = 0;

    static std::unique_ptr<INodeColorer> create(NodeColorScheme scheme);

    [[nodiscard]] NodeColorScheme scheme() const { return m_scheme; }
protected:
    NodeColorScheme m_scheme;
    QSharedPointer<AssemblyGraph> &m_graph;
};
