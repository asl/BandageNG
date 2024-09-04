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


#ifndef PATH_H
#define PATH_H

#include "graphlocation.h"

#include <llvm/Support/Error.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include <vector>

class DeBruijnNode;
class DeBruijnEdge;
class AssemblyGraph;

class Path {
public:
    // [from, to] since UI does this
    struct Range {
        int from; int to;
    };

    struct MappingRange {
        // on genome/contig/whatever
        Range initial_range;
        // on node
        Range mapped_range;
    };


    // Class used to report error if path is not connected via pair of nodes
    class NotConnectedError : public llvm::ErrorInfo<NotConnectedError> {
      public:
        NotConnectedError(const DeBruijnNode *from, const DeBruijnNode *to)
                : from(from), to(to) {}

        void log(llvm::raw_ostream &OS) const override;
        std::error_code convertToErrorCode() const override {
            llvm_unreachable("Not implemented");
        }
        static char ID;

      private:
        const DeBruijnNode *from = nullptr;
        const DeBruijnNode *to = nullptr;
    };

    using MappingPath = std::vector<std::pair<DeBruijnNode*, MappingRange>>;

    //CREATORS
    Path() {}
    Path(GraphLocation location);

    static Path makeFromUnorderedNodes(const std::vector<DeBruijnNode *> &nodes,
                                       bool strandSpecific);
    static llvm::Expected<Path>
    makeFromOrderedNodes(const std::vector<DeBruijnNode *> &nodes,
                         bool circular);
    static Path makeFromString(const QString& pathString,
                               const AssemblyGraph &graph,
                               bool circular,
                               QString * pathStringFailure);

    //ACCESSORS
    const auto& nodes() const {return m_nodes;}
    const auto& edges() const {return m_edges;}
    bool isEmpty() const {return m_nodes.empty();}
    bool isCircular() const;
    bool haveSameNodes(const Path& other) const;
    bool hasNodeSubset(const Path& other) const;
    [[nodiscard]] QByteArray getPathSequence() const;
    [[nodiscard]] QByteArray getFasta(QString name = "") const;
    [[nodiscard]] QByteArray getAAFasta(unsigned shift, QString name = "") const;
    [[nodiscard]] QString getString(bool spaces) const;
    int getLength() const;
    QList<Path> extendPathInAllPossibleWays() const;
    bool canNodeFitOnEnd(DeBruijnNode * node, Path * extendedPath) const;
    bool canNodeFitAtStart(DeBruijnNode * node, Path * extendedPath) const;

    std::vector<int> getPosition(const DeBruijnNode *node) const;
    std::vector<DeBruijnNode *> getNodesAt(int startPosition, int endPosition) const;
    MappingPath getNodeCovering(int startPosition, int endPosition) const;

    bool containsNode(const DeBruijnNode * node) const;
    bool containsEntireNode(const DeBruijnNode * node) const;
    bool isInMiddleOfPath(const DeBruijnNode * node) const;
    unsigned int numberOfOccurrencesInMiddleOfPath(const DeBruijnNode * node) const;
    bool isStartingNode(const DeBruijnNode * node) const;
    bool isEndingNode(const DeBruijnNode * node) const;
    double getStartFraction() const;
    double getEndFraction() const;
    size_t getNodeCount() const { return m_nodes.size(); }
    [[nodiscard]] GraphLocation getStartLocation() const {return m_startLocation;}
    [[nodiscard]] GraphLocation getEndLocation() const {return m_endLocation;}
    bool operator==(Path const &other) const;

    //MODIFERS
    bool addNode(DeBruijnNode * newNode, bool strandSpecific, bool makeCircularIfPossible);
    void extendPathToIncludeEntirityOfNodes();
    void trim(int start = 0, int end = 0);

    //STATIC
    static QList<Path> getAllPossiblePaths(GraphLocation startLocation,
                                           GraphLocation endLocation,
                                           int nodeSearchDepth,
                                           int minDistance, int maxDistance);

private:
    GraphLocation m_startLocation;
    GraphLocation m_endLocation;
    std::vector<DeBruijnNode *> m_nodes;
    std::vector<DeBruijnEdge *> m_edges;

    bool checkForOtherEdges();
};

struct Walk {
    std::string sampleId;
    unsigned seqStart, seqEnd;
    unsigned hapIndex;
    Path walk;
};

#endif // PATH_H
