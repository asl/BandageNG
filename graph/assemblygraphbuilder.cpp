// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage-NG is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage-NG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#include "assemblygraphbuilder.h"
#include "path.h"
#include "gfa.h"

#include "graph/assemblygraph.h"
#include "graph/debruijnedge.h"
#include "graph/debruijnnode.h"
#include "graph/fileutils.h"

#include "seq/sequence.hpp"

#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QString>
#include <QRegularExpression>
#include <memory>

#include <zlib.h>

static bool checkFirstLineOfFile(const QString& fullFileName, const QString& regExp) {
    QFile inputFile(fullFileName);
    if (inputFile.open(QIODevice::ReadOnly)) {
        QTextStream in(&inputFile);
        if (in.atEnd())
            return false;

        QRegularExpression rx(regExp);
        QString line = in.readLine();
        if (rx.match(line).hasMatch())
            return true;
    }
    return false;
}

//Cursory look to see if file appears to be a FASTG file.
static bool checkFileIsFastG(const QString& fullFileName) {
    return checkFirstLineOfFile(fullFileName, "^>(NODE|EDGE).*;");
}

//Cursory look to see if file appears to be a FASTA file.
static bool checkFileIsFasta(const QString& fullFileName) {
    return checkFirstLineOfFile(fullFileName, "^>");
}

//Cursory look to see if file appears to be a GFA file.
static bool checkFileIsGfa(const QString& fullFileName) {
    QFileInfo gfaFileInfo(fullFileName);
    if (gfaFileInfo.isFile()) {
        return fullFileName.endsWith(".gfa") ||
               fullFileName.endsWith(".gfa.gz");
    }

    return false;
}

//Cursory look to see if file appears to be a Trinity.fasta file.
static bool checkFileIsTrinityFasta(const QString& fullFileName) {
    return checkFirstLineOfFile(fullFileName, "path=\\[");
}

//Cursory look to see if file appears to be an ASQG file.
static bool checkFileIsAsqg(const QString& fullFileName) {
    return checkFirstLineOfFile(fullFileName, "^HT\t");
}

static ssize_t gzgetdelim(char **buf, size_t *bufsiz, int delimiter, gzFile fp) {
    char *ptr, *eptr;

    if (*buf == NULL || *bufsiz == 0) {
        *bufsiz = BUFSIZ;
        if ((*buf = (char*)malloc(*bufsiz)) == NULL)
            return -1;
    }

    for (ptr = *buf, eptr = *buf + *bufsiz;;) {
        char c = gzgetc(fp);
        if (c == -1) {
            if (gzeof(fp)) {
                ssize_t diff = (ssize_t) (ptr - *buf);
                if (diff != 0) {
                    *ptr = '\0';
                    return diff;
                }
            }
            return -1;
        }
        *ptr++ = c;
        if (c == delimiter) {
            *ptr = '\0';
            return ptr - *buf;
        }
        if (ptr + 2 >= eptr) {
            char *nbuf;
            size_t nbufsiz = *bufsiz * 2;
            ssize_t d = ptr - *buf;
            if ((nbuf = (char*)realloc(*buf, nbufsiz)) == NULL)
                return -1;

            *buf = nbuf;
            *bufsiz = nbufsiz;
            eptr = nbuf + nbufsiz;
            ptr = nbuf + d;
        }
    }
}

static ssize_t gzgetline(char **buf, size_t *bufsiz, gzFile fp) {
    return gzgetdelim(buf, bufsiz, '\n', fp);
}


static std::string getOppositeNodeName(std::string nodeName) {
    return (nodeName.back() == '-' ?
            nodeName.substr(0, nodeName.size() - 1) + '+' :
            nodeName.substr(0, nodeName.size() - 1) + '-');
}

//This function will look to see if there is a FASTA file (.fa or .fasta) with
//the same base name as the graph. If so, it will load it and give its
//sequences to the graph nodes with matching names. This is useful for GFA
//files which have no sequences (just '*') like ABySS makes.
//Returns true if any sequences were loaded (doesn't have to be all sequences
//in the graph).
static bool attemptToLoadSequencesFromFasta(AssemblyGraph &graph) {
    if (graph.m_sequencesLoadedFromFasta == NOT_READY ||
        graph.m_sequencesLoadedFromFasta == TRIED)
        return false;

    graph.m_sequencesLoadedFromFasta = TRIED;

    QFileInfo gfaFileInfo(graph.m_filename);
    QString baseName = gfaFileInfo.completeBaseName();
    QString fastaName = gfaFileInfo.dir().filePath(baseName + ".fa");
    QFileInfo fastaFileInfo(fastaName);
    if (!fastaFileInfo.exists()) {
        fastaName = gfaFileInfo.dir().filePath(baseName + ".fasta");
        fastaFileInfo = QFileInfo(fastaName);
    }
    if (!fastaFileInfo.exists()) {
        fastaName = gfaFileInfo.dir().filePath(baseName + ".contigs.fasta");
        fastaFileInfo = QFileInfo(fastaName);
    }
    if (!fastaFileInfo.exists())
        return false;

    bool atLeastOneNodeSequenceLoaded = false;
    std::vector<QString> names;
    std::vector<QByteArray> sequences;
    utils::readFastaFile(fastaName, &names, &sequences);

    for (size_t i = 0; i < names.size(); ++i) {
        QString name = names[i];
        name = name.split(QRegularExpression("\\s+"))[0];
        if (graph.m_deBruijnGraphNodes.count((name + "+").toStdString())) {
            DeBruijnNode * posNode = graph.m_deBruijnGraphNodes[(name + "+").toStdString()];
            if (posNode->sequenceIsMissing()) {
                Sequence sequence{sequences[i]};
                atLeastOneNodeSequenceLoaded = true;
                posNode->setSequence(sequence);
                DeBruijnNode * negNode = graph.m_deBruijnGraphNodes[(name + "-").toStdString()];
                negNode->setSequence(sequence.GetReverseComplement());
            }
        }
    }

    return atLeastOneNodeSequenceLoaded;
}

