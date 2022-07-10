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

#include "nodecolorer.h"

#include <tsl/htrie_map.h>
#include <vector>
#include <unordered_set>

class DepthNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Color by depth"; };
};

class UniformNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Uniform color"; };
};

class RandomNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] std::pair<QColor, QColor> get(const GraphicsItemNode *node,
                                                const GraphicsItemNode *rcNode) override;
    [[nodiscard]] const char* name() const override { return "Random colors"; };
};

class GrayNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Gray colors"; };
};

class CustomNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Custom colors"; };
};

class ContiguityNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Color by contiguity"; };
};

class GCNodeColorer : public INodeColorer {
public:
    using INodeColorer::INodeColorer;

    QColor get(const GraphicsItemNode *node) override;
    [[nodiscard]] const char* name() const override { return "Color by GC content"; };
};

class TagValueNodeColorer : public INodeColorer {
public:
    explicit TagValueNodeColorer(NodeColorScheme scheme)
            : INodeColorer(scheme) {
        if (m_graph)
            TagValueNodeColorer::reset();
    }

    QColor get(const GraphicsItemNode *node) override;
    void reset() override;
    [[nodiscard]] const char* name() const override { return "Color by tag value"; };

    void setTagName(const std::string &tagName) { m_tagName = tagName; }
    [[nodiscard]] auto tagNames() const {
        std::vector<std::string> names(m_tagNames.begin(), m_tagNames.end());
        std::sort(names.begin(), names.end());
        return names;
    }

private:
    std::string m_tagName = "";
    tsl::htrie_map<char, QColor> m_allTags;
    std::unordered_set<std::string> m_tagNames;
};

class CSVNodeColorer : public INodeColorer {
public:
    explicit CSVNodeColorer(NodeColorScheme scheme)
            : INodeColorer(scheme) {
        if (m_graph)
            CSVNodeColorer::reset();
    }
    QColor get(const GraphicsItemNode *node) override;
    void reset() override;
    [[nodiscard]] const char* name() const override { return "Color by CSV columns"; };

    void setColumnIdx(unsigned idx) { m_colIdx = idx; }

private:
    unsigned m_colIdx = 0;
    std::vector<tsl::htrie_map<char, QColor>> m_colors;
};