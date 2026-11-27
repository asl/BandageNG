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
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

class DeBruijnNode;
class DeBruijnEdge;
class GraphicsItemNode;
class MyProgressDialog;
class BandageGraphicsScene;

class AssemblyGraphError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class AssemblyGraphBuilder;

// One pass over the node trie. Both signed names store the same pointer.
// A key ending in '-' is the negative name, so it is skipped.
class NodeIterator
        : public bandage::iterator_facade_base<NodeIterator,
                                            std::forward_iterator_tag,
                                            DeBruijnNode *,
                                            std::ptrdiff_t,
                                            DeBruijnNode **,
                                            DeBruijnNode *> {
    using TrieIt = tsl::htrie_map<char, DeBruijnNode *>::const_iterator;
    TrieIt m_it{};
    TrieIt m_end{};

    void skip() {
        while (m_it != m_end && (m_it.key_back() == '-' || *m_it == nullptr))
            ++m_it;
    }

public:
    NodeIterator() = default;
    NodeIterator(TrieIt it, TrieIt end) : m_it(it), m_end(end) { skip(); }

    bool operator==(const NodeIterator &rhs) const { return m_it == rhs.m_it; }
    DeBruijnNode *operator*() const { return *m_it; }
    NodeIterator &operator++() {
        ++m_it;
        skip();
        return *this;
    }
};

// The positive orientation of each node. A palindrome is that one orientation.
class CanonicalNodeIterator
        : public bandage::iterator_facade_base<CanonicalNodeIterator,
                                            std::forward_iterator_tag,
                                            OrientedNode,
                                            std::ptrdiff_t,
                                            const OrientedNode *,
                                            OrientedNode> {
    NodeIterator m_it{};

public:
    CanonicalNodeIterator() = default;
    explicit CanonicalNodeIterator(NodeIterator it) : m_it(it) {}

    bool operator==(const CanonicalNodeIterator &rhs) const { return m_it == rhs.m_it; }
    OrientedNode operator*() const { return OrientedNode(*m_it); }
    CanonicalNodeIterator &operator++() {
        ++m_it;
        return *this;
    }
};

// Positive, then the distinct reverse. A palindrome is yielded once.
class OrientedNodeIterator
        : public bandage::iterator_facade_base<OrientedNodeIterator,
                                            std::forward_iterator_tag,
                                            OrientedNode,
                                            std::ptrdiff_t,
                                            const OrientedNode *,
                                            OrientedNode> {
    NodeIterator m_it{};
    bool m_negative = false;

public:
    OrientedNodeIterator() = default;
    explicit OrientedNodeIterator(NodeIterator it) : m_it(it) {}

    bool operator==(const OrientedNodeIterator &rhs) const {
        return m_it == rhs.m_it && m_negative == rhs.m_negative;
    }
    OrientedNode operator*() const {
        OrientedNode positive(*m_it);
        return m_negative ? positive.reverse() : positive;
    }
    OrientedNodeIterator &operator++() {
        if (!m_negative && *m_it != nullptr && !(*m_it)->isSelfComplement()) {
            m_negative = true;
            return *this;
        }
        m_negative = false;
        ++m_it;
        return *this;
    }
};