class GFAAssemblyGraphBuilder : public AssemblyGraphBuilder {
  private:
    static constexpr unsigned makeTag(const char name[2]) {
        return (unsigned)name[0] << 8 | name[1];
    }

    static bool isStandardTag(const char name[2]) {
        switch (makeTag(name)) {
            case makeTag("DP"):
            case makeTag("LN"):
            case makeTag("KC"):
            case makeTag("FC"):
            case makeTag("RC"):
            case makeTag("LB"):
            case makeTag("L2"):
            case makeTag("CB"):
            case makeTag("C2"):
                return true;
        }

        return false;
    }


    static DeBruijnNode *maybeAddSegment(const std::string &nodeName,
                                  double nodeDepth, Sequence sequence,
                                  AssemblyGraph &graph) {
        auto nodeStorage = graph.m_deBruijnGraphNodes.find(nodeName);
        // If node already exists it should be a placeholder of zero length
        if (nodeStorage != graph.m_deBruijnGraphNodes.end()) {
            DeBruijnNode *placeholder = nodeStorage.value();
            if (!placeholder->getSequence().empty())
                return nullptr;

            // Takeover the placeholder
            placeholder->setDepth(nodeDepth);
            placeholder->setSequence(sequence);

            return placeholder;
        }

        return (graph.m_deBruijnGraphNodes[nodeName] = new DeBruijnNode(nodeName.c_str(), nodeDepth, sequence));
    }

    static auto
    addSegmentPair(const std::string &nodeName,
                   double nodeDepth, Sequence sequence,
                   AssemblyGraph &graph) {
        std::string oppositeNodeName = getOppositeNodeName(nodeName);

        auto *nodePtr = maybeAddSegment(nodeName, nodeDepth, sequence, graph);
        if (!nodePtr)
            throw AssemblyGraphError("Duplicate segment named: " + nodeName);

        auto oppositeNodePtr =
                maybeAddSegment(getOppositeNodeName(nodeName), nodeDepth, sequence.GetReverseComplement(), graph);
        if (!oppositeNodePtr)
            throw AssemblyGraphError("Duplicate segment named: " + oppositeNodeName);

        nodePtr->setReverseComplement(oppositeNodePtr);
        oppositeNodePtr->setReverseComplement(nodePtr);

        return std::make_pair(nodePtr, oppositeNodePtr);
    }

    // Add placeholder
    static auto
    addSegmentPair(const std::string &nodeName,
                   AssemblyGraph &graph) {
        return addSegmentPair(nodeName, 0, Sequence(), graph);
    }

    template<class Container, class Key>
    static void maybeAddTags(Key k, Container &c,
                             const std::vector<gfa::tag> &tags,
                             bool ignoreStandard = true) {
        bool tagsInserted = false;
        for (const auto& tag : tags) {
            if (ignoreStandard && isStandardTag(tag.name))
                continue;
            c[k].push_back(tag);
            tagsInserted = true;
        }

        if (tagsInserted)
            c[k].shrink_to_fit();
    }

    template<class Entity>
    static bool maybeAddCustomColor(const Entity *e,
                                    const std::vector<gfa::tag> &tags,
                                    const char *tag,
                                    AssemblyGraph &graph) {
        if (auto cb = getTag<std::string>(tag, tags)) {
            graph.setCustomColour(e, cb->c_str());
            return true;
        }

        return false;
    }

