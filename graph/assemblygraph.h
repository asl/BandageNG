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

#include "debruijnedge.h"
#include "path.h"
#include "annotation.h"
#include "graphscope.h"

#include "io/gfa.h"

#include "program/llvm.h"
#include "seq/sequence.hpp"
#include "parallel_hashmap/phmap.h"
#include "tsl/htrie_map.h"

#include <QString>
#include <QPair>
#include <QObject>
#include <string>
#include <string_view>
#include <vector>

class DeBruijnNode;
class DeBruijnEdge;
class MyProgressDialog;
class BandageGraphicsScene;

class AssemblyGraphError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class AssemblyGraphBuilder;

enum NodeNameStatus {NODE_NAME_OKAY, NODE_NAME_TAKEN, NODE_NAME_EMPTY,
    NODE_NAME_CONTAINS_TAB, NODE_NAME_CONTAINS_NEWLINE, NODE_NAME_CONTAINS_COMMA,
    NODE_NAME_CONTAINS_SPACE};
enum SequencesLoadedFromFasta {NOT_READY, NOT_TRIED, TRIED};

struct GraphLoadInfo {
    bool complexOverlaps = false;
};

class AssemblyGraph : public QObject
{
    Q_OBJECT

    friend class AssemblyGraphBuilder;

public:
    AssemblyGraph();
    ~AssemblyGraph() override;

    //Nodes are stored in a map with a key of the node's name.
    tsl::htrie_map<char, DeBruijnNode *> m_deBruijnGraphNodes;

    using DeBruijnLink = std::pair<DeBruijnNode *, DeBruijnNode *>;

    // Edges are stored in a set for fast removal
    phmap::parallel_flat_hash_set<DeBruijnEdge *> m_deBruijnGraphEdges;

    // Custom colors
    phmap::parallel_flat_hash_map<const DeBruijnNode *, QColor> m_nodeColors;
    // Custom labels
    phmap::parallel_flat_hash_map<const DeBruijnNode *, QString> m_nodeLabels;
    // Edge styles
    struct EdgeStyle {
        float width;
        Qt::PenStyle lineStyle;

        EdgeStyle();
    };
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, EdgeStyle> m_edgeStyles;
    // Edge colors
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, QColor> m_edgeColors;
    // Colour of the reverse direction. Empty unless that direction sets one.
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, QColor> m_edgeReverseColors;
    // Graphics item for the reverse direction. Empty until that direction is drawn.
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, GraphicsItemEdge *> m_reverseGraphicsItems;
    // Overlap text for an overlap that is not a single length. An exact
    // overlap is identified by its length and has no entry here.
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, std::string> m_overlapKeys;

    // CSV data
    phmap::parallel_flat_hash_map<const DeBruijnNode *, QStringList> m_nodeCSVData;
    QStringList m_csvHeaders;
    // Tags
    phmap::parallel_flat_hash_map<const DeBruijnNode *, std::vector<gfa::tag>> m_nodeTags;
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, std::vector<gfa::tag>> m_edgeTags;

    // Paths
    tsl::htrie_map<char, Path> m_deBruijnGraphPaths;

    // Walks
    tsl::htrie_map<char, Walk> m_deBruijnGraphWalks;

    int m_nodeCount;
    int m_edgeCount;
    unsigned pathCount() const { return m_deBruijnGraphPaths.size(); }
    unsigned walkCount() const { return m_deBruijnGraphWalks.size(); }

    long long m_totalLength;
    long long m_shortestContig;
    long long m_longestContig;
    double m_meanDepth;
    double m_firstQuartileDepth;
    double m_medianDepth;
    double m_thirdQuartileDepth;
    QString m_filename;
    QString m_depthTag;
    SequencesLoadedFromFasta m_sequencesLoadedFromFasta;

    void cleanUp();
    struct MadeLink {
        DeBruijnEdge *edge = nullptr;
        // Set when the requested ends are the opposite direction from the stored ones.
        bool reversed = false;
        bool created = false;
    };

    // One object, stored in the positive orientation. A repeat of either
    // direction with the same overlap and type returns the existing edge.
    // When allowDistinctOverlap is false, any existing connection is reused.
    // overlapKey distinguishes overlaps that share a numeric length of 0.
    MadeLink makeLink(DeBruijnNode *from, DeBruijnNode *to,
                      int overlap, EdgeOverlapType overlapType,
                      bool allowDistinctOverlap,
                      std::string_view overlapKey = {});
    void createDeBruijnEdge(std::string_view node1Name, std::string_view node2Name,
                            int overlap = 0,
                            EdgeOverlapType overlapType = UNKNOWN_OVERLAP);
    void resetNodes();
    void resetEdges();
    // Drop graphics pointers. Drawn flags stay, so a scene rebuild can
    // create items for edges that are already marked drawn.
    void clearEdgeGraphicsItems();
    double getMeanDepth(bool drawnNodesOnly = false);
    static double getMeanDepth(const std::vector<DeBruijnNode *> &nodes);

    void determineGraphInfo();
    void clearGraphInfo();

    void recalculateAllNodeWidths(double averageNodeWidth,
                                  double depthPower, double depthEffectOnWidth);

    bandage::Expected<GraphLoadInfo> loadGraphFromFile(const QString& filename);
    void markNodesToDraw(const graph::Scope &scope,
                         const std::vector<DeBruijnNode *>& startingNodes = {});

    bool loadCSV(const QString& filename, QStringList *columns, QString *errormsg, bool *coloursLoaded);