enum NodeNameStatus {NODE_NAME_OKAY, NODE_NAME_TAKEN, NODE_NAME_EMPTY,
    NODE_NAME_CONTAINS_TAB, NODE_NAME_CONTAINS_NEWLINE, NODE_NAME_CONTAINS_COMMA,
    NODE_NAME_CONTAINS_SPACE, NODE_NAME_ENDS_WITH_SIGN};
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

    // Each node once. The trie is not modified through this range.
    auto nodes() const {
        return bandage::make_range(
                NodeIterator(m_deBruijnGraphNodes.begin(), m_deBruijnGraphNodes.end()),
                NodeIterator(m_deBruijnGraphNodes.end(), m_deBruijnGraphNodes.end()));
    }
    auto canonical_nodes() const {
        return bandage::make_range(CanonicalNodeIterator(nodes().begin()),
                                   CanonicalNodeIterator(nodes().end()));
    }
    auto oriented_nodes() const {
        return bandage::make_range(OrientedNodeIterator(nodes().begin()),
                                   OrientedNodeIterator(nodes().end()));
    }

    using DeBruijnLink = std::pair<OrientedNode, OrientedNode>;

    // Edges are stored in a set for fast removal
    phmap::parallel_flat_hash_set<DeBruijnEdge *> m_deBruijnGraphEdges;

    // Custom colors
    phmap::parallel_flat_hash_map<OrientedNode, QColor> m_nodeColors;
    // Custom labels
    phmap::parallel_flat_hash_map<OrientedNode, QString> m_nodeLabels;
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
    // Empty until the reverse direction is drawn. The stored direction's item stays on the edge.
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, GraphicsItemEdge *> m_reverseEdgeGraphics;
    // Graphics item for the negative node orientation. Empty in single mode,
    // which draws one item and keeps it on the node.
    phmap::parallel_flat_hash_map<const DeBruijnNode *, GraphicsItemNode *> m_negativeNodeGraphics;
    // Overlap text for an overlap that is not a single length. An exact
    // overlap is identified by its length and has no entry here.
    phmap::parallel_flat_hash_map<const DeBruijnEdge *, std::string> m_overlapKeys;

    // CSV data
    phmap::parallel_flat_hash_map<OrientedNode, QStringList> m_nodeCSVData;
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
    MadeLink makeLink(OrientedNode from, OrientedNode to,
                      int overlap, EdgeOverlapType overlapType,
                      bool allowDistinctOverlap,
                      std::string_view overlapKey = {});
    // Strand from the key suffix. Both keys point at the one node.
    OrientedNode orientedNode(std::string_view signedName) const;
    GraphicsItemNode *negativeNodeGraphics(const DeBruijnNode *node) const;
    void setNegativeNodeGraphics(DeBruijnNode *node, GraphicsItemNode *item);
    void createDeBruijnEdge(std::string_view node1Name, std::string_view node2Name,
                            int overlap = 0,
                            EdgeOverlapType overlapType = UNKNOWN_OVERLAP);
    void resetNodes();
    void resetEdges();
    // Drop graphics pointers. Drawn flags stay, so a scene rebuild can
    // create items for edges that are already marked drawn.
    void clearEdgeGraphicsItems();
    // Null the positive item and the negative side map. Drawn flags stay,
    // so a scene rebuild can create items for nodes that are already drawn.
    void clearNodeGraphicsItems();
    double getMeanDepth(bool drawnNodesOnly = false);
    static double getMeanDepth(const std::vector<OrientedNode> &nodes);

    void determineGraphInfo();
    void clearGraphInfo();

    void recalculateAllNodeWidths(double averageNodeWidth,
                                  double depthPower, double depthEffectOnWidth);

    bandage::Expected<GraphLoadInfo> loadGraphFromFile(const QString& filename);
    void markNodesToDraw(const graph::Scope &scope,
                         const std::vector<OrientedNode> &startingNodes = {});

    bool loadCSV(const QString& filename, QStringList *columns, QString *errormsg, bool *coloursLoaded);

    static bool checkIfStringHasNodes(QString nodesString);
    static std::string generateNodesNotFoundErrorMessage(const std::vector<QString> &nodesNotInGraph,
                                                         bool exact);
    std::pair<OrientedNode, OrientedNode> getNodes(const QString &nodeName) const;

    std::vector<OrientedNode> getNodesFromStringList(QString nodeNamesString,
                                                     bool exactMatch,
                                                     std::vector<QString> * nodesNotInGraph = nullptr) const;

    void setAllEdgesExactOverlap(int overlap);
    void autoDetermineAllEdgesExactOverlap();

    int getDrawnNodeCount() const;
    void deleteNodes(const std::vector<OrientedNode> &nodes);
    void deleteEdges(const std::vector<DeBruijnEdge *> &edges);
    // Copy into a vector, then use the overload above. That overload is not a
    // candidate here, so the call cannot re-enter this template. A braced list
    // has no type to deduce and uses the vector overload directly.
    template <typename Range>
    std::enable_if_t<!std::is_same_v<std::decay_t<Range>, std::vector<OrientedNode>>>
    deleteNodes(Range &&nodes);
    template <typename Range>
    std::enable_if_t<!std::is_same_v<std::decay_t<Range>, std::vector<DeBruijnEdge *>>>
    deleteEdges(Range &&edges);
    // Links made while this node was a placeholder treated the two
    // strand names as different ends. Once the sequence is its own
    // reverse complement those ends are one orientation, and the extra
    // edges are the same link.
    void collapseSelfComplementEdges(DeBruijnNode *node);
    // Call while the node still has two orientations. Negative colour, label,
    // and CSV entries move onto the positive key before the hash changes.
    void rekeySelfComplementRecords(DeBruijnNode *node);
    void duplicateNodePair(OrientedNode node, BandageGraphicsScene *scene);
    bool mergeNodes(QList<OrientedNode> nodes, BandageGraphicsScene *scene);

    int mergeAllPossible(BandageGraphicsScene *scene = 0,
                         MyProgressDialog *progressDialog = 0);

    void changeNodeName(const QString& oldName, const QString& newName);
    NodeNameStatus checkNodeNameValidity(const QString& nodeName) const;
    void changeNodeDepth(const std::vector<OrientedNode> &nodes,
                         double newDepth);

    unsigned int getDeadEndCount() const;
    void getNodeStats(int *n50, int *shortestNode, int *firstQuartile, int *median, int *thirdQuartile, int *longestNode) const;
    void getGraphComponentCountAndLargestComponentSize(int *componentCount, int *largestComponentLength) const;
    double getMedianDepthByBase() const;

    struct StorageAccount {
        size_t nodes = 0;
        uint64_t nodeBytes = 0;
        size_t nodesWithoutSequence = 0;
        // Name characters that did not fit in the node. Short names are in nodeBytes.
        uint64_t nodeNameBytes = 0;
        // Sequence heap allocations, the header included. Inline sequences are in nodeBytes.
        uint64_t sequenceBytes = 0;
        // Edge pointers that spilled out of the node.
        uint64_t incidentEdgeBytes = 0;
        // Signed-name keys plus the pointer stored under each key.
        uint64_t nodeIndexBytes = 0;

        size_t edges = 0;
        uint64_t edgeBytes = 0;
        // Pointers in the edge set. Bucket overhead is not visible from here.
        uint64_t edgeIndexBytes = 0;

        size_t paths = 0;
        uint64_t pathBytes = 0;
        size_t walks = 0;
        uint64_t walkBytes = 0;

        // Tags, overlap text, labels, CSV, and custom colours. Zero on a plain graph.
        uint64_t annotationBytes = 0;

        uint64_t accountedBytes() const {
            return nodeBytes + nodeNameBytes + sequenceBytes + incidentEdgeBytes
                   + nodeIndexBytes + edgeBytes + edgeIndexBytes + pathBytes
                   + walkBytes + annotationBytes;
        }
    };

    StorageAccount storageAccount() const;

    long long getEstimatedSequenceLength(double medianDepthByBase) const;
    long long getTotalLengthMinusEdgeOverlaps() const;
    QPair<int, int> getOverlapRange() const;
    long long getTotalLengthOrphanedNodes() const;

    bool hasCustomColour(OrientedNode node) const;
    bool hasCustomColour(const DeBruijnEdge *edge, bool reverse = false) const;
    QColor getCustomColour(OrientedNode node) const;
    QColor getCustomColour(const DeBruijnEdge *edge, bool reverse = false) const;
    void setCustomColour(OrientedNode node, QColor color);
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

    QString getCustomLabel(OrientedNode node) const;
    void setCustomLabel(OrientedNode node, QString newLabel);

    bool hasCsvData(OrientedNode node) const;
    QStringList getAllCsvData(OrientedNode node) const;
    std::optional<QString> getCsvLine(OrientedNode node, int i) const;
    void setCsvData(OrientedNode node, QStringList csvData);
    void clearCsvData(OrientedNode node);

    QColor getCustomColourForDisplay(OrientedNode node) const;
    QStringList getCustomLabelForDisplay(OrientedNode node) const;

    std::string getUniqueNodeName(std::string_view baseName) const;
    QString getNodeNameFromString(QString string) const;

    std::vector<OrientedNode> getNodesInDepthRange(double min, double max) const;
