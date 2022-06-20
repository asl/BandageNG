//Copyright 2017 Ryan Wick

//This file is part of Bandage

//Bandage is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//Bandage is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include "ogdf/energybased/FMMMLayout.h"

#include <QObject>
#include <QFutureSynchronizer>

namespace ogdf {
    class FMMMLayout;
}

class AssemblyGraph;

class GraphLayoutWorker : public QObject
{
    Q_OBJECT

public:
    GraphLayoutWorker(AssemblyGraph &graph,
                      int graphLayoutQuality,
                      bool useLinearLayout,
                      double graphLayoutComponentSeparation,
                      double aspectRatio = 1.333333);
    ~GraphLayoutWorker() = default;

private:
    void buildGraph();
    void determineLinearNodePositions();
    void initLayout(ogdf::FMMMLayout &layout) const;

    QFutureSynchronizer<void> m_taskSynchronizer;
    std::vector<ogdf::FMMMLayout> m_layout;
    AssemblyGraph &m_graph;
    int m_graphLayoutQuality;
    bool m_useLinearLayout;
    double m_graphLayoutComponentSeparation;
    double m_aspectRatio;

public slots:
    void layoutGraph();

    [[maybe_unused]] void cancelLayout();
};