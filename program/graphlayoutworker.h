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

#include <QObject>
#include <QFutureSynchronizer>

namespace ogdf {
    class GraphAttributes;
    template<class T>
    class EdgeArray;
}

class AssemblyGraph;

class GraphLayouter {
public:
    GraphLayouter(int graphLayoutQuality,
                  bool useLinearLayout,
                  double graphLayoutComponentSeparation,
                  double aspectRatio = 1.333333);
    virtual ~GraphLayouter() {}
    virtual void init() = 0;
    virtual void cancel() = 0;
    virtual void run(ogdf::GraphAttributes &GA, const ogdf::EdgeArray<double> &edges) = 0;

protected:
    int m_graphLayoutQuality;
    bool m_useLinearLayout;
    double m_graphLayoutComponentSeparation;
    double m_aspectRatio;
};

class GraphLayoutWorker : public QObject {
    Q_OBJECT

public:
    GraphLayoutWorker(AssemblyGraph &graph,
                      int graphLayoutQuality,
                      bool useLinearLayout,
                      double graphLayoutComponentSeparation,
                      double aspectRatio = 1.333333);
    ~GraphLayoutWorker() override = default;

private:
    void buildGraph();
    void determineLinearNodePositions();

    QFutureSynchronizer<void> m_taskSynchronizer;
    std::vector<std::unique_ptr<GraphLayouter>> m_state;
    AssemblyGraph &m_graph;
    int m_graphLayoutQuality;
    bool m_useLinearLayout;
    double m_graphLayoutComponentSeparation;
    double m_aspectRatio;

public slots:
    void layoutGraph();

    [[maybe_unused]] void cancelLayout();
};