    bool handleSegment(const gfa::segment &record,
                       AssemblyGraph &graph) {
        bool sequencesAreMissing = false;

        std::string nodeName{record.name};
        const auto &seq = record.seq;

        // We check to see if the node ended in a "+" or "-".
        // If so, we assume that is giving the orientation and leave it.
        // And if it doesn't end in a "+" or "-", we assume "+" and add
        // that to the node name.
        if (nodeName.back() != '+' && nodeName.back() != '-')
            nodeName.push_back('+');

        // GFA can use * to indicate that the sequence is not in the
        // file.  In this case, try to use the LN tag for length.
        // If there is a sequence, then the LN tag will be ignored.
        size_t length = seq.size();
        Sequence sequence;
        if (!length ||
            length == 1 && seq.starts_with('*')) {
            auto lnTag = getTag<int64_t>("LN", record.tags);
            if (lnTag)
                length = size_t(*lnTag);

            sequencesAreMissing = true;
            sequence = Sequence(length, /* allNs */ true);
        } else
            sequence = Sequence{seq};

        double nodeDepth = 0;
        if (auto dpTag = getTag<float>("DP", record.tags)) {
            graph.m_depthTag = "DP";
            nodeDepth = *dpTag;
        } else if (auto kcTag = getTag<int64_t>("KC", record.tags)) {
            graph.m_depthTag = "KC";
            nodeDepth = double(*kcTag) / double(length);
        } else if (auto rcTag = getTag<int64_t>("RC", record.tags)) {
            graph.m_depthTag = "RC";
            nodeDepth = double(*rcTag) / double(length);
        } else if (auto fcTag = getTag<int64_t>("FC", record.tags)) {
            graph.m_depthTag = "FC";
            nodeDepth = double(*fcTag) / double(length);
        }

        // FIXME: get rid of copies and QString's
        auto [nodePtr, oppositeNodePtr] = addSegmentPair(nodeName, nodeDepth, sequence, graph);

        auto lb = getTag<std::string>("LB", record.tags);
        auto l2 = getTag<std::string>("L2", record.tags);
        hasCustomLabels_ = hasCustomLabels_ || lb || l2;
        if (lb) graph.setCustomLabel(nodePtr, lb->c_str());
        if (l2) graph.setCustomLabel(oppositeNodePtr, l2->c_str());

        hasCustomColours_ |= maybeAddCustomColor(nodePtr, record.tags, "CB", graph);
        hasCustomColours_ |= maybeAddCustomColor(oppositeNodePtr, record.tags, "C2", graph);

        maybeAddTags(nodePtr, graph.m_nodeTags, record.tags);
        maybeAddTags(oppositeNodePtr, graph.m_nodeTags, record.tags);

        return sequencesAreMissing;
    }

    static DeBruijnNode *getNode(const std::string &name,
                                 AssemblyGraph &graph) {
        auto nodeIt = graph.m_deBruijnGraphNodes.find(name);
        if (nodeIt != graph.m_deBruijnGraphNodes.end())
            return *nodeIt;

        // Add placeholder
        DeBruijnNode *nodePtr, *oppositeNodePtr;
        std::tie(nodePtr, oppositeNodePtr) = addSegmentPair(name, graph);

        return nodePtr;
    }

    auto addLink(const std::string &fromNode,
                 const std::string &toNode,
                 const std::vector<gfa::tag> &tags,
                 AssemblyGraph &graph) {
        // Get source / dest nodes (or create placeholders to fill in)
        DeBruijnNode *fromNodePtr = getNode(fromNode, graph);
        DeBruijnNode *toNodePtr = getNode(toNode, graph);

        DeBruijnEdge *edgePtr = nullptr, *rcEdgePtr = nullptr;

        // Ignore dups, hifiasm seems to create them
        if (graph.m_deBruijnGraphEdges.count({fromNodePtr, toNodePtr}))
            return std::make_pair(edgePtr, rcEdgePtr);

        edgePtr = new DeBruijnEdge(fromNodePtr, toNodePtr);

        bool isOwnPair = fromNodePtr == toNodePtr->getReverseComplement() &&
                         toNodePtr == fromNodePtr->getReverseComplement();
        graph.m_deBruijnGraphEdges[{fromNodePtr, toNodePtr}] = edgePtr;
        fromNodePtr->addEdge(edgePtr);
        toNodePtr->addEdge(edgePtr);

        if (isOwnPair) {
            edgePtr->setReverseComplement(edgePtr);
        } else {
            auto *rcFromNodePtr = fromNodePtr->getReverseComplement();
            auto *rcToNodePtr = toNodePtr->getReverseComplement();
            rcEdgePtr = new DeBruijnEdge(rcToNodePtr, rcFromNodePtr);
            rcFromNodePtr->addEdge(rcEdgePtr);
            rcToNodePtr->addEdge(rcEdgePtr);
            edgePtr->setReverseComplement(rcEdgePtr);
            rcEdgePtr->setReverseComplement(edgePtr);
            graph.m_deBruijnGraphEdges[{rcToNodePtr, rcFromNodePtr}] = rcEdgePtr;
        }

        hasCustomColours_ |= maybeAddCustomColor(edgePtr, tags, "CB", graph);
        hasCustomColours_ |= maybeAddCustomColor(rcEdgePtr, tags, "C2", graph);

        maybeAddTags(edgePtr, graph.m_edgeTags, tags,
                     false);
        if (rcEdgePtr)
            maybeAddTags(rcEdgePtr, graph.m_edgeTags, tags,
                         false);

        return std::make_pair(edgePtr, rcEdgePtr);
    }

