// Copyright 2017 Ryan Wick
// Copyright 2022 Anton Korobeynikov
// Copyright 2017 Andrey Zakharov

// This file is part of Bandage

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

#include "gfa.hpp"
#include "path.h"
#include "annotation.hpp"

#include "ui/mygraphicsscene.h"

#include "seq/sequence.hpp"
#include "parallel_hashmap/phmap.h"
#include "tsl/htrie_map.h"

#include "ogdf/basic/Graph.h"
#include "ogdf/basic/GraphAttributes.h"

#include <QString>
#include <QPair>
#include <QObject>
#include <vector>

class DeBruijnNode;
class DeBruijnEdge;
class MyProgressDialog;

class AssemblyGraphError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class AssemblyGraphBuilder;


class AssemblyGraph : public QObject
{
    Q_OBJECT

    friend class AssemblyGraphBuilder;
    
public:
    AssemblyGraph();
    ~AssemblyGraph() override;

    //Nodes are stored in a map with a key of the node's name.
    tsl::htrie_map<char, DeBruijnNode*> m_deBruijnGraphNodes;

    //Edges are stored in a map with a key of the starting and ending node
    //pointers.
    phmap::parallel_flat_hash_map<QPair<DeBruijnNode*, DeBruijnNode*>, DeBruijnEdge*> m_deBruijnGraphEdges;

    // Custom colors
    phmap::parallel_flat_hash_map<const DeBruijnNode*, QColor> m_nodeColors;
    // Custom labels
    phmap::parallel_flat_hash_map<const DeBruijnNode*, QString> m_nodeLabels;
    // CSV data
    phmap::parallel_flat_hash_map<const DeBruijnNode*, QStringList> m_nodeCSVData;
    // Tags
    phmap::parallel_flat_hash_map<const DeBruijnNode*, std::vector<gfa::tag>> m_nodeTags;
    phmap::parallel_flat_hash_map<const DeBruijnEdge*, std::vector<gfa::tag>> m_edgeTags;

    tsl::htrie_map<char, Path*> m_deBruijnGraphPaths;
    
    ogdf::Graph * m_ogdfGraph;
    ogdf::EdgeArray<double> * m_edgeArray;
    ogdf::GraphAttributes * m_graphAttributes;

    int m_kmer;
    int m_nodeCount;
    int m_edgeCount;
    int m_pathCount;
    long long m_totalLength;
    long long m_shortestContig;
    long long m_longestContig;
    double m_meanDepth;
    double m_firstQuartileDepth;
    double m_medianDepth;
    double m_thirdQuartileDepth;
    GraphFileType m_graphFileType;
    bool m_contiguitySearchDone;
    QString m_filename;
    QString m_depthTag;
    SequencesLoadedFromFasta m_sequencesLoadedFromFasta;

    void cleanUp();
    void createDeBruijnEdge(const QString& node1Name, const QString& node2Name,
                            int overlap = 0,
                            EdgeOverlapType overlapType = UNKNOWN_OVERLAP);
    void clearOgdfGraphAndResetNodes();
    static QByteArray getReverseComplement(const QByteArray& forwardSequence);
    void resetEdges();
    double getMeanDepth(bool drawnNodesOnly = false);
    static double getMeanDepth(const std::vector<DeBruijnNode *> &nodes);
    static double getMeanDepth(const QList<DeBruijnNode *>& nodes);
    void resetNodeContiguityStatus();
    void determineGraphInfo();
    void clearGraphInfo();
    void recalculateAllDepthsRelativeToDrawnMean();
    void recalculateAllNodeWidths();

    bool loadGraphFromFile(const QString& filename);
    void buildOgdfGraphFromNodesAndEdges(const std::vector<DeBruijnNode *>& startingNodes,
                                         int nodeDistance);
    void addGraphicsItemsToScene(MyGraphicsScene * scene);

    static QStringList splitCsv(const QString& line, const QString& sep=",");
    bool loadCSV(const QString& filename, QStringList * columns, QString * errormsg, bool * coloursLoaded);
    std::vector<DeBruijnNode *> getStartingNodes(QString * errorTitle,
                                                 QString * errorMessage,
                                                 bool doubleMode,
                                                 const QString& nodesList,
                                                 const QString& blastQueryName,
                                                 const QString& pathName);

    static bool checkIfStringHasNodes(QString nodesString);
    static QString generateNodesNotFoundErrorMessage(std::vector<QString> nodesNotInGraph,
                                              bool exact);
    std::vector<DeBruijnNode *> getNodesFromString(QString nodeNamesString,
                                                   bool exactMatch,
                                                   std::vector<QString> * nodesNotInGraph = 0);
    void layoutGraph() const;

    void setAllEdgesExactOverlap(int overlap);
    void autoDetermineAllEdgesExactOverlap();

    static void readFastaOrFastqFile(const QString& filename, std::vector<QString> * names,
                                     std::vector<QByteArray> * sequences);
    static void readFastaFile(const QString& filename, std::vector<QString> * names,
                              std::vector<QByteArray> * sequences);
    static void readFastqFile(const QString& filename, std::vector<QString> * names,
                              std::vector<QByteArray> * sequences);