private:
    void eraseNodeRecords(const DeBruijnNode *node);
    void eraseEdgeRecords(const DeBruijnEdge *edge);
    // Edges are already unique. deleteEdges builds that set for a vector.
    void deleteEdgeSet(const phmap::flat_hash_set<DeBruijnEdge *> &edges);

    std::vector<OrientedNode> getNodesFromListExact(const QStringList& nodesList, std::vector<QString> * nodesNotInGraph) const;
    std::vector<OrientedNode> getNodesFromListPartial(const QStringList& nodesList, std::vector<QString> * nodesNotInGraph) const;
    std::vector<int> makeOverlapCountVector();
    void clearAllCsvData();
    std::string getNewNodeName(std::string_view oldNodeName) const;

signals:
    void setMergeTotalCount(int totalCount);
    void setMergeCompletedCount(int completedCount);
};

template <typename Range>
std::enable_if_t<!std::is_same_v<std::decay_t<Range>, std::vector<OrientedNode>>>
AssemblyGraph::deleteNodes(Range &&nodes) {
    deleteNodes(std::vector<OrientedNode>(std::begin(nodes), std::end(nodes)));
}

template <typename Range>
std::enable_if_t<!std::is_same_v<std::decay_t<Range>, std::vector<DeBruijnEdge *>>>
AssemblyGraph::deleteEdges(Range &&edges) {
    deleteEdges(std::vector<DeBruijnEdge *>(std::begin(edges), std::end(edges)));
}