    void handleLink(const gfa::link &record,
                    AssemblyGraph &graph) {
        std::string fromNode{record.lhs};
        fromNode.push_back(record.lhs_revcomp ? '-' : '+');
        std::string toNode{record.rhs};
        toNode.push_back(record.rhs_revcomp ? '-' : '+');

        auto [edgePtr, rcEdgePtr] =
                addLink(fromNode, toNode, record.tags, graph);
        if (!edgePtr)
            return;

        const auto &overlap = record.overlap;
        size_t overlapLength = 0;
        if (overlap.size() > 1 ||
            (overlap.size() == 1 && overlap.front().op != 'M')) {
            hasComplexOverlaps_ = true;
        } else if (overlap.size() == 1) {
            overlapLength = overlap.front().count;
        }

        edgePtr->setOverlap(static_cast<int>(overlapLength));
        edgePtr->setOverlapType(EXACT_OVERLAP);
        if (rcEdgePtr) {
            rcEdgePtr->setOverlap(edgePtr->getOverlap());
            rcEdgePtr->setOverlapType(edgePtr->getOverlapType());
        }
    }

    void handleGapLink(const gfa::gaplink &record,
                       AssemblyGraph &graph) {
        // FIXME: get rid of severe duplication!
        std::string fromNode{record.lhs};
        fromNode.push_back(record.lhs_revcomp ? '-' : '+');
        std::string toNode{record.rhs};
        toNode.push_back(record.rhs_revcomp ? '-' : '+');

        auto [edgePtr, rcEdgePtr] =
                addLink(fromNode, toNode, record.tags, graph);
        if (!edgePtr)
            return;

        edgePtr->setOverlap(record.distance == std::numeric_limits<int64_t>::min() ? 0 : record.distance);
        edgePtr->setOverlapType(JUMP);
        if (rcEdgePtr) {
            rcEdgePtr->setOverlap(edgePtr->getOverlap());
            rcEdgePtr->setOverlapType(edgePtr->getOverlapType());
        }

        if (!graph.hasCustomColour(edgePtr))
            graph.setCustomColour(edgePtr, "red");
        if (!graph.hasCustomColour(rcEdgePtr))
            graph.setCustomColour(rcEdgePtr, "red");
        if (!graph.hasCustomStyle(edgePtr))
            graph.setCustomStyle(edgePtr, Qt::DashLine);
        if (!graph.hasCustomStyle(rcEdgePtr))
            graph.setCustomStyle(rcEdgePtr, Qt::DashLine);
    }

    void handlePath(const gfa::path &record,
                    AssemblyGraph &graph) {
        QList<DeBruijnNode *> pathNodes;
        pathNodes.reserve(record.segments.size());

        for (const auto &node : record.segments)
            pathNodes.push_back(graph.m_deBruijnGraphNodes.at(node));
        graph.m_deBruijnGraphPaths[record.name] = new Path(Path::makeFromOrderedNodes(pathNodes, false));
    }


  public:
    using AssemblyGraphBuilder::AssemblyGraphBuilder;

    bool build(AssemblyGraph &graph) override {
        graph.m_filename = fileName_;

        bool sequencesAreMissing = false;

        std::unique_ptr<std::remove_pointer<gzFile>::type, decltype(&gzclose)>
                fp(gzopen(fileName_.toStdString().c_str(), "r"), gzclose);
        if (!fp)
            throw AssemblyGraphError("failed to open file: " + fileName_.toStdString());

        size_t i = 0;
        char *line = nullptr;
        size_t len = 0;
        ssize_t read;
        while ((read = gzgetline(&line, &len, fp.get())) != -1) {
            if (read <= 1)
                continue; // skip empty lines

            auto result = gfa::parse_record(line, read - 1);
            if (!result)
                continue;

            std::visit([&](const auto &record) {
                    using T = std::decay_t<decltype(record)>;
                    if constexpr (std::is_same_v<T, gfa::segment>) {
                        sequencesAreMissing |= handleSegment(record, graph);
                    } else if constexpr (std::is_same_v<T, gfa::link>) {
                        handleLink(record, graph);
                    } else if constexpr (std::is_same_v<T, gfa::gaplink>) {
                        handleGapLink(record, graph);
                    } else if constexpr (std::is_same_v<T, gfa::path>) {
                        handlePath(record, graph);
                    }
                },
                *result);
        }

        graph.m_sequencesLoadedFromFasta = NOT_TRIED;
        if (sequencesAreMissing)
            attemptToLoadSequencesFromFasta(graph);

        return true;
    }
};

static void pointEachNodeToItsReverseComplement(AssemblyGraph &graph) {
    for (auto &entry : graph.m_deBruijnGraphNodes) {
        DeBruijnNode * positiveNode = entry;
        if (!positiveNode->isPositiveNode())
            continue;

        if (DeBruijnNode * negativeNode =
            graph.m_deBruijnGraphNodes[getOppositeNodeName(positiveNode->getName().toStdString())]) {
            positiveNode->setReverseComplement(negativeNode);
            negativeNode->setReverseComplement(positiveNode);
        }
    }
}

static void makeReverseComplementNodeIfNecessary(AssemblyGraph &graph, DeBruijnNode *node) {
    auto reverseComplementName = getOppositeNodeName(node->getName().toStdString());
    if (!graph.m_deBruijnGraphNodes.count(reverseComplementName)) {
        Sequence nodeSequence{};
        if (!node->sequenceIsMissing())
            nodeSequence = node->getSequence();
        auto newNode = new DeBruijnNode(reverseComplementName.c_str(), node->getDepth(),
                                        nodeSequence.GetReverseComplement(),
                                        node->getLength());
        graph.m_deBruijnGraphNodes.emplace(reverseComplementName, newNode);
    }
}