    int getDrawnNodeCount() const;
    void deleteNodes(const std::vector<DeBruijnNode *> &nodes);
    void deleteEdges(const std::vector<DeBruijnEdge *> &edges);
    void duplicateNodePair(DeBruijnNode * node, MyGraphicsScene * scene);
    bool mergeNodes(QList<DeBruijnNode *> nodes, MyGraphicsScene * scene,
                    bool recalulateDepth);
    static void removeGraphicsItemEdges(const std::vector<DeBruijnEdge *> &edges,
                                 bool reverseComplement,
                                 MyGraphicsScene * scene);
    static void removeGraphicsItemNodes(const std::vector<DeBruijnNode *> &nodes,
                                 bool reverseComplement,
                                 MyGraphicsScene * scene);
    int mergeAllPossible(MyGraphicsScene * scene = 0,
                         MyProgressDialog * progressDialog = 0);

    void saveEntireGraphToFasta(const QString& filename);
    void saveEntireGraphToFastaOnlyPositiveNodes(const QString& filename);
    bool saveEntireGraphToGfa(const QString& filename);
    bool saveVisibleGraphToGfa(const QString& filename);
    void changeNodeName(const QString& oldName, const QString& newName);
    NodeNameStatus checkNodeNameValidity(const QString& nodeName) const;
    void changeNodeDepth(const std::vector<DeBruijnNode *> &nodes,
                         double newDepth);

    static QByteArray addNewlinesToSequence(const QByteArray& sequence, int interval = 70);
    int getDeadEndCount() const;
    void getNodeStats(int * n50, int * shortestNode, int * firstQuartile, int * median, int * thirdQuartile, int * longestNode) const;
    void getGraphComponentCountAndLargestComponentSize(int * componentCount, int * largestComponentLength) const;
    double getMedianDepthByBase() const;

    long long getEstimatedSequenceLength(double medianDepthByBase) const;
    long long getTotalLengthMinusEdgeOverlaps() const;
    QPair<int, int> getOverlapRange() const;
    bool attemptToLoadSequencesFromFasta();
    long long getTotalLengthOrphanedNodes() const;
    bool useLinearLayout() const;

    bool hasCustomColour(const DeBruijnNode* node) const;
    QColor getCustomColour(const DeBruijnNode* node) const;
    void setCustomColour(const DeBruijnNode* node, QColor color);

    QString getCustomLabel(const DeBruijnNode* node) const;
    void setCustomLabel(const DeBruijnNode* node, QString newLabel);

    bool hasCsvData(const DeBruijnNode* node) const;
    QStringList getAllCsvData(const DeBruijnNode *node) const;
    QString getCsvLine(const DeBruijnNode *node, int i) const;
    void setCsvData(const DeBruijnNode* node, QStringList csvData);
    void clearCsvData(const DeBruijnNode* node);

    QColor getCustomColourForDisplay(const DeBruijnNode *node) const;
    QStringList getCustomLabelForDisplay(const DeBruijnNode *node) const;

    QString getGfaSegmentLine(const DeBruijnNode *node, const QString& depthTag) const;

    QString getUniqueNodeName(QString baseName) const;
private:
    template<typename T> double getValueUsingFractionalIndex(std::vector<T> * v, double index) const;
    QString convertNormalNumberStringToBandageNodeName(QString number);
    static QStringList removeNullStringsFromList(const QStringList& in);
    std::vector<DeBruijnNode *> getNodesFromListExact(const QStringList& nodesList, std::vector<QString> * nodesNotInGraph);
    std::vector<DeBruijnNode *> getNodesFromListPartial(const QStringList& nodesList, std::vector<QString> * nodesNotInGraph);
    static std::vector<DeBruijnNode *> getNodesFromBlastHits(const QString& queryName);
    std::vector<DeBruijnNode *> getNodesInDepthRange(double min, double max);
    std::vector<int> makeOverlapCountVector();
    static QString getOppositeNodeName(QString nodeName) ;
    void clearAllCsvData();
    QString getNodeNameFromString(QString string) const;
    QString getNewNodeName(QString oldNodeName) const;
    static void duplicateGraphicsNode(DeBruijnNode * originalNode, DeBruijnNode * newNode, MyGraphicsScene * scene);
    static bool canAddNodeToStartOfMergeList(QList<DeBruijnNode *> * mergeList,
                                      DeBruijnNode * potentialNode);
    static bool canAddNodeToEndOfMergeList(QList<DeBruijnNode *> * mergeList,
                                    DeBruijnNode * potentialNode);
    static void mergeGraphicsNodes(QList<DeBruijnNode *> * originalNodes,
                            QList<DeBruijnNode *> * revCompOriginalNodes,
                            DeBruijnNode * newNode, MyGraphicsScene * scene);
    static bool mergeGraphicsNodes2(QList<DeBruijnNode *> * originalNodes,
                             DeBruijnNode * newNode, MyGraphicsScene * scene);
    static void removeAllGraphicsEdgesFromNode(DeBruijnNode * node,
                                        bool reverseComplement,
                                        MyGraphicsScene * scene);
    QString cleanNodeName(QString name);
    static double findDepthAtIndex(QList<DeBruijnNode *> * nodeList, long long targetIndex) ;

signals:
    void setMergeTotalCount(int totalCount);
    void setMergeCompletedCount(int completedCount);
};
