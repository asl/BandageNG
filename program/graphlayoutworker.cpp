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


#include "graphlayoutworker.h"
#include "graph/assemblygraph.h"
#include "ogdf/energybased/FMMMLayout.h"
#include "ogdf/energybased/fmmm/FMMMOptions.h"

#include <ctime>

GraphLayoutWorker::GraphLayoutWorker(AssemblyGraph &graph,
                                     int graphLayoutQuality, bool useLinearLayout,
                                     double graphLayoutComponentSeparation, double aspectRatio)
        : m_layout(new ogdf::FMMMLayout),
          m_graph(graph),
          m_graphLayoutQuality(graphLayoutQuality),
          m_useLinearLayout(useLinearLayout),
          m_graphLayoutComponentSeparation(graphLayoutComponentSeparation),
          m_aspectRatio(aspectRatio) {}

void GraphLayoutWorker::layoutGraph() {
    m_layout->randSeed(clock());
    m_layout->useHighLevelOptions(false);
    m_layout->unitEdgeLength(1.0);
    m_layout->allowedPositions(ogdf::FMMMOptions::AllowedPositions::All);
    m_layout->pageRatio(m_aspectRatio);
    m_layout->minDistCC(m_graphLayoutComponentSeparation);
    m_layout->stepsForRotatingComponents(50); // Helps to make linear graph components more horizontal.

    m_layout->initialPlacementForces(m_useLinearLayout ?
                                     ogdf::FMMMOptions::InitialPlacementForces::KeepPositions :
                                     ogdf::FMMMOptions::InitialPlacementForces::RandomTime);

    switch (m_graphLayoutQuality) {
        case 0:
            m_layout->fixedIterations(3);
            m_layout->fineTuningIterations(1);
            m_layout->nmPrecision(2);
            break;
        case 1:
            m_layout->fixedIterations(15);
            m_layout->fineTuningIterations(10);
            m_layout->nmPrecision(2);
            break;
        case 2:
            m_layout->fixedIterations(30);
            m_layout->fineTuningIterations(20);
            m_layout->nmPrecision(4);
            break;
        case 3:
            m_layout->fixedIterations(60);
            m_layout->fineTuningIterations(40);
            m_layout->nmPrecision(6);
            break;
        case 4:
            m_layout->fixedIterations(120);
            m_layout->fineTuningIterations(60);
            m_layout->nmPrecision(8);
            break;
    }

    m_layout->call(m_graph.m_ogdfGraphAttributes, m_graph.m_ogdfEdgeLengths);

    emit finishedLayout();
}

void GraphLayoutWorker::cancelLayout() {
    m_layout->fixedIterations(0);
    m_layout->fineTuningIterations(0);
    m_layout->threshold(std::numeric_limits<double>::max());
}

GraphLayoutWorker::~GraphLayoutWorker() {
    delete m_layout;
}