class FastaAssemblyGraphBuilder : public AssemblyGraphBuilder {
    using AssemblyGraphBuilder::AssemblyGraphBuilder;

    // This function adjusts a node name to make sure it is valid for use in Bandage.
    [[nodiscard]] static QString cleanNodeName(QString name) {
        //Replace whitespace with underscores
        name = name.replace(QRegularExpression("\\s"), "_");

        //Remove any commas.
        name = name.replace(",", "");

        //Remove any trailing + or -.
        if (name.endsWith('+') || name.endsWith('-'))
            name.chop(1);

        return name;
    }

    bool build(AssemblyGraph &graph) override {
        graph.m_filename = fileName_;
        graph.m_depthTag = "";

        std::vector<QString> names;
        std::vector<QByteArray> sequences;
        utils::readFastaFile(fileName_, &names, &sequences);

        std::vector<QString> circularNodeNames;
        for (size_t i = 0; i < names.size(); ++i) {
            QString name = names[i];
            QString lowerName = name.toLower();
            double depth = 1.0;
            Sequence sequence{sequences[i]};

            // Check to see if the node name matches the Velvet/SPAdes contig
            // format.  If so, we can get the depth and node number.
            QStringList thisNodeDetails = name.split("_");
            if (thisNodeDetails.size() >= 6 && thisNodeDetails[2] == "length" && thisNodeDetails[4] == "cov") {
                name = thisNodeDetails[1];
                depth = thisNodeDetails[5].toDouble();
                graph.m_depthTag = "KC";
            }

            // Check to see if the name matches SKESA format, in which case we can get the depth and node number.
            else if (thisNodeDetails.size() >= 3 && thisNodeDetails[0] == "Contig" && thisNodeDetails[1].toInt() > 0) {
                name = thisNodeDetails[1];
                bool ok;
                double convertedDepth = thisNodeDetails[2].toDouble(&ok);
                if (ok)
                    depth = convertedDepth;
                graph.m_depthTag = "KC";
            }

            // If it doesn't match, then we will use the sequence name up to the first space.
            else {
                QStringList nameParts = name.split(" ");
                if (!nameParts.empty())
                    name = nameParts[0];
            }

            name = cleanNodeName(name);
            name = graph.getUniqueNodeName(name) + "+";

            //  Look for "depth=" and "circular=" in the full header and use them if possible.
            if (lowerName.contains("depth=")) {
                QString depthString = lowerName.split("depth=")[1];
                if (depthString.contains("x"))
                    depthString = depthString.split("x")[0];
                else
                    depthString = depthString.split(" ")[0];
                bool ok;
                double depthFromString = depthString.toFloat(&ok);
                if (ok)
                    depth = depthFromString;
            }
            if (lowerName.contains("circular=true"))
                circularNodeNames.push_back(name);

            // SKESA circularity
            if (thisNodeDetails.size() == 4 and thisNodeDetails[3] == "Circ")
                circularNodeNames.push_back(name);

            if (name.length() < 1)
                throw "load error";

            auto node = new DeBruijnNode(name, depth, sequence);
            graph.m_deBruijnGraphNodes.emplace(name.toStdString(), node);
            makeReverseComplementNodeIfNecessary(graph, node);
        }
        pointEachNodeToItsReverseComplement(graph);

        // For any circular nodes, make an edge connecting them to themselves.
        for (const auto& circularNodeName : circularNodeNames) {
            graph.createDeBruijnEdge(circularNodeName, circularNodeName, 0, EXACT_OVERLAP);
        }

        return true;
    }
};

class FastgAssemblyGraphBuilder : public AssemblyGraphBuilder {
    using AssemblyGraphBuilder::AssemblyGraphBuilder;