    static bool checkIfStringHasNodes(QString nodesString);
    static std::string generateNodesNotFoundErrorMessage(const std::vector<QString> &nodesNotInGraph,
                                                         bool exact);
    std::pair<DeBruijnNode *, DeBruijnNode *> getNodes(const QString &nodeName) const;

    std::vector<DeBruijnNode *> getNodesFromStringList(QString nodeNamesString,
                                                       bool exactMatch,
                                                       std::vector<QString> * nodesNotInGraph = nullptr) const;

    void setAllEdgesExactOverlap(int overlap);
    void autoDetermineAllEdgesExactOverlap();

    int getDrawnNodeCount() const;
    void deleteNodes(const std::vector<DeBruijnNode *> &nodes);
    void deleteEdges(const std::vector<DeBruijnEdge *> &edges);
    void duplicateNodePair(DeBruijnNode *node, BandageGraphicsScene *scene);
    bool mergeNodes(QList<DeBruijnNode *> nodes, BandageGraphicsScene *scene);

    int mergeAllPossible(BandageGraphicsScene *scene = 0,
                         MyProgressDialog *progressDialog = 0);

    void changeNodeName(const QString& oldName, const QString& newName);
    NodeNameStatus checkNodeNameValidity(const QString& nodeName) const;
    void changeNodeDepth(const std::vector<DeBruijnNode *> &nodes,
                         double newDepth);

    unsigned int getDeadEndCount() const;
    void getNodeStats(int *n50, int *shortestNode, int *firstQuartile, int *median, int *thirdQuartile, int *longestNode) const;
    void getGraphComponentCountAndLargestComponentSize(int *componentCount, int *largestComponentLength) const;
    double getMedianDepthByBase() const;

    struct StorageAccount {
        size_t orientedNodes = 0;
        size_t segments = 0;
        // Bytes of the unsigned name stored on the positive node. The negative
        // node stores none, so this is also the per-segment total. The index is separate.
        uint64_t nameBytes = 0;
        uint64_t segmentNameBytes = 0;
        uint64_t nodeIndexKeyBytes = 0;
        // Packed 2-bit buffers, counted once per segment (the two strands share one).
        uint64_t packedSequenceBytes = 0;
        size_t missingSegments = 0;
        uint64_t missingSequenceBases = 0;
        // One stored edge per link.
        size_t edges = 0;
        size_t paths = 0;
        uint64_t pathNameBytes = 0;
        uint64_t pathSteps = 0;
        size_t walks = 0;
        uint64_t walkKeyBytes = 0;
        uint64_t walkSampleBytes = 0;
        uint64_t walkSteps = 0;
    };

    StorageAccount storageAccount() const;

    long long getEstimatedSequenceLength(double medianDepthByBase) const;
    long long getTotalLengthMinusEdgeOverlaps() const;
    QPair<int, int> getOverlapRange() const;
    long long getTotalLengthOrphanedNodes() const;

    bool hasCustomColour(const DeBruijnNode *node) const;
    bool hasCustomColour(const DeBruijnEdge *edge, bool reverse = false) const;
    QColor getCustomColour(const DeBruijnNode *node) const;
    QColor getCustomColour(const DeBruijnEdge *edge, bool reverse = false) const;
    void setCustomColour(const DeBruijnNode *node, QColor color);
    void setCustomColour(const DeBruijnEdge *edge, QColor color, bool reverse = false);

    GraphicsItemEdge *reverseGraphicsItem(const DeBruijnEdge *edge) const;
    void setReverseGraphicsItem(const DeBruijnEdge *edge, GraphicsItemEdge *item);
    // The graphics item for one direction. Clears that pointer.
    GraphicsItemEdge *takeGraphicsItem(DeBruijnEdge *edge, bool reversed);

    EdgeStyle getCustomStyle(const DeBruijnEdge *edge) const;
    void setCustomStyle(const DeBruijnEdge *edge, Qt::PenStyle lineStyle);
    void setCustomStyle(const DeBruijnEdge *edge, float width);
    void setCustomStyle(const DeBruijnEdge *edge, EdgeStyle style);
    bool hasCustomStyle(const DeBruijnEdge *edge) const;

    QString getCustomLabel(const DeBruijnNode *node) const;
    void setCustomLabel(const DeBruijnNode *node, QString newLabel);

    bool hasCsvData(const DeBruijnNode *node) const;
    QStringList getAllCsvData(const DeBruijnNode *node) const;
    std::optional<QString> getCsvLine(const DeBruijnNode *node, int i) const;
    void setCsvData(const DeBruijnNode *node, QStringList csvData);
    void clearCsvData(const DeBruijnNode *node);

    QColor getCustomColourForDisplay(const DeBruijnNode *node) const;
    QStringList getCustomLabelForDisplay(const DeBruijnNode *node) const;

    std::string getUniqueNodeName(std::string_view baseName) const;
    QString getNodeNameFromString(QString string) const;

    std::vector<DeBruijnNode *> getNodesInDepthRange(double min, double max) const;
private:
    void eraseNodeRecords(const DeBruijnNode *node);
    void eraseEdgeRecords(const DeBruijnEdge *edge);

    std::vector<DeBruijnNode *> getNodesFromListExact(const QStringList& nodesList, std::vector<QString> * nodesNotInGraph) const;
    std::vector<DeBruijnNode *> getNodesFromListPartial(const QStringList& nodesList, std::vector<QString> * nodesNotInGraph) const;
    std::vector<int> makeOverlapCountVector();
    void clearAllCsvData();
    std::string getNewNodeName(std::string_view oldNodeName) const;

signals:
    void setMergeTotalCount(int totalCount);
    void setMergeCompletedCount(int completedCount);
};