    bool build(AssemblyGraph &graph) override {
        graph.m_filename = fileName_;
        graph.m_depthTag = "KC";

        QFile inputFile(fileName_);
        if (inputFile.open(QIODevice::ReadOnly)) {
            std::vector<QString> edgeStartingNodeNames;
            std::vector<QString> edgeEndingNodeNames;
            DeBruijnNode * node = nullptr;
            QByteArray sequenceBytes;

            QTextStream in(&inputFile);
            while (!in.atEnd()) {
                QString nodeName;
                double nodeDepth;

                QString line = in.readLine();

                //If the line starts with a '>', then we are beginning a new node.
                if (line.startsWith(">")) {
                    if (node != nullptr) {
                        node->setSequence(sequenceBytes);
                        sequenceBytes.clear();
                    }
                    line.remove(0, 1); //Remove '>' from start
                    line.chop(1); //Remove ';' from end
                    QStringList nodeDetails = line.split(":");

                    const QString& thisNode = nodeDetails.at(0);

                    //A single quote as the last character indicates a negative node.
                    bool negativeNode = thisNode.at(thisNode.size() - 1) == '\'';

                    QStringList thisNodeDetails = thisNode.split("_");

                    if (thisNodeDetails.size() < 6)
                        throw "load error";

                    nodeName = thisNodeDetails.at(1);
                    if (negativeNode)
                        nodeName += "-";
                    else
                        nodeName += "+";
                    if (graph.m_deBruijnGraphNodes.count(nodeName.toStdString()))
                        throw "load error";

                    QString nodeDepthString = thisNodeDetails.at(5);
                    if (negativeNode) {
                        //It may be necessary to remove a single quote from the end of the depth
                        if (nodeDepthString.at(nodeDepthString.size() - 1) == '\'')
                            nodeDepthString.chop(1);
                    }
                    nodeDepth = nodeDepthString.toDouble();

                    //Make the node
                    node = new DeBruijnNode(nodeName, nodeDepth, {}); //Sequence string is currently empty - will be added to on subsequent lines of the fastg file
                    graph.m_deBruijnGraphNodes.emplace(nodeName.toStdString(), node);

                    //The second part of nodeDetails is a comma-delimited list of edge nodes.
                    //Edges aren't made right now (because the other node might not yet exist),
                    //so they are saved into vectors and made after all the nodes have been made.
                    if (nodeDetails.size() == 1 || nodeDetails.at(1).isEmpty())
                        continue;
                    QStringList edgeNodes = nodeDetails.at(1).split(",");
                    for (auto edgeNode : edgeNodes) {
                        QChar lastChar = edgeNode.at(edgeNode.size() - 1);
                        bool negativeNode = false;
                        if (lastChar == '\'') {
                            negativeNode = true;
                            edgeNode.chop(1);
                        }
                        QStringList edgeNodeDetails = edgeNode.split("_");

                        if (edgeNodeDetails.size() < 2)
                            throw "load error";

                        QString edgeNodeName = edgeNodeDetails.at(1);
                        if (negativeNode)
                            edgeNodeName += "-";
                        else
                            edgeNodeName += "+";

                        edgeStartingNodeNames.push_back(nodeName);
                        edgeEndingNodeNames.push_back(edgeNodeName);
                    }
                }

                //If the line does not start with a '>', then this line is part of the
                //sequence for the last node.
                else {
                    sequenceBytes.push_back(line.simplified().toLocal8Bit());
                }
            }
            if (node != nullptr) {
                node->setSequence(sequenceBytes);
                sequenceBytes.clear();
            }

            inputFile.close();

            // Add fake reverse-complementary nodes for all self-reverse-complement ones
            {
                std::vector<DeBruijnNode*> nodes;
                for (const auto &entry : graph.m_deBruijnGraphNodes) {
                    DeBruijnNode *node = entry;
                    if (!graph.m_deBruijnGraphNodes.count(getOppositeNodeName(node->getName().toStdString())))
                        nodes.emplace_back(node);
                }

                for (auto &entry : nodes)
                    makeReverseComplementNodeIfNecessary(graph,entry);
            }
            pointEachNodeToItsReverseComplement(graph);


            //Create all of the edges.
            for (size_t i = 0; i < edgeStartingNodeNames.size(); ++i) {
                QString node1Name = edgeStartingNodeNames[i];
                QString node2Name = edgeEndingNodeNames[i];
                graph.createDeBruijnEdge(node1Name, node2Name);
            }
        }

        graph.autoDetermineAllEdgesExactOverlap();

        if (graph.m_deBruijnGraphNodes.empty())
            throw "load error";

        return true;
    }
};

// This function builds a graph from an ASQG file.  Bandage expects edges to
// conform to its expectation: overlaps are only at the ends of sequences and
// always have the same length in each of the two sequences.  It will not load
// edges which fail to meet this expectation.  The function's return value is
// the number of edges which could not be loaded.
class AsqgAssemblyGraphBuilder : public AssemblyGraphBuilder {
    using AssemblyGraphBuilder::AssemblyGraphBuilder;

    bool build(AssemblyGraph &graph) override {
        graph.m_filename = fileName_;
        graph.m_depthTag = "";

        int badEdgeCount = 0;

        QFile inputFile(fileName_);
        if (inputFile.open(QIODevice::ReadOnly)) {
            std::vector<QString> edgeStartingNodeNames;
            std::vector<QString> edgeEndingNodeNames;
            std::vector<int> edgeOverlaps;

            QTextStream in(&inputFile);
            while (!in.atEnd()) {
                QString line = in.readLine();

                QStringList lineParts = line.split(QRegularExpression("\t"));
                if (lineParts.empty())
                    continue;

                // Lines beginning with "VT" are sequence (node) lines
                if (lineParts.at(0) == "VT") {
                    if (lineParts.size() < 3)
                        throw "load error";

                    // We treat all nodes in this file as positive nodes and add "+" to the end of their names.
                    QString nodeName = lineParts.at(1);
                    if (nodeName.isEmpty())
                        nodeName = "node";
                    nodeName += "+";

                    Sequence sequence{lineParts.at(2).toLocal8Bit()};
                    int length = static_cast<int>(sequence.size());

                    // ASQG files don't seem to include depth, so just set this to one for every node.
                    double nodeDepth = 1.0;

                    auto node = new DeBruijnNode(nodeName, nodeDepth, sequence, length);
                    graph.m_deBruijnGraphNodes.emplace(nodeName.toStdString(), node);
                }
                // Lines beginning with "ED" are edge lines
                else if (lineParts.at(0) == "ED") {
                    // Edges aren't made now, in case their sequence hasn't yet been specified.
                    // Instead, we save the starting and ending nodes and make the edges after
                    // we're done looking at the file.
                    if (lineParts.size() < 2)
                        throw "load error";

                    QStringList edgeParts = lineParts[1].split(" ");
                    if (edgeParts.size() < 8)
                        throw "load error";

                    QString s1Name = edgeParts.at(0);
                    QString s2Name = edgeParts.at(1);
                    int s1OverlapStart = edgeParts.at(2).toInt();
                    int s1OverlapEnd = edgeParts.at(3).toInt();
                    int s1Length = edgeParts.at(4).toInt();
                    int s2OverlapStart = edgeParts.at(5).toInt();
                    int s2OverlapEnd = edgeParts.at(6).toInt();
                    int s2Length = edgeParts.at(7).toInt();

                    //We want the overlap region of s1 to be at the end of the node sequence.  If it isn't, we use the
                    //negative node and flip the overlap coordinates.
                    if (s1OverlapEnd == s1Length - 1)
                        s1Name += "+";
                    else {
                        s1Name += "-";
                        int newOverlapStart = s1Length - s1OverlapEnd - 1;
                        int newOverlapEnd = s1Length - s1OverlapStart - 1;
                        s1OverlapStart = newOverlapStart;
                        s1OverlapEnd = newOverlapEnd;
                    }

                    //We want the overlap region of s2 to be at the start of the node sequence.  If it isn't, we use the
                    //negative node and flip the overlap coordinates.
                    if (s2OverlapStart == 0)
                        s2Name += "+";
                    else {
                        s2Name += "-";
                        int newOverlapStart = s2Length - s2OverlapEnd - 1;
                        int newOverlapEnd = s2Length - s2OverlapStart - 1;
                        s2OverlapStart = newOverlapStart;
                        s2OverlapEnd = newOverlapEnd;
                    }

                    int s1OverlapLength = s1OverlapEnd - s1OverlapStart + 1;
                    int s2OverlapLength = s2OverlapEnd - s2OverlapStart + 1;

                    //If the overlap between the two nodes is in agreement and the overlap regions extend to the ends of the
                    //nodes, then we will make the edge.
                    if (s1OverlapLength == s2OverlapLength && s1OverlapEnd == s1Length - 1 && s2OverlapStart == 0) {
                        edgeStartingNodeNames.push_back(s1Name);
                        edgeEndingNodeNames.push_back(s2Name);
                        edgeOverlaps.push_back(s1OverlapLength);
                    }
                    else
                        ++badEdgeCount;
                }
            }

            //Pair up reverse complements, creating them if necessary.
            {
                std::vector<DeBruijnNode*> nodes;
                for (const auto &entry : graph.m_deBruijnGraphNodes) {
                    DeBruijnNode *node = entry;
                    if (!graph.m_deBruijnGraphNodes.count(getOppositeNodeName(node->getName().toStdString())))
                        nodes.emplace_back(node);
                }

                for (auto &entry : nodes)
                    makeReverseComplementNodeIfNecessary(graph, entry);
            }
            pointEachNodeToItsReverseComplement(graph);

            //Create all of the edges.
            for (size_t i = 0; i < edgeStartingNodeNames.size(); ++i)
            {
                QString node1Name = edgeStartingNodeNames[i];
                QString node2Name = edgeEndingNodeNames[i];
                int overlap = edgeOverlaps[i];
                graph.createDeBruijnEdge(node1Name, node2Name, overlap, EXACT_OVERLAP);
            }
        }

        if (graph.m_deBruijnGraphNodes.empty())
            throw "load error";

        return badEdgeCount == 0;
    }
};

class TrinityAssemblyGraphBuilder : public AssemblyGraphBuilder {
    using AssemblyGraphBuilder::AssemblyGraphBuilder;

    bool build(AssemblyGraph &graph) override {
        graph.m_filename = fileName_;
        graph.m_depthTag = "";

        std::vector<QString> names;
        std::vector<QByteArray> sequences;
        utils::readFastaFile(fileName_, &names, &sequences);

        std::vector<QString> edgeStartingNodeNames;
        std::vector<QString> edgeEndingNodeNames;

        for (size_t i = 0; i < names.size(); ++i) {
            QString name = names[i];
            Sequence sequence{sequences[i]};

            //The header can come in a few different formats:
            // TR1|c0_g1_i1 len=280 path=[274:0-228 275:229-279] [-1, 274, 275, -2]
            // TRINITY_DN31_c1_g1_i1 len=301 path=[279:0-300] [-1, 279, -2]
            // GG1|c0_g1_i1 len=302 path=[1:0-301]
            // comp0_c0_seq1 len=286 path=[6:0-285]
            // c0_g1_i1 len=363 path=[119:0-185 43:186-244 43:245-303 43:304-362]

            //The node names will begin with a string that contains everything
            //up to the component number (e.g. "c0"), in the same format as it is
            //in the Trinity.fasta file.  If the node name begins with "TRINITY_DN"
            //or "TRINITY_GG", "TR" or "GG", then that will be trimmed off.

            if (name.length() < 4)
                throw "load error";

            int componentStartIndex = name.indexOf(QRegularExpression("c\\d+_"));
            int componentEndIndex = name.indexOf("_", componentStartIndex);

            if (componentStartIndex < 0 || componentEndIndex < 0)
                throw "load error";

            QString component = name.left(componentEndIndex);
            if (component.startsWith("TRINITY_DN") || component.startsWith("TRINITY_GG"))
                component = component.remove(0, 10);
            else if (component.startsWith("TR") || component.startsWith("GG"))
                component = component.remove(0, 2);

            if (component.length() < 2)
                throw "load error";

            int pathStartIndex = name.indexOf("path=[") + 6;
            int pathEndIndex = name.indexOf("]", pathStartIndex);
            if (pathStartIndex < 0 || pathEndIndex < 0)
                throw "load error";
            int pathLength = pathEndIndex - pathStartIndex;
            QString path = name.mid(pathStartIndex, pathLength);
            if (path.size() == 0)
                throw "load error";

            QStringList pathParts = path.split(" ");

            //Each path part is a node
            QString previousNodeName;
            for (int i = 0; i < pathParts.length(); ++i) {
                const QString& pathPart = pathParts.at(i);
                QStringList nodeParts = pathPart.split(":");
                if (nodeParts.size() < 2)
                    throw "load error";

                //Most node numbers will be formatted simply as the number, but some
                //(I don't know why) have '@' and the start and '@!' at the end.  In
                //these cases, we must strip those extra characters off.
                QString nodeNumberString = nodeParts.at(0);
                if (nodeNumberString.at(0) == '@')
                    nodeNumberString = nodeNumberString.mid(1, nodeNumberString.length() - 3);

                QString nodeName = component + "_" + nodeNumberString + "+";

                //If the node doesn't yet exist, make it now.
                if (!graph.m_deBruijnGraphNodes.count(nodeName.toStdString())) {
                    const QString& nodeRange = nodeParts.at(1);
                    QStringList nodeRangeParts = nodeRange.split("-");

                    if (nodeRangeParts.size() < 2)
                        throw "load error";

                    int nodeRangeStart = nodeRangeParts.at(0).toInt();
                    int nodeRangeEnd = nodeRangeParts.at(1).toInt();

                    Sequence nodeSequence = sequence.Subseq(nodeRangeStart, nodeRangeEnd + 1);

                    auto node = new DeBruijnNode(nodeName, 1.0, nodeSequence);
                    graph.m_deBruijnGraphNodes.emplace(nodeName.toStdString(), node);
                }

                //Remember to make an edge for the previous node to this one.
                if (i > 0) {
                    edgeStartingNodeNames.push_back(previousNodeName);
                    edgeEndingNodeNames.push_back(nodeName);
                }
                previousNodeName = nodeName;
            }
        }

        //Even though the Trinity.fasta file only contains positive nodes, Bandage
        //expects negative reverse complements nodes, so make them now.
        {
            std::vector<DeBruijnNode*> nodes;
            for (const auto &entry :graph. m_deBruijnGraphNodes) {
                DeBruijnNode *node = entry;
                if (!graph.m_deBruijnGraphNodes.count(getOppositeNodeName(node->getName().toStdString())))
                    nodes.emplace_back(node);
        }

            for (auto &entry : nodes)
                makeReverseComplementNodeIfNecessary(graph, entry);
        }
        pointEachNodeToItsReverseComplement(graph);

        //Create all of the edges.  The createDeBruijnEdge function checks for
        //duplicates, so it's okay if we try to add the same edge multiple times.
        for (size_t i = 0; i < edgeStartingNodeNames.size(); ++i) {
            QString node1Name = edgeStartingNodeNames[i];
            QString node2Name = edgeEndingNodeNames[i];
            graph.createDeBruijnEdge(node1Name, node2Name);
        }

        graph.setAllEdgesExactOverlap(0);

        if (graph.m_deBruijnGraphNodes.empty())
            throw "load error";

        return true;
    }
};

std::unique_ptr<AssemblyGraphBuilder>
AssemblyGraphBuilder::get(const QString &fullFileName) {
    std::unique_ptr<AssemblyGraphBuilder> res;

    if (checkFileIsGfa(fullFileName))
        res.reset(new GFAAssemblyGraphBuilder(fullFileName));
    else if (checkFileIsFastG(fullFileName))
        res.reset(new FastgAssemblyGraphBuilder(fullFileName));
    else if (checkFileIsTrinityFasta(fullFileName))
        res.reset(new TrinityAssemblyGraphBuilder(fullFileName));
    else if (checkFileIsAsqg(fullFileName))
        res.reset(new AsqgAssemblyGraphBuilder(fullFileName));
    else if (checkFileIsFasta(fullFileName))
        res.reset(new FastaAssemblyGraphBuilder(fullFileName));

    return res;
}
