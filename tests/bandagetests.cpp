//Copyright 2015 Ryan Wick

//This file is part of Bandage.

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


#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"
#include "graph/debruijnedge.h"
#include "graph/graphicsitemedge.h"
#include "graph/graphicsitemnode.h"
#include "graph/annotationsmanager.h"
#include "graph/annotation.h"
#include "graph/nodecolorer.h"
#include "graph/nodecolorers.h"
#include "graph/gfawriter.h"
#include "graph/io.h"
#include "graph/graphscope.h"
#include "io/bedloader.h"

#include "layout/graphlayoutworker.h"
#include "layout/io.h"
#include "ui/bandagegraphicsscene.h"

#include "program/settings.h"
#include "program/memory.h"
#include "program/globals.h"
#include "command_line/commoncommandlinefunctions.h"
#include "command_line/settings.h"

#include "graphsearch/graphsearch.h"
#include "graphsearch/query.h"
#include "graphsearch/hit.h"
#include "program/scinot.h"

#include <CLI/CLI.hpp>

#include <QtTest/QtTest>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QTextStream>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <memory>

#define REQUIRE_OK(var, expr) \
    auto var##Result = (expr); \
    if (!var##Result) \
        QFAIL(bandage::toString(var##Result.takeError()).c_str()); \
    auto var = std::move(*var##Result)

#define EXPECT_DRAWN(scopeExpr, expectedCount) \
    do { \
        REQUIRE_OK(expectDrawnNodes, graph::getStartingNodes(*g_assemblyGraph, scopeExpr)); \
        g_assemblyGraph->resetNodes(); \
        g_assemblyGraph->markNodesToDraw(scopeExpr, expectDrawnNodes); \
        QCOMPARE(g_assemblyGraph->getDrawnNodeCount(), expectedCount); \
    } while (false)

#define EXPECT_PATH_ERROR(pathText, circular, sentence) \
    do { \
        auto pathErrorResult = Path::makeFromString(pathText, *g_assemblyGraph, circular); \
        if (pathErrorResult) \
            QFAIL("expected a path error"); \
        std::string pathErrorTitle; \
        std::string pathErrorBody; \
        bandage::handleAllErrors(pathErrorResult.takeError(), [&](const bandage::TextError &err) { \
            pathErrorTitle = err.title(); \
            pathErrorBody = err.text(); \
        }); \
        QCOMPARE(QString::fromStdString(pathErrorTitle), QString("Invalid path")); \
        QCOMPARE(QString::fromStdString(pathErrorBody), QString(sentence)); \
    } while (false)

class BandageTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmpDir;

    QString tempFile(const QString &fileName) const {
        return m_tmpDir.filePath(fileName);
    }

    QString testFile(const QString &fileName) const {
        QDir testDir(getTestDirectory());

        return testDir.filePath(fileName);
    }

    QString getTestDirectory() const {
        QDir directory = QDir::current();

        // We want to find a directory "Bandage/tests/inputs".  Keep backing up in the
        // directory structure until we find it.
        QString path;
        while (true)
        {
            path = directory.path() + "/tests/inputs/";

            if (QDir(path).exists())
                return path;
            if (!directory.cdUp())
                return "";
        }

        return "";
    }

public:
    BandageTests()
            : m_tmpDir("bandage-tests") {
        std::cout << "sizeof(DeBruijnNode)=" << sizeof(DeBruijnNode)
                  << ", sizeof(DeBruijnEdge)=" << sizeof(DeBruijnEdge) << std::endl;
        std::cout << "sizeof(GraphicsItemEdge)=" << sizeof(GraphicsItemEdge)
                  << ", sizeof(GraphicsItemNode)=" << sizeof(GraphicsItemNode) << std::endl;
    }

private slots:
    void init() {
        g_settings.reset(new Settings());
        g_memory.reset(new Memory());
        g_graphSearch = search::GraphSearch::get(search::BLAST, QDir("."));
        g_assemblyGraph.reset(new AssemblyGraph());
        g_annotationsManager = std::make_shared<AnnotationsManager>();
    }

    void cleanup() {
    }

    void loadFastg();
    void loadLogan();
    void loadGFAWithPlaceholders();
    void loadGFA12();
    void loadGFA();
    void loadGFAWithIntegerDp();
    void loadGAF();
    void loadSPAdesPaths();
    void loadLinks();
    void loadTrinity();
    void pathFunctionsOnGFA();
    void pathFunctionsOnFastg();
    void pathFunctionsOnGfaSequencesInGraph();
    void pathFunctionsOnGfaSequencesInFasta();
    void graphLocationFunctions();
    void loadCsvData();
    void loadCsvDataTrinity();
    void blastSearch();
    void blastSearchFilters();
    void graphScope();
    void graphScopeComponent();
    void graphLayout();
    void commandLineSettings();
    void sciNotComparisons();
    void graphEdits();
    void fastgToGfa();
    void mergeNodesOnGfa();
    void changeNodeNames();
    void changeNodeDepths();
    void blastQueryPaths();
    void bandageInfo();
    void sequenceInit();
    void sequenceInitN();
    void sequenceAllNs();
    void sequenceAccess();
    void sequenceSubstring();
    void sequenceDoubleReverseComplement();
    void sequenceShort();
    void oneEdgePerLink();
    void duplicateNodeWithSelfLoop();
    void loadDistinctGfaOverlaps();
    void loadLoganPalindromicSegment();
    void queryPathOverlapReverseStoredEdge();
    void drawEdgeDirections();
    void annotationGroupNodesWithAnnotations();
    void loadWalks();
    void graphScopePathsWalksAndErrors();
    void layoutRoundTrip();
    void loadBedAnnotations();
    void gfaTagsAndCsvColours();
    void nodeColorers();
    void hitAnnotationCoordinates();
    void minimap2Search();
    void hmmerSearch();
    void gfaJumpsOverlapsAndFastaCircularity();
    void partialNodeNameScope();


private:
    DeBruijnEdge * getEdgeFromNodeNames(QString startingNodeName,
                                        QString endingNodeName) const;
    bool doCircularSequencesMatch(QByteArray s1, QByteArray s2) const;
};





static void requireGraphLoad(const QString &filename) {
    auto loaded = g_assemblyGraph->loadGraphFromFile(filename);
    if (!loaded)
        QFAIL(bandage::toString(loaded.takeError()).c_str());
}

void BandageTests::loadFastg()
{
    requireGraphLoad(testFile("test.fastg"));

    //Check that the appropriate number of nodes/edges are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 88);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 59);

    //Check the length of a couple nodes.
    DeBruijnNode * node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode * node28 = g_assemblyGraph->m_deBruijnGraphNodes["28-"];
    QCOMPARE(node1->getLength(), 6070);
    QCOMPARE(node28->getLength(), 79);
}

void BandageTests::loadLogan()
{
    requireGraphLoad(testFile("test.contigs.fa.zst"));

    // Check that the appropriate number of nodes/edges are present.
    // The fixture has 9586 L tags. Opposite directions share one object.
    // Segments 3221 and 3247 equal their own reverse complement. The four
    // L tags on each collapse to one self-loop once the two strand names
    // are the same node, so those segments contribute two own-reverse-
    // complement edges rather than six.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 6562);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 4791);
    size_t ownReverseComplement = 0;
    for (DeBruijnEdge *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (edge->isOwnReverseComplement())
            ++ownReverseComplement;
    }
    QCOMPARE(ownReverseComplement, 2u);

    // Check the length of a couple nodes.
    DeBruijnNode * node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode * node28 = g_assemblyGraph->m_deBruijnGraphNodes["28-"];
    QCOMPARE(node1->getLength(), 60);
    QCOMPARE(node28->getLength(), 60);
}

void BandageTests::loadGFAWithPlaceholders()
{
    requireGraphLoad(testFile("test_not_defined.gfa"));

    //Check that the appropriate number of nodes/edges are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 8);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 4);

    //Check the length of a couple nodes.
    DeBruijnNode * node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode * node2 = g_assemblyGraph->m_deBruijnGraphNodes["2+"];
    DeBruijnNode * node4 = g_assemblyGraph->m_deBruijnGraphNodes["4-"];
    QCOMPARE(node1->getLength(), 19);
    QCOMPARE(node2->getLength(), 1);
    QCOMPARE(node4->getLength(), 0);
}

void BandageTests::loadGFA12()
{
    requireGraphLoad(testFile("test_gfa12.gfa.gz"));

    //Check that the appropriate number of nodes/edges/paths are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 12);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 8);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphPaths.size(), 5);
}


void BandageTests::loadGFA()
{
    //Check that the graph loaded properly.
    requireGraphLoad(testFile("test.gfa"));

    //Check that the appropriate number of nodes/edges are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 34);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 16);

    //Check the length of a couple nodes.
    DeBruijnNode * node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode * node14 = g_assemblyGraph->m_deBruijnGraphNodes["14-"];
    QCOMPARE(node1->getLength(), 2060);
    QCOMPARE(node14->getLength(), 120);
}

void BandageTests::loadGFAWithIntegerDp()
{
    requireGraphLoad(testFile("test_dp_int.gfa"));

    QCOMPARE(g_assemblyGraph->m_depthTag, QString("DP"));

    DeBruijnNode *node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode *node2 = g_assemblyGraph->m_deBruijnGraphNodes["2+"];
    DeBruijnNode *node3 = g_assemblyGraph->m_deBruijnGraphNodes["3+"];
    QCOMPARE(node1->getDepth(), 42.0);
    QCOMPARE(node2->getDepth(), 3.5);
    QCOMPARE(node3->getDepth(), 7.0);
    QCOMPARE(node1->getReverseComplement()->getDepth(), 42.0);
}

void BandageTests::loadGAF()
{
    // Check that the graph loaded properly.
    requireGraphLoad(testFile("test_gaf.gfa"));

    //Check that the appropriate number of nodes/edges are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 8);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 4);

    //Check the length of a couple nodes.
    DeBruijnNode * node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode * node14 = g_assemblyGraph->m_deBruijnGraphNodes["4-"];
    QCOMPARE(node1->getLength(), 44);
    QCOMPARE(node14->getLength(), 42);

    QVERIFY(io::loadGAFPaths(*g_assemblyGraph, testFile("test.gaf")));
    const Path &p = g_assemblyGraph->m_deBruijnGraphPaths["read"];
    QCOMPARE(p.getLength(), 71);
}

void BandageTests::loadSPAdesPaths()
{
    // Check that the graph loaded properly.
    requireGraphLoad(testFile("test.gfa"));

    // Check that the appropriate number of nodes/edges are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 34);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 16);

    // Check the length of a couple nodes.
    DeBruijnNode * node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode * node14 = g_assemblyGraph->m_deBruijnGraphNodes["14-"];
    QCOMPARE(node1->getLength(), 2060);
    QCOMPARE(node14->getLength(), 120);

    QVERIFY(io::loadSPAdesPaths(*g_assemblyGraph, testFile("test.paths")));
    const Path &p1 = g_assemblyGraph->m_deBruijnGraphPaths["NODE_FIRST"];
    QCOMPARE(p1.getLength(), 4060);

    const Path &p21 = g_assemblyGraph->m_deBruijnGraphPaths["NODE_SECOND_1"];
    QCOMPARE(p21.getLength(), 4000);
    const Path &p22 = g_assemblyGraph->m_deBruijnGraphPaths["NODE_SECOND_2"];
    QCOMPARE(p22.getLength(), 6000);
}

void BandageTests::loadLinks()
{
    // Check that the graph loaded properly.
    requireGraphLoad(testFile("test.gfa"));

    // Check that the appropriate number of nodes/edges are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 34);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 16);

    // Check the length of a couple nodes.
    DeBruijnNode * node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode * node14 = g_assemblyGraph->m_deBruijnGraphNodes["14-"];
    QCOMPARE(node1->getLength(), 2060);
    QCOMPARE(node14->getLength(), 120);

    std::vector<DeBruijnEdge*> newEdges;
    QVERIFY(io::loadLinks(*g_assemblyGraph, testFile("test.links.tsv"), &newEdges));
    QCOMPARE(newEdges.size(), 2);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 18);
}


void BandageTests::loadTrinity()
{
    requireGraphLoad(testFile("test.Trinity.fasta"));

    //Check that the appropriate number of nodes/edges are present.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 1170);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 528);

    //Check the length of a couple nodes.
    DeBruijnNode * node10241 = g_assemblyGraph->m_deBruijnGraphNodes["4|c4_10241+"];
    DeBruijnNode * node3901 = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3901-"];
    QCOMPARE(node10241->getLength(), 1186);
    QCOMPARE(node3901->getLength(), 1);
}


//LastGraph files have no overlap in the edges, so these tests look at paths
//where the connections are simple.
void BandageTests::pathFunctionsOnGFA()
{
    requireGraphLoad(testFile("test.gfa"));
    REQUIRE_OK(testPath1, Path::makeFromString("(1996) 9+, 13+ (5)", *g_assemblyGraph, false));
    REQUIRE_OK(testPath2, Path::makeFromString("(1996) 9+, 13+ (5)", *g_assemblyGraph, false));
    REQUIRE_OK(testPath3, Path::makeFromString("(1996) 9+, 13+ (6)", *g_assemblyGraph, false));
    REQUIRE_OK(testPath4, Path::makeFromString("9+, 13+, 14-", *g_assemblyGraph, false));

    auto badPath = Path::makeFromString("not a path", *g_assemblyGraph, false);
    QVERIFY(!badPath);
    QString badTitle;
    QString badText;
    bandage::handleAllErrors(badPath.takeError(), [&](const bandage::TextError &err) {
        badTitle = QString::fromStdString(err.title());
        badText = QString::fromStdString(err.text());
    });
    QCOMPARE(badTitle, QString("Invalid path"));
    QCOMPARE(badText, QString("the text is not formatted correctly"));
    EXPECT_PATH_ERROR("(1) 9+", true, "circular paths cannot contain start or end positions");
    EXPECT_PATH_ERROR("missing+", false, "the following nodes are not in the graph: missing+");
    EXPECT_PATH_ERROR("(999999) 9+", false, "starting node position not valid");
    EXPECT_PATH_ERROR("9+ (999999)", false, "ending node position not valid");
    DeBruijnNode *pathStart = g_assemblyGraph->m_deBruijnGraphNodes["9+"];
    QString disconnected;
    for (auto &entry : g_assemblyGraph->m_deBruijnGraphNodes) {
        DeBruijnNode *candidate = entry;
        if (candidate == pathStart)
            continue;
        bool linked = false;
        for (auto *edge : pathStart->edges()) {
            if (edge->getStartingNode() == pathStart && edge->getEndingNode() == candidate) {
                linked = true;
                break;
            }
        }
        if (!linked) {
            disconnected = QString::fromStdString(candidate->getName());
            break;
        }
    }
    QVERIFY(!disconnected.isEmpty());
    EXPECT_PATH_ERROR(QString("9+, %1").arg(disconnected), false, "the nodes do not form a path");

    DeBruijnNode * node4Minus = g_assemblyGraph->m_deBruijnGraphNodes["4-"];
    DeBruijnNode * node9Plus = g_assemblyGraph->m_deBruijnGraphNodes["9+"];
    DeBruijnNode * node13Plus = g_assemblyGraph->m_deBruijnGraphNodes["13+"];
    DeBruijnNode * node14Minus = g_assemblyGraph->m_deBruijnGraphNodes["14+"];
    DeBruijnNode * node7Plus = g_assemblyGraph->m_deBruijnGraphNodes["7+"];

    QCOMPARE(testPath1.getLength(), 10);
    QCOMPARE(testPath1.getPathSequence(), QByteArray("GGAAGAGAGC"));
    QCOMPARE(testPath1.isEmpty(), false);
    QCOMPARE(testPath1.isCircular(), false);
    QCOMPARE(testPath1 == testPath2, true);
    QCOMPARE(testPath1 == testPath3, false);
    QCOMPARE(testPath1.haveSameNodes(testPath3), true);
    QCOMPARE(testPath1.hasNodeSubset(testPath4), true);
    QCOMPARE(testPath4.hasNodeSubset(testPath1), false);
    QCOMPARE(testPath1.getString(true), QString("(1996) 9+, 13+ (5)"));
    QCOMPARE(testPath1.getString(false), QString("(1996)9+,13+(5)"));
    QCOMPARE(testPath4.getString(true), QString("9+, 13+, 14-"));
    QCOMPARE(testPath4.getString(false), QString("9+,13+,14-"));
    QCOMPARE(testPath1.containsEntireNode(node13Plus), false);
    QCOMPARE(testPath4.containsEntireNode(node13Plus), true);
    QCOMPARE(testPath4.isInMiddleOfPath(node13Plus), true);
    QCOMPARE(testPath4.isInMiddleOfPath(node14Minus), false);
    QCOMPARE(testPath4.isInMiddleOfPath(node9Plus), false);

    Path testPath4Extended;
    QCOMPARE(testPath4.canNodeFitOnEnd(node7Plus, &testPath4Extended), true);
    QCOMPARE(testPath4Extended.getString(true), QString("9+, 13+, 14-, 7+"));
    QCOMPARE(testPath4.canNodeFitAtStart(node4Minus, &testPath4Extended), true);
    QCOMPARE(testPath4Extended.getString(true), QString("4-, 9+, 13+, 14-"));

    auto node13Pos = testPath4.getPosition(node13Plus);
    QCOMPARE(node13Pos.size(), 1);
    QCOMPARE(node13Pos.front(), 2001);
    auto nodesAt2000 = testPath4.getNodesAt(2000, 2000);
    QCOMPARE(nodesAt2000.size(), 1);
    auto nodesAt2001 = testPath4.getNodesAt(2001, 2001);
    QCOMPARE(nodesAt2001.size(), 2);
    auto nodeRange = testPath4.getNodesAt(2000, 4060);
    QCOMPARE(nodeRange.size(), 3);
}



//FASTG files have overlaps in the edges, so these tests look at paths where
//the overlap has to be removed from the path sequence.
void BandageTests::pathFunctionsOnFastg()
{
    requireGraphLoad(testFile("test.fastg"));
    REQUIRE_OK(testPath1, Path::makeFromString("(50234) 6+, 26+, 23+, 26+, 24+ (200)", *g_assemblyGraph, false));
    REQUIRE_OK(testPath2, Path::makeFromString("26+, 23+", *g_assemblyGraph, true));
    QCOMPARE(testPath1.getLength(), 1764);
    QCOMPARE(testPath2.getLength(), 1387);
    QCOMPARE(testPath1.isCircular(), false);
    QCOMPARE(testPath2.isCircular(), true);
}


//This function tests paths on a GFA file which keeps its sequences in the GFA
//file.
void BandageTests::pathFunctionsOnGfaSequencesInGraph()
{
    requireGraphLoad(testFile("test_plasmids.gfa"));

    //Check that the number of nodes/edges.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 18);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 12);

    //Check a couple short paths.

    REQUIRE_OK(testPath1, Path::makeFromString("232+, 277+", *g_assemblyGraph, false));
    QByteArray testPath1Sequence = "CCTTATACGAAGCCCAGGTTAATCCTGGGCTTTTTGTTGAATCTGATCATTGGTAGCAAACAGATCAGGATTGGTAATTTTGATGTTTTCCTGACAACTCCTGCAAAGCATCAGCCCAGCAAAAAGTTGTACATGTTCCGTTGATTCACAGAAGGCACATGGCTTAGGAAAAGAGATGATATTGGCTGAATTGACTAATTCTGTTGACATAAGAAGTAACCTTGGATTGTACATATTTATTTTTAATAAATTTCAATACTTTATACCTAATTTAGCCACTAAAATTTGTACATTATTGTATAATCCATGTGTACATATTATTTTTTATAATTTTCATTCACTTAGACCCAAAATAGTATTTATTTTTGTACAACACCATGTACAGAACAATAATCATATAAATCAAATTTTTAGCATAAAAAAGTCCATTAATTTTGTACACAATTCTGAAACTTAAAAATCTAAACTTTCATCAATTTTTTTCATAATTTCAATAAATTAACCTTAATTTTAAGATATATTCTGAAATTTGGTTTGAAAGCTGTTTTTACATTATATTTCAATACTTTAAATCAAAAAATTGGATATTTTTTGAAAAACTCAATGAAAGTTTATTTTTTATTTAAAAACAACTAGTTATATTAGTTTTTATCCATTTTTTTGAAACAGTTTCTATATGATAAAAAAACCTATAAAAACCATATCTAGCAAAGGTTTGAGGGTTATCATCTTTAGATGCGTGGTGTGTGACAAAAAAATCCCGGCATGTGCCGGATTCTGGATTAGAAAATTGGCTAAAGTGACGTAGGACTGGTGCTTGGTTTTACATGGAAAAAAGTATTTATTTTCTGGTTTATAAAAACGTAAAAAGATCAGTTTTTGTTCATTCATCCAGGTTAAAAATTTCAACCTAAAACTTTAATTATGAAAAGCTTCACAGAAAGCATTCAAATGCGATTTAAGAGCCTTTATCTAAAAAACATAGATCTTATAGCGAAAAACAGAAAACAGCTCAAAAAACGCAAAAGAGAGTGAAGTAAAGAGATGTTTTGACTTTAGATAGCTGCATAGAGCGAGTGTCTACGAGCGAACTATCAAAATTTGCGTCTAGACTCTCTGAAAAACATTTTTTTGCCCTCTTTAGCCTAAGAAAGCTTAATTTTCATGCAGAAATTTGCTCCTGGACCGAGCGTAGCGAGAAAAAAAGCTCATGAGCGAAGCGAATTCCGAGTTGCTTTTGCTTTTTCTTAAAGTCACGCAAGTATTAACCAAAAAATTGCCCCGACGAACTGAGCGAAAGCGAAGTTCAATAGAGTTTGAGCGAAGCGAAAACCAAGGGC";
    REQUIRE_OK(testPath2, Path::makeFromString("277-, 232-", *g_assemblyGraph, false));
    QByteArray testPath2Sequence = "GCCCTTGGTTTTCGCTTCGCTCAAACTCTATTGAACTTCGCTTTCGCTCAGTTCGTCGGGGCAATTTTTTGGTTAATACTTGCGTGACTTTAAGAAAAAGCAAAAGCAACTCGGAATTCGCTTCGCTCATGAGCTTTTTTTCTCGCTACGCTCGGTCCAGGAGCAAATTTCTGCATGAAAATTAAGCTTTCTTAGGCTAAAGAGGGCAAAAAAATGTTTTTCAGAGAGTCTAGACGCAAATTTTGATAGTTCGCTCGTAGACACTCGCTCTATGCAGCTATCTAAAGTCAAAACATCTCTTTACTTCACTCTCTTTTGCGTTTTTTGAGCTGTTTTCTGTTTTTCGCTATAAGATCTATGTTTTTTAGATAAAGGCTCTTAAATCGCATTTGAATGCTTTCTGTGAAGCTTTTCATAATTAAAGTTTTAGGTTGAAATTTTTAACCTGGATGAATGAACAAAAACTGATCTTTTTACGTTTTTATAAACCAGAAAATAAATACTTTTTTCCATGTAAAACCAAGCACCAGTCCTACGTCACTTTAGCCAATTTTCTAATCCAGAATCCGGCACATGCCGGGATTTTTTTGTCACACACCACGCATCTAAAGATGATAACCCTCAAACCTTTGCTAGATATGGTTTTTATAGGTTTTTTTATCATATAGAAACTGTTTCAAAAAAATGGATAAAAACTAATATAACTAGTTGTTTTTAAATAAAAAATAAACTTTCATTGAGTTTTTCAAAAAATATCCAATTTTTTGATTTAAAGTATTGAAATATAATGTAAAAACAGCTTTCAAACCAAATTTCAGAATATATCTTAAAATTAAGGTTAATTTATTGAAATTATGAAAAAAATTGATGAAAGTTTAGATTTTTAAGTTTCAGAATTGTGTACAAAATTAATGGACTTTTTTATGCTAAAAATTTGATTTATATGATTATTGTTCTGTACATGGTGTTGTACAAAAATAAATACTATTTTGGGTCTAAGTGAATGAAAATTATAAAAAATAATATGTACACATGGATTATACAATAATGTACAAATTTTAGTGGCTAAATTAGGTATAAAGTATTGAAATTTATTAAAAATAAATATGTACAATCCAAGGTTACTTCTTATGTCAACAGAATTAGTCAATTCAGCCAATATCATCTCTTTTCCTAAGCCATGTGCCTTCTGTGAATCAACGGAACATGTACAACTTTTTGCTGGGCTGATGCTTTGCAGGAGTTGTCAGGAAAACATCAAAATTACCAATCCTGATCTGTTTGCTACCAATGATCAGATTCAACAAAAAGCCCAGGATTAACCTGGGCTTCGTATAAGG";
    QCOMPARE(testPath1.getPathSequence(), testPath1Sequence);
    QCOMPARE(testPath2.getPathSequence(), testPath2Sequence);
}


//This function tests paths on a GFA file which keeps its sequences in a
//separate FASTA file.
void BandageTests::pathFunctionsOnGfaSequencesInFasta()
{
    requireGraphLoad(testFile("test_plasmids_separate_sequences.gfa"));

    //Check that the number of nodes/edges.
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 18);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 12);

    //Since a node sequence hasn't be required yet, the nodes should still have
    //just '*' for their sequence. But they should still report the correct
    //length.
    DeBruijnNode * node282Plus = g_assemblyGraph->m_deBruijnGraphNodes["282+"];
    DeBruijnNode * node282Minus = g_assemblyGraph->m_deBruijnGraphNodes["282-"];
    QCOMPARE(node282Plus->sequenceIsMissing(), false);
    QCOMPARE(node282Minus->sequenceIsMissing(), false);
    QCOMPARE(node282Plus->getLength(), 1819);
    QCOMPARE(node282Minus->getLength(), 1819);

    //Check a couple short paths.
    REQUIRE_OK(testPath1, Path::makeFromString("232+, 277+", *g_assemblyGraph, false));
    QByteArray testPath1Sequence = "CCTTATACGAAGCCCAGGTTAATCCTGGGCTTTTTGTTGAATCTGATCATTGGTAGCAAACAGATCAGGATTGGTAATTTTGATGTTTTCCTGACAACTCCTGCAAAGCATCAGCCCAGCAAAAAGTTGTACATGTTCCGTTGATTCACAGAAGGCACATGGCTTAGGAAAAGAGATGATATTGGCTGAATTGACTAATTCTGTTGACATAAGAAGTAACCTTGGATTGTACATATTTATTTTTAATAAATTTCAATACTTTATACCTAATTTAGCCACTAAAATTTGTACATTATTGTATAATCCATGTGTACATATTATTTTTTATAATTTTCATTCACTTAGACCCAAAATAGTATTTATTTTTGTACAACACCATGTACAGAACAATAATCATATAAATCAAATTTTTAGCATAAAAAAGTCCATTAATTTTGTACACAATTCTGAAACTTAAAAATCTAAACTTTCATCAATTTTTTTCATAATTTCAATAAATTAACCTTAATTTTAAGATATATTCTGAAATTTGGTTTGAAAGCTGTTTTTACATTATATTTCAATACTTTAAATCAAAAAATTGGATATTTTTTGAAAAACTCAATGAAAGTTTATTTTTTATTTAAAAACAACTAGTTATATTAGTTTTTATCCATTTTTTTGAAACAGTTTCTATATGATAAAAAAACCTATAAAAACCATATCTAGCAAAGGTTTGAGGGTTATCATCTTTAGATGCGTGGTGTGTGACAAAAAAATCCCGGCATGTGCCGGATTCTGGATTAGAAAATTGGCTAAAGTGACGTAGGACTGGTGCTTGGTTTTACATGGAAAAAAGTATTTATTTTCTGGTTTATAAAAACGTAAAAAGATCAGTTTTTGTTCATTCATCCAGGTTAAAAATTTCAACCTAAAACTTTAATTATGAAAAGCTTCACAGAAAGCATTCAAATGCGATTTAAGAGCCTTTATCTAAAAAACATAGATCTTATAGCGAAAAACAGAAAACAGCTCAAAAAACGCAAAAGAGAGTGAAGTAAAGAGATGTTTTGACTTTAGATAGCTGCATAGAGCGAGTGTCTACGAGCGAACTATCAAAATTTGCGTCTAGACTCTCTGAAAAACATTTTTTTGCCCTCTTTAGCCTAAGAAAGCTTAATTTTCATGCAGAAATTTGCTCCTGGACCGAGCGTAGCGAGAAAAAAAGCTCATGAGCGAAGCGAATTCCGAGTTGCTTTTGCTTTTTCTTAAAGTCACGCAAGTATTAACCAAAAAATTGCCCCGACGAACTGAGCGAAAGCGAAGTTCAATAGAGTTTGAGCGAAGCGAAAACCAAGGGC";
    REQUIRE_OK(testPath2, Path::makeFromString("277-, 232-", *g_assemblyGraph, false));
    QByteArray testPath2Sequence = "GCCCTTGGTTTTCGCTTCGCTCAAACTCTATTGAACTTCGCTTTCGCTCAGTTCGTCGGGGCAATTTTTTGGTTAATACTTGCGTGACTTTAAGAAAAAGCAAAAGCAACTCGGAATTCGCTTCGCTCATGAGCTTTTTTTCTCGCTACGCTCGGTCCAGGAGCAAATTTCTGCATGAAAATTAAGCTTTCTTAGGCTAAAGAGGGCAAAAAAATGTTTTTCAGAGAGTCTAGACGCAAATTTTGATAGTTCGCTCGTAGACACTCGCTCTATGCAGCTATCTAAAGTCAAAACATCTCTTTACTTCACTCTCTTTTGCGTTTTTTGAGCTGTTTTCTGTTTTTCGCTATAAGATCTATGTTTTTTAGATAAAGGCTCTTAAATCGCATTTGAATGCTTTCTGTGAAGCTTTTCATAATTAAAGTTTTAGGTTGAAATTTTTAACCTGGATGAATGAACAAAAACTGATCTTTTTACGTTTTTATAAACCAGAAAATAAATACTTTTTTCCATGTAAAACCAAGCACCAGTCCTACGTCACTTTAGCCAATTTTCTAATCCAGAATCCGGCACATGCCGGGATTTTTTTGTCACACACCACGCATCTAAAGATGATAACCCTCAAACCTTTGCTAGATATGGTTTTTATAGGTTTTTTTATCATATAGAAACTGTTTCAAAAAAATGGATAAAAACTAATATAACTAGTTGTTTTTAAATAAAAAATAAACTTTCATTGAGTTTTTCAAAAAATATCCAATTTTTTGATTTAAAGTATTGAAATATAATGTAAAAACAGCTTTCAAACCAAATTTCAGAATATATCTTAAAATTAAGGTTAATTTATTGAAATTATGAAAAAAATTGATGAAAGTTTAGATTTTTAAGTTTCAGAATTGTGTACAAAATTAATGGACTTTTTTATGCTAAAAATTTGATTTATATGATTATTGTTCTGTACATGGTGTTGTACAAAAATAAATACTATTTTGGGTCTAAGTGAATGAAAATTATAAAAAATAATATGTACACATGGATTATACAATAATGTACAAATTTTAGTGGCTAAATTAGGTATAAAGTATTGAAATTTATTAAAAATAAATATGTACAATCCAAGGTTACTTCTTATGTCAACAGAATTAGTCAATTCAGCCAATATCATCTCTTTTCCTAAGCCATGTGCCTTCTGTGAATCAACGGAACATGTACAACTTTTTGCTGGGCTGATGCTTTGCAGGAGTTGTCAGGAAAACATCAAAATTACCAATCCTGATCTGTTTGCTACCAATGATCAGATTCAACAAAAAGCCCAGGATTAACCTGGGCTTCGTATAAGG";

    //Check the paths sequences. Doing so will trigger the loading of node
    //sequences from the FASTA file.
    QCOMPARE(testPath1.getPathSequence(), testPath1Sequence);
    QCOMPARE(testPath2.getPathSequence(), testPath2Sequence);

    //Now that the paths have been accessed, the node sequences should be
    //loaded (even the ones not in the path).
    QCOMPARE(node282Plus->sequenceIsMissing(), false);
    QCOMPARE(node282Minus->sequenceIsMissing(), false);
    QCOMPARE(node282Plus->getLength(), 1819);
    QCOMPARE(node282Minus->getLength(), 1819);
}


void BandageTests::graphLocationFunctions()
{
    //First do some tests with a FASTG, where the overlap results in a simpler
    //situations: all positions have a reverse complement position in the
    //reverse complement node.
    requireGraphLoad(testFile("test.fastg"));
    DeBruijnNode * node12Plus = g_assemblyGraph->m_deBruijnGraphNodes["12+"];
    DeBruijnNode * node3Plus = g_assemblyGraph->m_deBruijnGraphNodes["3+"];

    GraphLocation location1(node12Plus, 1);
    GraphLocation revCompLocation1 = location1.reverseComplementLocation();

    QCOMPARE(location1.getBase(), 'C');
    QCOMPARE(revCompLocation1.getBase(), 'G');
    QCOMPARE(revCompLocation1.getPosition(), 394);

    GraphLocation location2 = GraphLocation::endOfNode(node3Plus);
    QCOMPARE(location2.getPosition(), 5869);

    location2.moveLocation(-1);
    QCOMPARE(location2.getPosition(), 5868);

    location2.moveLocation(2);
    QCOMPARE(location2.getNode()->getName(), std::string("38-"));
    QCOMPARE(location2.getPosition(), 1);

    GraphLocation location3;
    QCOMPARE(location2.isNull(), false);
    QCOMPARE(location3.isNull(), true);
}



void BandageTests::loadCsvData()
{
    requireGraphLoad(testFile("test.gfa"));

    QString errormsg;
    QStringList columns;
    bool coloursLoaded = false;
    QVERIFY(g_assemblyGraph->loadCSV(testFile("test.csv"), &columns, &errormsg, &coloursLoaded));

    DeBruijnNode * node6Plus = g_assemblyGraph->m_deBruijnGraphNodes["6+"];
    DeBruijnNode * node6Minus = g_assemblyGraph->m_deBruijnGraphNodes["6-"];
    DeBruijnNode * node7Plus = g_assemblyGraph->m_deBruijnGraphNodes["7+"];
    DeBruijnNode * node4Plus = g_assemblyGraph->m_deBruijnGraphNodes["4+"];
    DeBruijnNode * node4Minus = g_assemblyGraph->m_deBruijnGraphNodes["4-"];
    DeBruijnNode * node3Plus = g_assemblyGraph->m_deBruijnGraphNodes["3+"];
    DeBruijnNode * node5Minus = g_assemblyGraph->m_deBruijnGraphNodes["5-"];
    DeBruijnNode * node8Plus = g_assemblyGraph->m_deBruijnGraphNodes["8+"];
    DeBruijnNode * node9Plus = g_assemblyGraph->m_deBruijnGraphNodes["9+"];
    DeBruijnNode * node14Plus = g_assemblyGraph->m_deBruijnGraphNodes["14+"];

    QCOMPARE(columns.size(), 3);
    QCOMPARE(errormsg, QString("There were 2 unmatched entries in the CSV."));

    QCOMPARE(g_assemblyGraph->getCsvLine(node6Plus, 0), QString("SIX_PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node6Plus, 1), QString("6plus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node6Plus, 2), QString("plus6"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 3), QString(""));
    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 25), QString(""));

    QCOMPARE(g_assemblyGraph->getCsvLine(node6Minus, 0), QString("SIX_MINUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node6Minus, 1), QString("6minus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node6Minus, 2), QString("minus6"));

    QCOMPARE(g_assemblyGraph->getCsvLine(node7Plus, 0), QString("SEVEN_PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node7Plus, 1), QString("7plus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node7Plus, 2), QString("plus7"));

    QCOMPARE(g_assemblyGraph->getCsvLine(node4Plus, 0), QString("FOUR_PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node4Plus, 1), QString("4plus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node4Plus, 2), QString("plus4"));

    QCOMPARE(g_assemblyGraph->getCsvLine(node4Minus, 0), QString("FOUR_MINUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node4Minus, 1), QString("4minus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node4Minus, 2), QString("minus4"));

    QCOMPARE(g_assemblyGraph->getCsvLine(node3Plus, 0), QString("THREE_PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3Plus, 1), QString("3plus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3Plus, 2), QString("plus3"));

    QCOMPARE(g_assemblyGraph->getCsvLine(node5Minus, 0), QString("FIVE_MINUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node5Minus, 1), QString(""));
    QCOMPARE(g_assemblyGraph->getCsvLine(node5Minus, 2), QString(""));

    QCOMPARE(g_assemblyGraph->getCsvLine(node8Plus, 0), QString("EIGHT_PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node8Plus, 1), QString("8plus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node8Plus, 2), QString("plus8"));

    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 0), QString("NINE_PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 1), QString("9plus"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 2), QString("plus9"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 3), QString(""));
    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 4), QString(""));
    QCOMPARE(g_assemblyGraph->getCsvLine(node9Plus, 5), QString(""));

    errormsg = "";
    QVERIFY(g_assemblyGraph->loadCSV(testFile("test_paths.csv"), &columns, &errormsg, &coloursLoaded));
    QCOMPARE(columns.size(), 3);
    QCOMPARE(errormsg, "");
    QCOMPARE(g_assemblyGraph->getAllCsvData(node4Plus).join("|"), "P21|P22|P23");
    QCOMPARE(g_assemblyGraph->getAllCsvData(node8Plus).join("|"), "P21|P22|P23");
    QCOMPARE(g_assemblyGraph->getAllCsvData(node14Plus).join("|"), "P11|P12|P13");
}


void BandageTests::loadCsvDataTrinity()
{
    requireGraphLoad(testFile("test.Trinity.fasta"));

    QString errormsg;
    QStringList columns;
    bool coloursLoaded = false;
    g_assemblyGraph->loadCSV(testFile("test.Trinity.csv"), &columns, &errormsg, &coloursLoaded);

    DeBruijnNode * node3912Plus = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3912+"];
    DeBruijnNode * node3912Minus = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3912-"];
    DeBruijnNode * node3914Plus = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3914+"];
    DeBruijnNode * node3915Plus = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3915+"];
    DeBruijnNode * node3923Plus = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3923+"];
    DeBruijnNode * node3924Plus = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3924+"];
    DeBruijnNode * node3940Plus = g_assemblyGraph->m_deBruijnGraphNodes["19|c0_3940+"];

    QCOMPARE(columns.size(), 1);

    QCOMPARE(g_assemblyGraph->getCsvLine(node3912Plus, 0), QString("3912PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3912Minus, 0), QString("3912MINUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3914Plus, 0), QString("3914PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3915Plus, 0), QString("3915PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3923Plus, 0), QString("3923PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3924Plus, 0), QString("3924PLUS"));
    QCOMPARE(g_assemblyGraph->getCsvLine(node3940Plus, 0), QString("3940PLUS"));
}

void BandageTests::blastSearch()
{
    requireGraphLoad(testFile("test.fastg"));

    auto errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                        testFile("test_queries1.fasta"));

    QCOMPARE(errorString, "");

    search::Query * exact = g_graphSearch->getQueryFromName("test_query_exact");
    search::Query * one_mismatch = g_graphSearch->getQueryFromName("test_query_one_mismatch");
    search::Query * one_insertion = g_graphSearch->getQueryFromName("test_query_one_insertion");
    search::Query * one_deletion = g_graphSearch->getQueryFromName("test_query_one_deletion");

    QVERIFY(exact != nullptr);
    QVERIFY(one_mismatch != nullptr);
    QVERIFY(one_insertion != nullptr);
    QVERIFY(one_deletion != nullptr);
    QCOMPARE(exact->getLength(), 100);
    QCOMPARE(one_mismatch->getLength(), 100);
    QCOMPARE(one_insertion->getLength(), 101);
    QCOMPARE(one_deletion->getLength(), 99);

    const auto &exactHit = exact->getHits().at(0);
    const auto &one_mismatchHit = one_mismatch->getHits().at(0);
    const auto &one_insertionHit = one_insertion->getHits().at(0);
    const auto &one_deletionHit = one_deletion->getHits().at(0);

    QCOMPARE(exactHit->m_numberMismatches, 0);
    QCOMPARE(exactHit->m_numberGapOpens, 0);
    QCOMPARE(one_mismatchHit->m_numberMismatches, 1);
    QCOMPARE(one_mismatchHit->m_numberGapOpens, 0);
    QCOMPARE(one_insertionHit->m_numberMismatches, 0);
    QCOMPARE(one_insertionHit->m_numberGapOpens, 1);
    QCOMPARE(one_deletionHit->m_numberMismatches, 0);
    QCOMPARE(one_deletionHit->m_numberGapOpens, 1);

    QCOMPARE(exactHit->m_percentIdentity < 100.0, false);
    QCOMPARE(one_mismatchHit->m_percentIdentity < 100.0, true);
    QCOMPARE(one_insertionHit->m_percentIdentity < 100.0, true);
    QCOMPARE(one_deletionHit->m_percentIdentity < 100.0, true);
}



void BandageTests::blastSearchFilters()
{
    requireGraphLoad(testFile("test.fastg"));

    //First do the search with no filters
    auto errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                        testFile("test_queries2.fasta"));

    QCOMPARE(errorString, "");
    auto allHits = g_graphSearch->queries().allHits();
    int unfilteredHitCount = allHits.size();

    //Now filter by e-value.
    g_settings->evalueFilter.on = true;
    g_settings->evalueFilter = SciNot(1.0, -5);
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_queries2.fasta"));

    QCOMPARE(errorString, "");
    allHits = g_graphSearch->queries().allHits();
    QCOMPARE(allHits.size(), 14);
    QCOMPARE(allHits.size() < unfilteredHitCount, true);

    //Now add a bit score filter.
    g_settings->bitScoreFilter.on = true;
    g_settings->bitScoreFilter = 100.0;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_queries2.fasta"));

    QCOMPARE(errorString, "");
    allHits = g_graphSearch->queries().allHits();
    QCOMPARE(allHits.size(), 9);

    //Now add an alignment length filter.
    g_settings->alignmentLengthFilter.on = true;
    g_settings->alignmentLengthFilter = 100;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_queries2.fasta"));

    QCOMPARE(errorString, "");
    allHits = g_graphSearch->queries().allHits();
    QCOMPARE(allHits.size(), 8);

    //Now add an identity filter.
    g_settings->identityFilter.on = true;
    g_settings->identityFilter = 50.0;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_queries2.fasta"));

    QCOMPARE(errorString, "");
    allHits = g_graphSearch->queries().allHits();
    QCOMPARE(allHits.size(), 7);

    //Now add a query coverage filter.
    g_settings->queryCoverageFilter.on = true;
    g_settings->queryCoverageFilter = 90.0;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_queries2.fasta"));

    QCOMPARE(errorString, "");
    allHits = g_graphSearch->queries().allHits();
    QCOMPARE(allHits.size(), 5);
}



void BandageTests::graphScope()
{
    requireGraphLoad(testFile("test.fastg"));

    int drawnNodes;

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::wholeGraph();
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 44);
    }

    {
        g_settings->doubleMode = true;
        auto scope = graph::Scope::wholeGraph();
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));

        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 88);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::aroundNodes("1");
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 1);
    }

    {
        g_settings->doubleMode = true;
        auto scope = graph::Scope::aroundNodes("1");
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 2);
    }
    {
        g_settings->doubleMode = true;
        auto scope = graph::Scope::aroundNodes("1+");
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 1);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::aroundNodes("1", 1);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 3);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::aroundNodes("1", 2);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 10);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::depthRange(0.0, 211.0);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 43);
    }

    {
        g_settings->nodeDistance = 10; // this should be ignored everywhere
        g_settings->doubleMode = false;
        auto scope = graph::Scope::depthRange(0.0, 211.0);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 43);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::depthRange(211.0, 1000.0);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 1);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::depthRange(40.0, 211.0);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 42);
    }

    {
        auto errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                            testFile("test_queries1.fasta"));

        QCOMPARE(errorString, "");

        g_settings->doubleMode = false;
        auto scope = graph::Scope::aroundHits(g_graphSearch->queries(), "all");
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 1);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::aroundHits(g_graphSearch->queries(), "all", 1);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 3);
    }

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::aroundHits(g_graphSearch->queries(), "all", 2);
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        drawnNodes = g_assemblyGraph->getDrawnNodeCount();
        QCOMPARE(drawnNodes, 9);
    }

    GraphLayout layout(*g_assemblyGraph);
    layout::io::load(testFile("test.layout"), layout);
    QCOMPARE(layout.size(), 42);
    layout::apply(*g_assemblyGraph, layout);
    drawnNodes = g_assemblyGraph->getDrawnNodeCount();
    QCOMPARE(drawnNodes, 42);
}


void BandageTests::graphScopeComponent() {
    requireGraphLoad(testFile("test_components.gfa"));

    QString errorTitle;
    QString errorMessage;

    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::wholeGraph(), 6);
    }
    {
        g_settings->doubleMode = true;
        EXPECT_DRAWN(graph::Scope::wholeGraph(), 12);
    }

    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent("a1"), 3);
    }
    {
        g_settings->doubleMode = true;
        EXPECT_DRAWN(graph::Scope::aroundComponent("a1"), 6);
    }
    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent("a1,b1"), 5);
    }
    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent("c1"), 1);
    }
    {
        g_settings->doubleMode = true;
        EXPECT_DRAWN(graph::Scope::aroundComponent("c1"), 2);
    }
    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent({}, "PATH_A"), 3);
    }
    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent({}, "PATH_A,PATH_B"), 5);
    }
    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent({}, {}, "WALK_A"), 3);
    }
    {
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent({}, "PATH_A", "WALK_B"), 5);
    }
    {
        g_settings->nodeDistance = 10;
        g_settings->doubleMode = false;
        EXPECT_DRAWN(graph::Scope::aroundComponent("a1"), 3);
    }
    {
        g_settings->doubleMode = false;
        auto startingNodes = graph::getStartingNodes(*g_assemblyGraph,
                                                     graph::Scope::aroundComponent({}));
        QVERIFY(!startingNodes);
        bandage::handleAllErrors(startingNodes.takeError(), [&](const bandage::TextError &err) {
            errorTitle = QString::fromStdString(err.title());
            errorMessage = QString::fromStdString(err.text());
        });
        QVERIFY(!errorMessage.isEmpty());
    }
    {
        auto startingNodes = graph::getStartingNodes(*g_assemblyGraph,
                                                     graph::Scope::aroundComponent({}, "NO_SUCH_PATH"));
        QVERIFY(!startingNodes);
        bandage::handleAllErrors(startingNodes.takeError(), [&](const bandage::TextError &err) {
            errorTitle = QString::fromStdString(err.title());
            errorMessage = QString::fromStdString(err.text());
        });
        QCOMPARE(errorTitle, QString("Invalid path"));
        QCOMPARE(errorMessage, QString("No path with such name is loaded: NO_SUCH_PATH"));
    }
}


void BandageTests::graphLayout() {
    requireGraphLoad(testFile("test.fastg"));

    {
        g_settings->doubleMode = false;
        auto scope = graph::Scope::wholeGraph();
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        QCOMPARE(g_assemblyGraph->getDrawnNodeCount(), 44);

        auto layout = GraphLayoutWorker(g_settings->graphLayoutQuality,
                                        g_settings->linearLayout,
                                        g_settings->componentSeparation).layoutGraph(*g_assemblyGraph);

        QCOMPARE(layout.size(), 44);
    }

    {
        g_settings->doubleMode = true;
        auto scope = graph::Scope::wholeGraph();
        REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
        g_assemblyGraph->resetNodes();
        g_assemblyGraph->markNodesToDraw(scope, startingNodes);
        QCOMPARE(g_assemblyGraph->getDrawnNodeCount(), 88);

        auto layout = GraphLayoutWorker(g_settings->graphLayoutQuality,
                                        g_settings->linearLayout,
                                        g_settings->componentSeparation).layoutGraph(*g_assemblyGraph);

        QCOMPARE(layout.size(), 88);
    }
}

static void parseSettings(const QStringList &commandLineSettings) {
    std::vector<std::string> strings;
    std::vector<const char*> argv;
    argv.push_back("BandateTests");
    for (const auto &s : commandLineSettings)
        strings.emplace_back(s.toStdString());
    for (const auto &s : strings)
        argv.push_back(s.c_str());

    CLI::App app;
    addSettings(app);

#if QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
    try {
        app.parse(argv.size(), argv.data());
    } catch (const CLI::ParseError &e) {
        QFAIL(e.what());
    }
#else
    QVERIFY_THROWS_NO_EXCEPTION(app.parse(argv.size(), argv.data()));
#endif
}

static void parseSettingsExpectError(const QStringList &commandLineSettings) {
    std::vector<std::string> strings;
    std::vector<const char*> argv;
    argv.push_back("BandateTests");
    for (const auto &s : commandLineSettings)
        strings.emplace_back(s.toStdString());
    for (const auto &s : strings)
        argv.push_back(s.c_str());

    CLI::App app;
    addSettings(app);
    QVERIFY_THROWS_EXCEPTION(CLI::ParseError, app.parse(argv.size(), argv.data()));
}


void BandageTests::commandLineSettings() {
    QStringList commandLineSettings;

    commandLineSettings = QString("--scope entire").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, WHOLE_GRAPH);

    commandLineSettings = QString("--scope aroundnodes --nodes 1,2,3").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, AROUND_NODE);

    commandLineSettings = QString("--scope aroundcomponent --nodes 1").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, AROUND_COMPONENT);
    QCOMPARE(g_settings->startingNodes, QString("1"));

    commandLineSettings = QString("--scope aroundcomponent --path P1,P2 --walk W1").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, AROUND_COMPONENT);
    QCOMPARE(g_settings->startingPaths, QString("P1,P2"));
    QCOMPARE(g_settings->startingWalks, QString("W1"));

    commandLineSettings = QString("--scope depthrange --mindepth 1 --maxdepth 10").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, DEPTH_RANGE);

    commandLineSettings = QString("--nodes 5+").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->startingNodes, QString("5+"));

    commandLineSettings = QString("--nodes 1,2,3").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->startingNodes, QString("1,2,3"));

    QCOMPARE(g_settings->startingNodesExactMatch, true);
    commandLineSettings = QString("--partial").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->startingNodesExactMatch, false);

    commandLineSettings = QString("--distance 12").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->nodeDistance.val, 12);

    commandLineSettings = QString("--mindepth 1.2").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minDepthRange.val, 1.2);

    commandLineSettings = QString("--maxdepth 2.1").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxDepthRange.val, 2.1);

    QCOMPARE(g_settings->doubleMode, false);
    commandLineSettings = QString("--double").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->doubleMode, true);

    QCOMPARE(g_settings->nodeLengthMode, AUTO_NODE_LENGTH);
    commandLineSettings = QString("--nodelen 10000").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->nodeLengthMode, MANUAL_NODE_LENGTH);
    QCOMPARE(g_settings->manualNodeLengthPerMegabase.val, 10000.0);

    commandLineSettings = QString("--iter 1").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphLayoutQuality.val, 1);

    commandLineSettings = QString("--nodewidth 4.2").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->averageNodeWidth.val, 4.2);

    commandLineSettings = QString("--depwidth 0.222").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->depthEffectOnWidth.val, 0.222);

    commandLineSettings = QString("--deppower 0.72").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->depthPower.val, 0.72);

    QCOMPARE(g_settings->displayNodeNames, false);
    commandLineSettings = QString("--names").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->displayNodeNames, true);

    QCOMPARE(g_settings->displayNodeLengths, false);
    commandLineSettings = QString("--lengths").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->displayNodeLengths, true);

    QCOMPARE(g_settings->displayNodeDepth, false);
    commandLineSettings = QString("--depth").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->displayNodeDepth, true);

    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, false);
    commandLineSettings = QString("--blasthits").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, true);

    commandLineSettings = QString("--fontsize 5").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->labelFont.pointSize(), 5);

    commandLineSettings = QString("--edgecol #00ff00").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->edgeColour.name(), QString("#00ff00"));

    commandLineSettings = QString("--edgewidth 5.5").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->edgeWidth.val, 5.5);

    commandLineSettings = QString("--outcol #ff0000").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->outlineColour.name(), QString("#ff0000"));

    commandLineSettings = QString("--outline 0.123").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->outlineThickness.val, 0.123);

    commandLineSettings = QString("--selcol tomato").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->selectionColour.name(), QString("#ff6347"));

    QCOMPARE(g_settings->antialiasing, true);
    commandLineSettings = QString("--noaa").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->antialiasing, false);

    commandLineSettings = QString("--textcol #550000ff").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->textColour.name(), QString("#0000ff"));
    QCOMPARE(g_settings->textColour.alpha(), 85);

    commandLineSettings = QString("--toutcol steelblue").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(getColourName(g_settings->textOutlineColour), QString("steelblue"));

    commandLineSettings = QString("--toutline 0.321").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->textOutlineThickness.val, 0.321);

    QCOMPARE(g_settings->positionTextNodeCentre, false);
    commandLineSettings = QString("--centre").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->positionTextNodeCentre, true);

    commandLineSettings = QString("--colour random").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->nodeColorer->scheme(), RANDOM_COLOURS);

    commandLineSettings = QString("--colour uniform").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->nodeColorer->scheme(), UNIFORM_COLOURS);

    commandLineSettings = QString("--colour depth").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->nodeColorer->scheme(), DEPTH_COLOUR);

    commandLineSettings = QString("--ransatpos 12").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->randomColourPositiveSaturation.val, 12);

    commandLineSettings = QString("--ransatneg 23").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->randomColourNegativeSaturation.val, 23);

    commandLineSettings = QString("--ranligpos 34").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->randomColourPositiveLightness.val, 34);

    commandLineSettings = QString("--ranligneg 45").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->randomColourNegativeLightness.val, 45);

    commandLineSettings = QString("--ranopapos 56").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->randomColourPositiveOpacity.val, 56);

    commandLineSettings = QString("--ranopaneg 67").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->randomColourNegativeOpacity.val, 67);

    commandLineSettings = QString("--unicolpos springgreen").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(getColourName(g_settings->uniformPositiveNodeColour), QString("springgreen"));

    commandLineSettings = QString("--unicolneg teal").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(getColourName(g_settings->uniformNegativeNodeColour), QString("teal"));

    commandLineSettings = QString("--unicolspe papayawhip").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(getColourName(g_settings->uniformNodeSpecialColour), QString("papayawhip"));

    commandLineSettings = QString("--colormap github").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(getColorMapName(g_settings->colorMap), QString("github"));

    commandLineSettings = QString("--colormap heat").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->colorMap, ColorMap::Heat);

    QCOMPARE(g_settings->autoDepthValue, true);
    commandLineSettings = QString("--depvallow 46.7").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->lowDepthValue.val, 46.7);
    QCOMPARE(g_settings->autoDepthValue, false);

    commandLineSettings = QString("--depvalhi 67.8").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->highDepthValue.val, 67.8);

    commandLineSettings = QString("--depvalhi 67.8").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->highDepthValue.val, 67.8);

    commandLineSettings = QString("--blastp --abc").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->searchParameters, QString("--abc"));

    QCOMPARE(g_settings->alignmentLengthFilter.on, false);
    commandLineSettings = QString("--alfilter 543").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->alignmentLengthFilter.on, true);
    QCOMPARE(g_settings->alignmentLengthFilter.val, 543);

    QCOMPARE(g_settings->queryCoverageFilter.on, false);
    commandLineSettings = QString("--qcfilter 67.8").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->queryCoverageFilter.on, true);
    QCOMPARE(g_settings->queryCoverageFilter.val, 67.8);

    QCOMPARE(g_settings->identityFilter.on, false);
    commandLineSettings = QString("--ifilter 12.3").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->identityFilter.on, true);
    QCOMPARE(g_settings->identityFilter.val, 12.3);

    QCOMPARE(g_settings->evalueFilter.on, false);
    commandLineSettings = QString("--evfilter 8.5e-14").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->evalueFilter.on, true);
    QCOMPARE(g_settings->evalueFilter.val, SciNot(8.5, -14));
    QCOMPARE(g_settings->evalueFilter.val.getCoefficient(), 8.5);
    QCOMPARE(g_settings->evalueFilter.val.getExponent(), -14);

    QCOMPARE(g_settings->bitScoreFilter.on, false);
    commandLineSettings = QString("--bsfilter 1234.5").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->bitScoreFilter.on, true);
    QCOMPARE(g_settings->bitScoreFilter.val, 1234.5);

    commandLineSettings = QString("--pathnodes 3").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxQueryPathNodes.val, 3);

    commandLineSettings = QString("--minpatcov 0.543").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minQueryCoveredByPath.val, 0.543);

    commandLineSettings = QString("--minhitcov 0.654").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minQueryCoveredByHits.val, 0.654);
    QCOMPARE(g_settings->minQueryCoveredByHits.on, true);

    commandLineSettings = QString("--minhitcov off").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minQueryCoveredByHits.on, false);

    commandLineSettings = QString("--minmeanid 0.765").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minMeanHitIdentity.val, 0.765);
    QCOMPARE(g_settings->minMeanHitIdentity.on, true);

    commandLineSettings = QString("--minmeanid off").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minMeanHitIdentity.on, false);

    commandLineSettings = QString("--minpatlen 0.97").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minLengthPercentage.val, 0.97);
    QCOMPARE(g_settings->minLengthPercentage.on, true);

    commandLineSettings = QString("--minpatlen off").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minLengthPercentage.on, false);

    commandLineSettings = QString("--maxpatlen 1.03").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxLengthPercentage.val, 1.03);
    QCOMPARE(g_settings->maxLengthPercentage.on, true);

    commandLineSettings = QString("--maxpatlen off").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxLengthPercentage.on, false);

    commandLineSettings = QString("--minlendis -1234").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minLengthBaseDiscrepancy.val, -1234);
    QCOMPARE(g_settings->minLengthBaseDiscrepancy.on, true);

    commandLineSettings = QString("--minlendis off").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->minLengthBaseDiscrepancy.on, false);

    commandLineSettings = QString("--maxlendis 4321").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxLengthBaseDiscrepancy.val, 4321);
    QCOMPARE(g_settings->maxLengthBaseDiscrepancy.on, true);

    commandLineSettings = QString("--maxlendis off").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxLengthBaseDiscrepancy.on, false);

    commandLineSettings = QString("--maxevprod 4e-500").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxEValueProduct.val.getCoefficient(), 4.0);
    QCOMPARE(g_settings->maxEValueProduct.val.getExponent(), -500);
    QCOMPARE(g_settings->maxEValueProduct.on, true);

    commandLineSettings = QString("--maxevprod off").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->maxEValueProduct.on, false);

    // --- Legacy option regression (old names only; no --searcher/--searchp/--labelhits/--aroundhits) ---
    init();
    QCOMPARE(g_settings->graphSearcher, search::BLAST);
    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, false);
    QCOMPARE(g_settings->searchParameters, QString());
    QCOMPARE(g_settings->aroundBlastScopeAlias, false);

    commandLineSettings = QString("--blasthits").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, true);
    QCOMPARE(g_settings->graphSearcher, search::BLAST);

    init();
    commandLineSettings = QString("--blastp --abc").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->searchParameters, QString("--abc"));
    QCOMPARE(g_settings->graphSearcher, search::BLAST);

    init();
    commandLineSettings = QStringList{"--scope", "aroundblast", "--query", testFile("test_queries1.fasta")};
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, AROUND_QUERY_HITS);
    QCOMPARE(g_settings->aroundBlastScopeAlias, true);
    QCOMPARE(g_settings->graphSearcher, search::BLAST);

    init();
    // --blastp takes one TEXT token (use --evalue=1 so CLI11 does not treat -e as a flag)
    commandLineSettings = QStringList{
            "--scope", "aroundblast",
            "--query", testFile("test_queries1.fasta"),
            "--blasthits",
            "--blastp", "--evalue=1"};
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, AROUND_QUERY_HITS);
    QCOMPARE(g_settings->aroundBlastScopeAlias, true);
    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, true);
    QCOMPARE(g_settings->searchParameters, QString("--evalue=1"));
    QCOMPARE(g_settings->graphSearcher, search::BLAST);

    // Graph searcher / label-hits CLI
    init();
    QCOMPARE(g_settings->graphSearcher, search::BLAST);
    commandLineSettings = QString("--searcher minimap2").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphSearcher, search::Minimap2);

    commandLineSettings = QString("--searcher blast").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphSearcher, search::BLAST);

    g_settings->defaultQueryHitAnnotationSetting.showText = false;
    commandLineSettings = QString("--labelhits").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, true);

    commandLineSettings = QString("--searchp --xyz").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->searchParameters, QString("--xyz"));

    commandLineSettings = QStringList{"--searchp=-x sr --for-only"};
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->searchParameters, QString("-x sr --for-only"));

    commandLineSettings = QStringList{"--scope", "aroundhits", "--query", testFile("test_queries1.fasta")};
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, AROUND_QUERY_HITS);
    QCOMPARE(g_settings->aroundBlastScopeAlias, false);

    commandLineSettings = QStringList{"--scope", "aroundblast", "--query", testFile("test_queries1.fasta")};
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphScope, AROUND_QUERY_HITS);
    QCOMPARE(g_settings->aroundBlastScopeAlias, true);

    commandLineSettings = QString("--searcher blast --blasthits").split(" ");
    parseSettings(commandLineSettings);
    QCOMPARE(g_settings->graphSearcher, search::BLAST);
    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, true);

    parseSettingsExpectError(QString("--searcher minimap2 --blasthits").split(" "));
    parseSettingsExpectError(QString("--searcher minimap2 --blastp --abc").split(" "));
    parseSettingsExpectError(QStringList{"--searcher", "minimap2", "--scope", "aroundblast",
                                         "--query", testFile("test_queries1.fasta")});
    parseSettingsExpectError(QString("--blastp --abc --searchp --xyz").split(" "));

    // --labelhits with Minimap2: updateGroupFromHits applies showText to that group
    init();
    requireGraphLoad(testFile("test.gfa"));
    parseSettings(QString("--searcher minimap2 --labelhits").split(" "));
    QCOMPARE(g_settings->graphSearcher, search::Minimap2);
    QCOMPARE(g_settings->defaultQueryHitAnnotationSetting.showText, true);
    {
        DeBruijnNode *node = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
        QVERIFY(node != nullptr);
        auto *query = new search::Query("q1", "ACGT");
        query->addHit(new search::Hit(query, node, 100.0, 4, 0, 0, 1, 4, 1, 4,
                                      SciNot(1.0, -10), 10.0));
        g_annotationsManager->updateGroupFromHits("Minimap2 hits", {query});
        const auto *group = g_annotationsManager->findGroupByName("Minimap2 hits");
        QVERIFY(group != nullptr);
        QCOMPARE(g_settings->annotationsSettings[group->id].showText, true);
        delete query;
    }
}


void BandageTests::sciNotComparisons()
{
    SciNot sn01(1.0, 10);
    SciNot sn02(10.0, 9);
    SciNot sn03(0.1, 11);
    SciNot sn04(5.0, 10);
    SciNot sn05(-5.0, 15);
    SciNot sn06(-6.0, 15);
    SciNot sn07(-3.0, 3);
    SciNot sn08(-0.3, 4);
    SciNot sn09(-3.0, -3);
    SciNot sn10(-0.3, -2);
    SciNot sn11(1.4, 2);
    SciNot sn12("1.4e2");
    SciNot sn13("140");

    QCOMPARE(sn01 == sn02, true);
    QCOMPARE(sn01 == sn03, true);
    QCOMPARE(sn01 == sn03, true);
    QCOMPARE(sn01 >= sn03, true);
    QCOMPARE(sn01 <= sn03, true);
    QCOMPARE(sn01 != sn03, false);
    QCOMPARE(sn01 < sn04, true);
    QCOMPARE(sn01 <= sn04, true);
    QCOMPARE(sn01 > sn04, false);
    QCOMPARE(sn01 >= sn04, false);
    QCOMPARE(sn04 > sn05, true);
    QCOMPARE(sn04 < sn05, false);
    QCOMPARE(sn06 < sn05, true);
    QCOMPARE(sn06 <= sn05, true);
    QCOMPARE(sn06 > sn05, false);
    QCOMPARE(sn06 >= sn05, false);
    QCOMPARE(sn07 == sn08, true);
    QCOMPARE(sn07 != sn08, false);
    QCOMPARE(sn09 == sn10, true);
    QCOMPARE(sn09 != sn10, false);
    QCOMPARE(sn11 == sn12, true);
    QCOMPARE(sn11 == sn13, true);
    QCOMPARE(sn01.asString(true), QString("1e10"));
    QCOMPARE(sn01.asString(false), QString("1e10"));
    QCOMPARE(sn11.asString(true), QString("1.4e2"));
    QCOMPARE(sn11.asString(false), QString("140"));
}


void BandageTests::graphEdits()
{
    requireGraphLoad(testFile("test.fastg"));

    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 88);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 59);

    //Get the path sequence now to compare to the merged node sequence at the
    //end.
    REQUIRE_OK(path, Path::makeFromString("6+, 26+, 23+, 26+, 24+", *g_assemblyGraph, false));
    QByteArray pathSequence = path.getPathSequence();

    g_assemblyGraph->duplicateNodePair(g_assemblyGraph->m_deBruijnGraphNodes["26+"], 0);

    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 90);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 63);

    std::vector<DeBruijnEdge *> edgesToRemove;
    edgesToRemove.push_back(getEdgeFromNodeNames("26_copy+", "24+"));
    edgesToRemove.push_back(getEdgeFromNodeNames("6+", "26+"));
    edgesToRemove.push_back(getEdgeFromNodeNames("26+", "23+"));
    edgesToRemove.push_back(getEdgeFromNodeNames("23+", "26_copy+"));
    g_assemblyGraph->deleteEdges(edgesToRemove);

    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 90);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 59);

    g_assemblyGraph->mergeAllPossible();

    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 82);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 55);

    DeBruijnNode * mergedNode = g_assemblyGraph->m_deBruijnGraphNodes["6_26_copy_23_26_24+"];
    if (mergedNode == nullptr)
        mergedNode = g_assemblyGraph->m_deBruijnGraphNodes["24_26_23_26_copy_6-"];

    QVERIFY(mergedNode != nullptr);
    QCOMPARE(Sequence(pathSequence), mergedNode->getSequence());
}



void BandageTests::fastgToGfa()
{
    //First load the graph as a FASTG and pull out some information and a
    //path sequence.
    requireGraphLoad(testFile("test.fastg"));

    int fastgNodeCount = g_assemblyGraph->m_nodeCount;
    int fastgEdgeCount= g_assemblyGraph->m_edgeCount;
    long long fastgTotalLength = g_assemblyGraph->m_totalLength;
    long long fastgShortestContig = g_assemblyGraph->m_shortestContig;
    long long fastgLongestContig = g_assemblyGraph->m_longestContig;
    REQUIRE_OK(fastgTestPath1, Path::makeFromString("24+, 14+, 39-, 43-, 42-, 2-, 4+, 33+, 35-, 31-, 44-, 27+, 9-, 28-, 44+, 31+, 36+", *g_assemblyGraph, false));
    REQUIRE_OK(fastgTestPath2, Path::makeFromString("16+, 7+, 13+, 37+, 41-, 40-, 42+, 43+, 39+, 14-, 22-, 20+, 7-, 25-, 32-, 38+, 3-", *g_assemblyGraph, false));
    QByteArray fastgTestPath1Sequence = fastgTestPath1.getPathSequence();
    QByteArray fastgTestPath2Sequence = fastgTestPath2.getPathSequence();

    //Now save the graph as a GFA and reload it and grab the same information.
    QVERIFY(gfa::saveEntireGraph(tempFile("test_temp.gfa"), *g_assemblyGraph));
    requireGraphLoad(tempFile("test_temp.gfa"));

    int gfaNodeCount = g_assemblyGraph->m_nodeCount;
    int gfaEdgeCount= g_assemblyGraph->m_edgeCount;
    long long gfaTotalLength = g_assemblyGraph->m_totalLength;
    long long gfaShortestContig = g_assemblyGraph->m_shortestContig;
    long long gfaLongestContig = g_assemblyGraph->m_longestContig;

    REQUIRE_OK(gfaTestPath1, Path::makeFromString("24+, 14+, 39-, 43-, 42-, 2-, 4+, 33+, 35-, 31-, 44-, 27+, 9-, 28-, 44+, 31+, 36+", *g_assemblyGraph, false));
    REQUIRE_OK(gfaTestPath2, Path::makeFromString("16+, 7+, 13+, 37+, 41-, 40-, 42+, 43+, 39+, 14-, 22-, 20+, 7-, 25-, 32-, 38+, 3-", *g_assemblyGraph, false));
    QByteArray gfaTestPath1Sequence = gfaTestPath1.getPathSequence();
    QByteArray gfaTestPath2Sequence = gfaTestPath2.getPathSequence();

    //Now we compare the LastGraph info to the GFA info to make sure they are
    //the same (or appropriately different).  The k-mer size for this graph is
    //51, so we expect each node to get 50 base pairs longer.
    QCOMPARE(fastgNodeCount, gfaNodeCount);
    QCOMPARE(fastgEdgeCount, gfaEdgeCount);
    QCOMPARE(fastgTotalLength, gfaTotalLength);
    QCOMPARE(fastgShortestContig, gfaShortestContig);
    QCOMPARE(fastgLongestContig, gfaLongestContig);
    QCOMPARE(fastgTestPath1Sequence, gfaTestPath1Sequence);
    QCOMPARE(fastgTestPath2Sequence, gfaTestPath2Sequence);
}


void BandageTests::mergeNodesOnGfa()
{
    requireGraphLoad(testFile("test_plasmids.gfa"));

    DeBruijnNode * node6Plus = g_assemblyGraph->m_deBruijnGraphNodes["6+"];
    DeBruijnNode * node280Plus = g_assemblyGraph->m_deBruijnGraphNodes["280+"];
    DeBruijnNode * node232Minus = g_assemblyGraph->m_deBruijnGraphNodes["232-"];
    DeBruijnNode * node333Plus = g_assemblyGraph->m_deBruijnGraphNodes["333+"];
    DeBruijnNode * node289Plus = g_assemblyGraph->m_deBruijnGraphNodes["289+"];
    DeBruijnNode * node283Plus = g_assemblyGraph->m_deBruijnGraphNodes["283+"];
    DeBruijnNode * node277Plus = g_assemblyGraph->m_deBruijnGraphNodes["277+"];
    DeBruijnNode * node297Plus = g_assemblyGraph->m_deBruijnGraphNodes["297+"];
    DeBruijnNode * node282Plus = g_assemblyGraph->m_deBruijnGraphNodes["282+"];

    //Create a path before merging the nodes.
    REQUIRE_OK(testPath1, Path::makeFromString("6+, 280+, 232-, 333+, 289+, 283+", *g_assemblyGraph, true));
    QByteArray path1Sequence = testPath1.getPathSequence();

    int path1Length = testPath1.getLength();
    int nodeTotalLength = node6Plus->getLength() + node280Plus->getLength() + node232Minus->getLength() +
            node333Plus->getLength() + node289Plus->getLength() + node283Plus->getLength();

    //The k-mer size in this test is 81, so the path should remove all of those overlaps.
    QCOMPARE(path1Length, nodeTotalLength - 6 * 81);

    //Now remove excess nodes.
    std::vector<DeBruijnNode*> nodesToDelete;
    nodesToDelete.push_back(node277Plus);
    nodesToDelete.push_back(node297Plus);
    nodesToDelete.push_back(node282Plus);
    g_assemblyGraph->deleteNodes(nodesToDelete);

    //There should now be six nodes in the graph (plus complements).
    QCOMPARE(12, g_assemblyGraph->m_deBruijnGraphNodes.size());

    //After a merge, there should be only one node (and its complement).
    g_assemblyGraph->mergeAllPossible();
    QCOMPARE(2, g_assemblyGraph->m_deBruijnGraphNodes.size());

    //That last node should have a length of its six constituent nodes, minus
    //the overlaps.
    DeBruijnNode * lastNode = *g_assemblyGraph->m_deBruijnGraphNodes.begin();
    QCOMPARE(lastNode->getLength(), nodeTotalLength - 5 * 81);

    //If we make a circular path with this node, its length should be equal to
    //the length of the path made before.
    REQUIRE_OK(testPath2, Path::makeFromString(QString::fromStdString(lastNode->getName()), *g_assemblyGraph, true));
    QCOMPARE(path1Length, testPath2.getLength());

    //The sequence of this second path should also match the sequence of the
    //first path.
    QByteArray path2Sequence = testPath2.getPathSequence();
    QCOMPARE(doCircularSequencesMatch(path1Sequence, path2Sequence), true);
}



void BandageTests::changeNodeNames()
{
    requireGraphLoad(testFile("test.fastg"));

    DeBruijnNode * node6Plus = g_assemblyGraph->m_deBruijnGraphNodes["6+"];
    DeBruijnNode * node6Minus = g_assemblyGraph->m_deBruijnGraphNodes["6-"];
    int nodeCountBefore = g_assemblyGraph->m_deBruijnGraphNodes.size();

    g_assemblyGraph->changeNodeName("6", "12345");

    DeBruijnNode * node12345Plus = g_assemblyGraph->m_deBruijnGraphNodes["12345+"];
    DeBruijnNode * node12345Minus = g_assemblyGraph->m_deBruijnGraphNodes["12345-"];
    int nodeCountAfter = g_assemblyGraph->m_deBruijnGraphNodes.size();

    QCOMPARE(node6Plus, node12345Plus);
    QCOMPARE(node6Minus, node12345Minus);
    QCOMPARE(nodeCountBefore, nodeCountAfter);
    QCOMPARE(node12345Plus->getNameWithoutSign(), std::string_view("12345"));
    QVERIFY(node12345Plus->isPositiveNode());
    QVERIFY(node12345Minus->isNegativeNode());

    QCOMPARE(g_assemblyGraph->checkNodeNameValidity(""), NODE_NAME_EMPTY);
    g_assemblyGraph->changeNodeName("12345", "");
    QCOMPARE(node12345Plus->getNameWithoutSign(), std::string_view("12345"));
    QVERIFY(node12345Plus->isPositiveNode());
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.count("12345+"), 1u);
}

void BandageTests::changeNodeDepths()
{
    requireGraphLoad(testFile("test.fastg"));

    DeBruijnNode * node6Plus = g_assemblyGraph->m_deBruijnGraphNodes["6+"];
    DeBruijnNode * node6Minus = g_assemblyGraph->m_deBruijnGraphNodes["6-"];
    DeBruijnNode * node7Plus = g_assemblyGraph->m_deBruijnGraphNodes["7+"];
    DeBruijnNode * node7Minus = g_assemblyGraph->m_deBruijnGraphNodes["7-"];

    std::vector<DeBruijnNode *> nodes;
    nodes.push_back(node6Plus);
    nodes.push_back(node7Plus);

    //Complementary pairs should have the same depth.
    QCOMPARE(node6Plus->getDepth(), node6Minus->getDepth());
    QCOMPARE(node7Plus->getDepth(), node7Minus->getDepth());

    g_assemblyGraph->changeNodeDepth(nodes, 0.5);

    //Check to make sure the change worked.
    QCOMPARE(0.5, node6Plus->getDepth());
    QCOMPARE(0.5, node6Minus->getDepth());
    QCOMPARE(0.5, node7Plus->getDepth());
    QCOMPARE(0.5, node7Minus->getDepth());
}

void BandageTests::blastQueryPaths() {
    requireGraphLoad(testFile("test_query_paths.gfa"));

    Settings defaultSettings;
    g_settings->queryFilename = testFile("test_query_paths.fasta");
    defaultSettings.queryFilename = testFile("test_query_paths.fasta");

    //Now filter by e-value to get only strong hits and do the BLAST search.
    g_settings->evalueFilter.on = true;
    g_settings->evalueFilter = SciNot(1.0, -5);

    //With the default settings, queries 1 to 5 should each have one path and
    //queries 6 and 7 should have 0 (because of their large inserts).
    auto errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                        testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    auto query1Paths = g_graphSearch->query(0)->getPaths();
    auto query2Paths = g_graphSearch->query(1)->getPaths();
    auto query3Paths = g_graphSearch->query(2)->getPaths();
    auto query4Paths = g_graphSearch->query(3)->getPaths();
    auto query5Paths = g_graphSearch->query(4)->getPaths();
    auto query6Paths = g_graphSearch->query(5)->getPaths();
    auto query7Paths = g_graphSearch->query(6)->getPaths();
    QCOMPARE(query1Paths.size(), 1);
    QCOMPARE(query2Paths.size(), 1);
    QCOMPARE(query3Paths.size(), 1);
    QCOMPARE(query4Paths.size(), 1);
    QCOMPARE(query5Paths.size(), 1);
    QCOMPARE(query6Paths.size(), 0);
    QCOMPARE(query7Paths.size(), 0);

    //query2 has a mean hit identity of 0.98.
    g_settings->minMeanHitIdentity.on = true;
    g_settings->minMeanHitIdentity = 0.979;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query2Paths = g_graphSearch->query(1)->getPaths();
    QCOMPARE(query2Paths.size(), 1);
    g_settings->minMeanHitIdentity = 0.981;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query2Paths = g_graphSearch->query(1)->getPaths();
    QCOMPARE(query2Paths.size(), 0);

    //Turning the filter off should make the path return.
    g_settings->minMeanHitIdentity.on = false;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query2Paths = g_graphSearch->query(1)->getPaths();
    QCOMPARE(query2Paths.size(), 1);
    *g_settings = defaultSettings;

    //query3 has a length discrepancy of -20.
    g_settings->minLengthBaseDiscrepancy.on = true;
    g_settings->minLengthBaseDiscrepancy = -20;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query3Paths = g_graphSearch->query(2)->getPaths();
    QCOMPARE(query3Paths.size(), 1);
    g_settings->minLengthBaseDiscrepancy = -19;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query3Paths = g_graphSearch->query(2)->getPaths();
    QCOMPARE(query3Paths.size(), 0);

    //Turning the filter off should make the path return.
    g_settings->minLengthBaseDiscrepancy.on = false;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query3Paths = g_graphSearch->query(2)->getPaths();
    QCOMPARE(query3Paths.size(), 1);
    *g_settings = defaultSettings;

    //query4 has a length discrepancy of +20.
    g_settings->maxLengthBaseDiscrepancy.on = true;
    g_settings->maxLengthBaseDiscrepancy = 20;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query4Paths = g_graphSearch->query(3)->getPaths();
    QCOMPARE(query4Paths.size(), 1);
    g_settings->maxLengthBaseDiscrepancy = 19;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query4Paths = g_graphSearch->query(3)->getPaths();
    QCOMPARE(query4Paths.size(), 0);

    //Turning the filter off should make the path return.
    g_settings->maxLengthBaseDiscrepancy.on = false;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query4Paths = g_graphSearch->query(3)->getPaths();
    QCOMPARE(query4Paths.size(), 1);
    *g_settings = defaultSettings;

    //query5 has a path through 4 nodes.
    g_settings->maxQueryPathNodes = 4;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query5Paths = g_graphSearch->query(4)->getPaths();
    QCOMPARE(query5Paths.size(), 1);
    g_settings->maxQueryPathNodes = 3;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query5Paths = g_graphSearch->query(4)->getPaths();
    QCOMPARE(query5Paths.size(), 0);
    *g_settings = defaultSettings;

    //By turning off length restrictions, queries 6 and 7 should get path with
    //a large insert in the middle.
    g_settings->minLengthPercentage.on = false;
    g_settings->maxLengthPercentage.on = false;
    g_settings->minLengthBaseDiscrepancy.on = false;
    g_settings->maxLengthBaseDiscrepancy.on = false;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query6Paths = g_graphSearch->query(5)->getPaths();
    query7Paths = g_graphSearch->query(6)->getPaths();
    QCOMPARE(query6Paths.size(), 1);
    QCOMPARE(query7Paths.size(), 1);

    //Adjusting on the max length restriction can allow query 6 to get a path
    //and then query 7.
    g_settings->maxLengthBaseDiscrepancy.on = true;
    g_settings->maxLengthBaseDiscrepancy = 1999;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query6Paths = g_graphSearch->query(5)->getPaths();
    query7Paths = g_graphSearch->query(6)->getPaths();
    QCOMPARE(query6Paths.size(), 1);
    QCOMPARE(query7Paths.size(), 0);
    g_settings->maxLengthBaseDiscrepancy = 2000;
    errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                   testFile("test_query_paths.fasta"));
    QCOMPARE(errorString, "");
    query6Paths = g_graphSearch->query(5)->getPaths();
    query7Paths = g_graphSearch->query(6)->getPaths();
    QCOMPARE(query6Paths.size(), 1);
    QCOMPARE(query7Paths.size(), 1);
}


void BandageTests::bandageInfo()
{
    int n50 = 0;
    int shortestNode = 0;
    int firstQuartile = 0;
    int median = 0;
    int thirdQuartile = 0;
    int longestNode = 0;
    int componentCount = 0;
    int largestComponentLength = 0;

    requireGraphLoad(testFile("test.fastg"));
    g_assemblyGraph->getNodeStats(&n50, &shortestNode, &firstQuartile, &median, &thirdQuartile, &longestNode);
    g_assemblyGraph->getGraphComponentCountAndLargestComponentSize(&componentCount, &largestComponentLength);
    QCOMPARE(44, g_assemblyGraph->m_nodeCount);
    QCOMPARE(59, g_assemblyGraph->m_edgeCount);
    QCOMPARE(214441, g_assemblyGraph->m_totalLength);
    QCOMPARE(0, g_assemblyGraph->getDeadEndCount());
    QCOMPARE(35628, n50);
    QCOMPARE(78, shortestNode);
    QCOMPARE(52213, longestNode);
    QCOMPARE(1, componentCount);
    QCOMPARE(214441, largestComponentLength);

    requireGraphLoad(testFile("test.Trinity.fasta"));
    g_assemblyGraph->getNodeStats(&n50, &shortestNode, &firstQuartile, &median, &thirdQuartile, &longestNode);
    g_assemblyGraph->getGraphComponentCountAndLargestComponentSize(&componentCount, &largestComponentLength);
    QCOMPARE(149, g_assemblyGraph->getDeadEndCount());
    QCOMPARE(66, componentCount);
    QCOMPARE(9398, largestComponentLength);

    requireGraphLoad(testFile("test.gfa"));
    g_assemblyGraph->getNodeStats(&n50, &shortestNode, &firstQuartile, &median, &thirdQuartile, &longestNode);
    g_assemblyGraph->getGraphComponentCountAndLargestComponentSize(&componentCount, &largestComponentLength);
    QCOMPARE(17, g_assemblyGraph->m_nodeCount);
    QCOMPARE(16, g_assemblyGraph->m_edgeCount);
    QCOMPARE(30959, g_assemblyGraph->m_totalLength);
    QCOMPARE(10, g_assemblyGraph->getDeadEndCount());
    QCOMPARE(2060, n50);
    QCOMPARE(119, shortestNode);
    QCOMPARE(2060, longestNode);
    QCOMPARE(1, componentCount);
    QCOMPARE(30959, largestComponentLength);
}

void BandageTests::sequenceInit() {
    Sequence sequenceFromString{"ATGC"};
    Sequence sequenceFromQByteArray{QByteArray{"ATGC"}};
    Sequence sequenceRevComp{"GCAT", true};
    Sequence sequenceFromStringLower{"atgc"};

    QCOMPARE(sequenceFromQByteArray, sequenceFromString);
    QCOMPARE(sequenceFromString, sequenceRevComp);
    QCOMPARE(sequenceRevComp, sequenceFromStringLower);
    QCOMPARE(sequenceFromStringLower, sequenceFromQByteArray);
}

void BandageTests::sequenceAllNs() {
    Sequence allNs(1000, /* allNs */ true);

    QVERIFY(allNs.missing());
    QCOMPARE(allNs.size(), 1000u);
    QCOMPARE(allNs.capacity(), 0u);
    QCOMPARE(allNs[0], 'N');
    QCOMPARE(allNs[999], 'N');

    Sequence another(1000, /* allNs */ true);
    QCOMPARE(allNs, another);
    QCOMPARE(allNs, allNs.GetReverseComplement());

    Sequence sub = allNs.Subseq(10, 14);
    QCOMPARE(sub.size(), 4u);
    QVERIFY(sub.missing());
    QCOMPARE(sub.capacity(), 0u);
    QCOMPARE(sub[0], 'N');
    QCOMPARE(sub[3], 'N');
    QCOMPARE(sub, sub.GetReverseComplement());

    Sequence mixed{"ACNGT"};
    QVERIFY(!mixed.missing());
    QVERIFY(mixed.capacity() > 0);
    QCOMPARE(mixed[0], 'A');
    QCOMPARE(mixed[2], 'N');
    QCOMPARE(mixed[4], 'T');

    // Large enough that the old all-N constructor would allocate a dense bitvector.
    Sequence large(1u << 20, /* allNs */ true);
    QVERIFY(large.missing());
    QCOMPARE(large.capacity(), 0u);
    QCOMPARE(large, large.GetReverseComplement());
}

void BandageTests::sequenceInitN() {
    Sequence sequenceFromString{"ATGCN"};
    Sequence sequenceFromQByteArray{QByteArray{"ATGCN"}};
    Sequence sequenceRevComp{"NGCAT", true};
    Sequence sequenceFromStringLower{"atgcn"};

    QCOMPARE(sequenceFromQByteArray, sequenceFromString);
    QCOMPARE(sequenceFromString, sequenceRevComp);
    QCOMPARE(sequenceRevComp, sequenceFromStringLower);
    QCOMPARE(sequenceFromStringLower, sequenceFromQByteArray);
}

void BandageTests::sequenceAccess() {
    Sequence sequence{"ATGCN"};

    QCOMPARE(sequence[0], 'A');
    QCOMPARE(sequence[1], 'T');
    QCOMPARE(sequence[2], 'G');
    QCOMPARE(sequence[3], 'C');
    QCOMPARE(sequence[4], 'N');

    Sequence sequenceRC{"ATGCN", true};

    QCOMPARE(sequenceRC[0], 'N');
    QCOMPARE(sequenceRC[1], 'G');
    QCOMPARE(sequenceRC[2], 'C');
    QCOMPARE(sequenceRC[3], 'A');
    QCOMPARE(sequenceRC[4], 'T');
}

void BandageTests::sequenceSubstring() {
    Sequence sequence{"ATGCNATGCN"};
    Sequence substr = sequence.Subseq(2, 7); // GCNAT

    QCOMPARE(substr[0], 'G');
    QCOMPARE(substr[1], 'C');
    QCOMPARE(substr[2], 'N');
    QCOMPARE(substr[3], 'A');
    QCOMPARE(substr[4], 'T');

    Sequence substrRC = sequence.Subseq(2, 7).GetReverseComplement(); // GCNAT -> ATNGC

    QCOMPARE(substrRC[0], 'A');
    QCOMPARE(substrRC[1], 'T');
    QCOMPARE(substrRC[2], 'N');
    QCOMPARE(substrRC[3], 'G');
    QCOMPARE(substrRC[4], 'C');

    Sequence rcSubstr = sequence.GetReverseComplement().Subseq(2, 8); // ATGCNATGCN -> NGCATNGCAT -> CATNG

    QCOMPARE(rcSubstr[0], 'C');
    QCOMPARE(rcSubstr[1], 'A');
    QCOMPARE(rcSubstr[2], 'T');
    QCOMPARE(rcSubstr[3], 'N');
    QCOMPARE(rcSubstr[4], 'G');
}

void BandageTests::sequenceDoubleReverseComplement() {
    Sequence sequence{"ATGCNATGCN"};

    QCOMPARE(sequence, sequence.GetReverseComplement().GetReverseComplement());
}

void BandageTests::sequenceShort() {
    std::string bases59(59, 'A');
    bases59[10] = 'C';
    bases59[58] = 'G';
    Sequence shortSeq{bases59};
    QCOMPARE(shortSeq.size(), 59u);
    QCOMPARE(shortSeq.capacity(), 0u);
    QVERIFY(!shortSeq.missing());
    QCOMPARE(shortSeq[0], 'A');
    QCOMPARE(shortSeq[10], 'C');
    QCOMPARE(shortSeq[58], 'G');

    Sequence copy = shortSeq;
    QCOMPARE(copy, shortSeq);
    QCOMPARE(copy.capacity(), 0u);

    Sequence rc = shortSeq.GetReverseComplement();
    QCOMPARE(rc.capacity(), 0u);
    QCOMPARE(rc.size(), 59u);
    QCOMPARE(rc[0], 'C');
    QCOMPARE(rc[48], 'G');
    QCOMPARE(rc[58], 'T');
    QCOMPARE(shortSeq, rc.GetReverseComplement());

    Sequence head = shortSeq.Subseq(0, 4);
    QCOMPARE(head.size(), 4u);
    QCOMPARE(head.capacity(), 0u);
    QCOMPARE(head[0], 'A');
    QCOMPARE(head[3], 'A');

    Sequence middle = shortSeq.Subseq(10, 12);
    QCOMPARE(middle.size(), 2u);
    QVERIFY(middle.capacity() > 0u);
    QCOMPARE(middle[0], 'C');
    QCOMPARE(middle[1], 'A');
    QCOMPARE(middle, middle.GetReverseComplement().GetReverseComplement());

    std::string bases60(60, 'T');
    bases60[59] = 'G';
    Sequence seq60{bases60};
    QCOMPARE(seq60.capacity(), 0u);
    QCOMPARE(seq60[59], 'G');
    QCOMPARE(seq60.GetReverseComplement()[0], 'C');

    std::string bases61(61, 'T');
    Sequence seq61{bases61};
    QVERIFY(seq61.capacity() > 0u);
    QCOMPARE(seq61[60], 'T');

    std::string withN(20, 'A');
    withN[3] = 'N';
    Sequence hasN{withN};
    QCOMPARE(hasN.size(), 20u);
    QVERIFY(hasN.capacity() > 0u);
    QVERIFY(!hasN.missing());
    QCOMPARE(hasN[3], 'N');
    QCOMPARE(hasN[0], 'A');
    Sequence hasNWindow = hasN.Subseq(2, 5);
    QCOMPARE(hasNWindow.size(), 3u);
    QCOMPARE(hasNWindow[0], 'A');
    QCOMPARE(hasNWindow[1], 'N');
    QCOMPARE(hasNWindow[2], 'A');
}

void BandageTests::oneEdgePerLink()
{
    Sequence seq(std::string(32, 'A'));
    auto *aPos = new DeBruijnNode("a", true, 1.0, seq);
    auto *aNeg = new DeBruijnNode("a", false, 1.0, seq);
    auto *bPos = new DeBruijnNode("b", true, 1.0, seq);
    auto *bNeg = new DeBruijnNode("b", false, 1.0, seq);
    aPos->setReverseComplement(aNeg);
    aNeg->setReverseComplement(aPos);
    bPos->setReverseComplement(bNeg);
    bNeg->setReverseComplement(bPos);
    g_assemblyGraph->m_deBruijnGraphNodes[aPos->getName()] = aPos;
    g_assemblyGraph->m_deBruijnGraphNodes[aNeg->getName()] = aNeg;
    g_assemblyGraph->m_deBruijnGraphNodes[bPos->getName()] = bPos;
    g_assemblyGraph->m_deBruijnGraphNodes[bNeg->getName()] = bNeg;

    g_assemblyGraph->createDeBruijnEdge("a+", "b+", 0, EXACT_OVERLAP);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 1);
    DeBruijnEdge *edge = *g_assemblyGraph->m_deBruijnGraphEdges.begin();
    QVERIFY(edge->leaves(aPos));
    QVERIFY(edge->enters(bPos));
    QVERIFY(edge->leaves(bNeg));
    QVERIFY(edge->enters(aNeg));
    QCOMPARE(edge->other(aPos), bPos);
    QCOMPARE(edge->other(bNeg), aNeg);
    bool sawOutgoing = false;
    for (auto link : aPos->outgoing_edges()) {
        if (link.edge() != edge)
            continue;
        sawOutgoing = true;
        QCOMPARE(link.otherNode(), bPos);
        QCOMPARE(link.overlap(), 0);
        QCOMPARE(link.overlapType(), EXACT_OVERLAP);
    }
    QVERIFY(sawOutgoing);
    bool sawReverse = false;
    for (auto link : bNeg->outgoing_edges()) {
        if (link.edge() != edge)
            continue;
        sawReverse = true;
        QCOMPARE(link.otherNode(), aNeg);
    }
    QVERIFY(sawReverse);
    QVERIFY(aPos->doesNodeLeadAway(bPos));
    QVERIFY(bNeg->doesNodeLeadAway(aNeg));

    g_assemblyGraph->createDeBruijnEdge("b-", "a-", 0, EXACT_OVERLAP);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 1);

    auto sameOverlap = g_assemblyGraph->makeLink(aPos, bPos, 0, EXACT_OVERLAP, true);
    QVERIFY(!sameOverlap.created);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 1);

    auto parallel = g_assemblyGraph->makeLink(aPos, bPos, 10, EXACT_OVERLAP, true);
    QVERIFY(parallel.created);
    QCOMPARE(parallel.edge->getOverlap(), 10);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 2);
    auto parallelReverse = g_assemblyGraph->makeLink(bNeg, aNeg, 10, EXACT_OVERLAP, true);
    QVERIFY(!parallelReverse.created);

    g_assemblyGraph->createDeBruijnEdge("a+", "b+", 99, EXACT_OVERLAP);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 2);

    g_assemblyGraph->createDeBruijnEdge("a+", "a-", 0, EXACT_OVERLAP);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 3);
    DeBruijnEdge *self = nullptr;
    for (DeBruijnEdge *candidate : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (candidate->isOwnReverseComplement())
            self = candidate;
    }
    QVERIFY(self != nullptr);
    QVERIFY(self->leaves(aPos));
    QVERIFY(self->enters(aNeg));
    QVERIFY(!self->leaves(aNeg));
    QCOMPARE(self->other(aPos), aNeg);

    auto *palindrome = new DeBruijnNode("s", true, 1.0, seq);
    palindrome->setReverseComplement(palindrome);
    g_assemblyGraph->m_deBruijnGraphNodes[palindrome->getName()] = palindrome;
    auto palindromeLink = g_assemblyGraph->makeLink(palindrome, bPos, 5, EXACT_OVERLAP, true);
    QVERIFY(palindromeLink.created);
    QVERIFY(palindromeLink.edge->leaves(palindrome));
    QVERIFY(palindromeLink.edge->enters(palindrome));
    auto palOut = palindrome->outgoing_edges();
    QCOMPARE(palOut.begin()->otherNode(), bPos);
    QCOMPARE(palOut.begin()->overlap(), 5);
    auto palIn = palindrome->incoming_edges();
    QCOMPARE(palIn.begin()->otherNode(), bNeg);
    auto palindromeReturn = g_assemblyGraph->makeLink(bNeg, palindrome, 5, EXACT_OVERLAP, true);
    QVERIFY(!palindromeReturn.created);

    std::vector<cigar::tag> ownTags;
    ownTags.emplace_back("CL", "Z", std::string("red"));
    ownTags.emplace_back("C2", "Z", std::string("blue"));
    io::handleStandardGFAEdgeTags(self, false, ownTags, *g_assemblyGraph);
    QCOMPARE(g_assemblyGraph->getCustomColour(self), QColor("red"));
    QVERIFY(!g_assemblyGraph->hasCustomColour(self, true));

    std::vector<cigar::tag> forwardColour;
    forwardColour.emplace_back("CL", "Z", std::string("red"));
    io::handleStandardGFAEdgeTags(edge, false, forwardColour, *g_assemblyGraph);
    std::vector<cigar::tag> reverseColour;
    reverseColour.emplace_back("CL", "Z", std::string("blue"));
    io::handleStandardGFAEdgeTags(edge, true, reverseColour, *g_assemblyGraph, true);
    QCOMPARE(g_assemblyGraph->getCustomColour(edge), QColor("red"));
    QCOMPARE(g_assemblyGraph->getCustomColour(edge, true), QColor("blue"));
    std::vector<cigar::tag> overwrite;
    overwrite.emplace_back("CL", "Z", std::string("green"));
    io::handleStandardGFAEdgeTags(edge, false, overwrite, *g_assemblyGraph, true);
    QCOMPARE(g_assemblyGraph->getCustomColour(edge), QColor("red"));

    const int beforeComplex = static_cast<int>(g_assemblyGraph->m_deBruijnGraphEdges.size());
    auto complexA = g_assemblyGraph->makeLink(aPos, bPos, 0, EXACT_OVERLAP, true, "5M1I4M");
    auto complexB = g_assemblyGraph->makeLink(aPos, bPos, 0, EXACT_OVERLAP, true, "3M2D");
    QVERIFY(complexA.created);
    QVERIFY(complexB.created);
    QVERIFY(complexA.edge != complexB.edge);
    auto complexAgain = g_assemblyGraph->makeLink(bNeg, aNeg, 0, EXACT_OVERLAP, true, "5M1I4M");
    QVERIFY(!complexAgain.created);
    QCOMPARE(complexAgain.edge, complexA.edge);
    QCOMPARE(static_cast<int>(g_assemblyGraph->m_deBruijnGraphEdges.size()), beforeComplex + 2);

    QVERIFY(!DeBruijnEdge::isPositiveOrientation(palindrome, bNeg));
    auto palToNeg = g_assemblyGraph->makeLink(palindrome, bNeg, 1, EXACT_OVERLAP, true);
    QVERIFY(palToNeg.created);
    QCOMPARE(palToNeg.edge->getStartingNode(), bPos);
    QCOMPARE(palToNeg.edge->getEndingNode(), palindrome);

    g_assemblyGraph->createDeBruijnEdge("a+", "b-", 3, EXACT_OVERLAP);
    DeBruijnEdge *renamed = nullptr;
    for (DeBruijnEdge *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (edge->getOverlap() == 3)
            renamed = edge;
    }
    QVERIFY(renamed != nullptr);
    g_assemblyGraph->changeNodeName("b", "0");
    auto reused = g_assemblyGraph->makeLink(renamed->getStartingNode(), renamed->getEndingNode(),
                                            3, EXACT_OVERLAP, true);
    QVERIFY(!reused.created);
    QCOMPARE(reused.edge, renamed);
    g_assemblyGraph->determineGraphInfo();
    QCOMPARE(g_assemblyGraph->m_edgeCount,
             static_cast<int>(g_assemblyGraph->m_deBruijnGraphEdges.size()));
}















DeBruijnEdge * BandageTests::getEdgeFromNodeNames(QString startingNodeName,
                                                  QString endingNodeName) const
{
    DeBruijnNode * startingNode = g_assemblyGraph->m_deBruijnGraphNodes[startingNodeName.toStdString()];
    DeBruijnNode * endingNode = g_assemblyGraph->m_deBruijnGraphNodes[endingNodeName.toStdString()];

    for (DeBruijnEdge *edge : startingNode->edges()) {
        if (edge->connects(startingNode, endingNode))
            return edge;
    }

    return nullptr;
}


//http://www.code10.info/index.php?option=com_content&view=article&id=62:articledna-reverse-complement&catid=49:cat_coding_algorithms_bioinformatics&Itemid=74
static QByteArray getReverseComplement(const QByteArray& forwardSequence) {
    QByteArray reverseComplement;
    reverseComplement.reserve(forwardSequence.length());

    for (int i = forwardSequence.length() - 1; i >= 0; --i) {
        switch (forwardSequence.at(i)) {
            case 'A': reverseComplement.append('T'); break;
            case 'T': reverseComplement.append('A'); break;
            case 'G': reverseComplement.append('C'); break;
            case 'C': reverseComplement.append('G'); break;
            case 'a': reverseComplement.append('t'); break;
            case 't': reverseComplement.append('a'); break;
            case 'g': reverseComplement.append('c'); break;
            case 'c': reverseComplement.append('g'); break;
            case 'R': reverseComplement.append('Y'); break;
            case 'Y': reverseComplement.append('R'); break;
            case 'S': reverseComplement.append('S'); break;
            case 'W': reverseComplement.append('W'); break;
            case 'K': reverseComplement.append('M'); break;
            case 'M': reverseComplement.append('K'); break;
            case 'r': reverseComplement.append('y'); break;
            case 'y': reverseComplement.append('r'); break;
            case 's': reverseComplement.append('s'); break;
            case 'w': reverseComplement.append('w'); break;
            case 'k': reverseComplement.append('m'); break;
            case 'm': reverseComplement.append('k'); break;
            case 'B': reverseComplement.append('V'); break;
            case 'D': reverseComplement.append('H'); break;
            case 'H': reverseComplement.append('D'); break;
            case 'V': reverseComplement.append('B'); break;
            case 'b': reverseComplement.append('v'); break;
            case 'd': reverseComplement.append('h'); break;
            case 'h': reverseComplement.append('d'); break;
            case 'v': reverseComplement.append('b'); break;
            case 'N': reverseComplement.append('N'); break;
            case 'n': reverseComplement.append('n'); break;
            case '.': reverseComplement.append('.'); break;
            case '-': reverseComplement.append('-'); break;
            case '?': reverseComplement.append('?'); break;
            default:  reverseComplement.append('*'); break;
        }
    }

    return reverseComplement;
}

//This function checks to see if two circular sequences match.  It needs to
//check each possible rotation, as well as reverse complements.
bool BandageTests::doCircularSequencesMatch(QByteArray s1, QByteArray s2) const
{
    for (int i = 0; i < s1.length() - 1; ++i)
    {
        QByteArray rotatedS1 = s1.right(s1.length() - i) + s1.left(i);
        if (rotatedS1 == s2)
            return true;
    }

    //If the code got here, then all possible rotations of s1 failed to match
    //s2.  Now we try the reverse complement.
    QByteArray s1Rc = getReverseComplement(s1);
    for (int i = 0; i < s1Rc.length() - 1; ++i)
    {
        QByteArray rotatedS1Rc = s1Rc.right(s1Rc.length() - i) + s1Rc.left(i);
        if (rotatedS1Rc == s2)
            return true;
    }

    return false;
}

void BandageTests::annotationGroupNodesWithAnnotations()
{
    requireGraphLoad(testFile("test.gfa"));

    DeBruijnNode *node1Plus = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    DeBruijnNode *node1Minus = node1Plus->getReverseComplement();
    QVERIFY(node1Plus != nullptr);
    QVERIFY(node1Minus != nullptr);
    QVERIFY(node1Plus->isPositiveNode());

    auto &group = g_annotationsManager->createAnnotationGroup("Minimap2 hits");
    QCOMPARE(group.getNodesWithAnnotations(true).size(), size_t(0));
    QCOMPARE(group.getNodesWithAnnotations(false).size(), size_t(0));

    // Annotate only the reverse-complement strand under a non-BLAST group name.
    group.annotationMap[node1Minus].emplace_back(
            std::make_unique<Annotation>(1, 10, "query"));

    auto doubleModeNodes = group.getNodesWithAnnotations(/*doubleMode=*/true);
    QCOMPARE(doubleModeNodes.size(), size_t(1));
    QCOMPARE(doubleModeNodes.front(), node1Minus);

    auto singleModeNodes = group.getNodesWithAnnotations(/*doubleMode=*/false);
    QCOMPARE(singleModeNodes.size(), size_t(1));
    QCOMPARE(singleModeNodes.front(), node1Plus); // canonical representative

    // Annotating the positive strand as well still yields one pair in single mode
    // and both strands in double mode.
    group.annotationMap[node1Plus].emplace_back(
            std::make_unique<Annotation>(1, 5, "query"));

    doubleModeNodes = group.getNodesWithAnnotations(/*doubleMode=*/true);
    QCOMPARE(doubleModeNodes.size(), size_t(2));
    QVERIFY(std::find(doubleModeNodes.begin(), doubleModeNodes.end(), node1Plus) != doubleModeNodes.end());
    QVERIFY(std::find(doubleModeNodes.begin(), doubleModeNodes.end(), node1Minus) != doubleModeNodes.end());

    singleModeNodes = group.getNodesWithAnnotations(/*doubleMode=*/false);
    QCOMPARE(singleModeNodes.size(), size_t(1));
    QCOMPARE(singleModeNodes.front(), node1Plus);
}

static void countSceneItems(BandageGraphicsScene &scene, int &nodes, int &edges) {
    nodes = 0;
    edges = 0;
    for (auto *item : scene.items()) {
        if (dynamic_cast<GraphicsItemNode *>(item) != nullptr)
            ++nodes;
        if (dynamic_cast<GraphicsItemEdge *>(item) != nullptr)
            ++edges;
    }
}

static BandageGraphicsScene *drawCorners(bool doubleMode, std::string *error) {
    g_settings->doubleMode = doubleMode;
    auto scope = graph::Scope::wholeGraph();
    auto startingNodes = graph::getStartingNodes(*g_assemblyGraph, scope);
    if (!startingNodes) {
        *error = bandage::toString(startingNodes.takeError());
        return nullptr;
    }
    g_assemblyGraph->resetNodes();
    g_assemblyGraph->markNodesToDraw(scope, *startingNodes);
    auto layout = GraphLayoutWorker(g_settings->graphLayoutQuality,
                                    g_settings->linearLayout,
                                    g_settings->componentSeparation)
                          .layoutGraph(*g_assemblyGraph);
    auto *scene = new BandageGraphicsScene();
    scene->addGraphicsItemsToScene(*g_assemblyGraph, layout);
    return scene;
}

// The link is stored as a+ → b+. A path along b- → a- uses that same edge,
// so the hit overlap has to come from connects(), not the stored end.
void BandageTests::queryPathOverlapReverseStoredEdge() {
    Sequence seq(std::string(20, 'A'));
    auto addPair = [&](const char *name) {
        auto *pos = new DeBruijnNode(name, true, 1.0, seq);
        auto *neg = new DeBruijnNode(name, false, 1.0, seq);
        pos->setReverseComplement(neg);
        neg->setReverseComplement(pos);
        g_assemblyGraph->m_deBruijnGraphNodes[pos->getName()] = pos;
        g_assemblyGraph->m_deBruijnGraphNodes[neg->getName()] = neg;
        return std::pair{pos, neg};
    };
    auto [aPos, aNeg] = addPair("a");
    auto [bPos, bNeg] = addPair("b");
    auto *cPos = addPair("c").first;
    g_assemblyGraph->createDeBruijnEdge("a+", "b+", 10, EXACT_OVERLAP);

    auto *query = new search::Query("q", QString(20, 'A'));
    const SciNot eValue(2.0, -4);
    auto hit = [&](DeBruijnNode *node, int nodeStart, int nodeEnd,
                   int queryStart, int queryEnd) {
        return query->emplaceHit(query, node, 100.0, 10, 0, 0,
                                 queryStart, queryEnd, nodeStart, nodeEnd,
                                 eValue, 20.0);
    };
    const auto *forwardA = hit(aPos, 11, 20, 1, 10);
    const auto *forwardB = hit(bPos, 1, 10, 11, 20);
    const auto *reverseB = hit(bNeg, 11, 20, 1, 10);
    const auto *reverseA = hit(aNeg, 1, 10, 11, 20);
    const auto *loose = hit(cPos, 1, 10, 11, 20);

    search::QueryPath forward(Path(), query, {forwardA, forwardB});
    search::QueryPath reverse(Path(), query, {reverseB, reverseA});
    search::QueryPath disconnected(Path(), query, {forwardA, loose});
    QCOMPARE(forward.getEvalueProduct(), reverse.getEvalueProduct());
    QVERIFY(forward.getEvalueProduct() != disconnected.getEvalueProduct());
    delete query;
}

// test_draw_corners.gfa. a and b are ordinary segments. s is ATAT, so it is
// its own reverse complement. The links are a+ → b+ together with b- → a-,
// the hairpin a+ → a-, the self-loop a+ → a+, s → b+, and the loop s → s.
void BandageTests::drawEdgeDirections()
{
    requireGraphLoad(testFile("test_draw_corners.gfa"));
    QCOMPARE(g_assemblyGraph->m_nodeCount, 4);
    QCOMPARE(g_assemblyGraph->m_edgeCount, 5);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 5u);

    std::string cornerError;
    QScopedPointer<BandageGraphicsScene> single(drawCorners(false, &cornerError));
    if (!cornerError.empty())
        QFAIL(cornerError.c_str());
    int nodes = 0;
    int edges = 0;
    countSceneItems(*single, nodes, edges);
    QCOMPARE(nodes, 3);
    QCOMPARE(edges, 5);
    for (auto *item : single->items()) {
        auto *edgeItem = dynamic_cast<GraphicsItemEdge *>(item);
        if (edgeItem == nullptr)
            continue;
        QVERIFY(g_assemblyGraph->reverseGraphicsItem(edgeItem->edge()) == nullptr);
        const bool palindromicLoop = edgeItem->edge()->getStartingNode()->getName() == "s+" &&
                                     edgeItem->edge()->getEndingNode()->getName() == "s+";
        // A 4-base palindromic self-loop has a zero-length end segment, so Qt
        // drops the cubic and the path is only a move.
        if (palindromicLoop)
            QCOMPARE(edgeItem->path().elementCount(), 1);
        else
            QVERIFY(!edgeItem->path().isEmpty());
    }

    cornerError.clear();
    QScopedPointer<BandageGraphicsScene> doubled(drawCorners(true, &cornerError));
    if (!cornerError.empty())
        QFAIL(cornerError.c_str());
    countSceneItems(*doubled, nodes, edges);
    QCOMPARE(nodes, 5);
    QCOMPARE(edges, 8);

    DeBruijnNode *aPos = g_assemblyGraph->m_deBruijnGraphNodes["a+"];
    DeBruijnNode *aNeg = g_assemblyGraph->m_deBruijnGraphNodes["a-"];
    DeBruijnNode *bPos = g_assemblyGraph->m_deBruijnGraphNodes["b+"];
    DeBruijnNode *bNeg = g_assemblyGraph->m_deBruijnGraphNodes["b-"];
    DeBruijnNode *s = g_assemblyGraph->m_deBruijnGraphNodes["s+"];
    int ownReverseComplement = 0;
    DeBruijnEdge *ordinary = nullptr;
    DeBruijnEdge *hairpin = nullptr;
    DeBruijnEdge *selfLoop = nullptr;
    DeBruijnEdge *palindromeLink = nullptr;
    DeBruijnEdge *palindromeLoop = nullptr;
    for (DeBruijnEdge *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        QVERIFY(edge->getGraphicsItemEdge() != nullptr);
        if (edge->isOwnReverseComplement()) {
            ++ownReverseComplement;
            QVERIFY(g_assemblyGraph->reverseGraphicsItem(edge) == nullptr);
            QVERIFY(!edge->isDrawnReverse());
            if (edge->getStartingNode() == aPos)
                hairpin = edge;
            else
                palindromeLoop = edge;
            continue;
        }
        QVERIFY(g_assemblyGraph->reverseGraphicsItem(edge) != nullptr);
        QVERIFY(!g_assemblyGraph->reverseGraphicsItem(edge)->path().isEmpty());
        if (edge->connects(aPos, bPos) && edge->getStartingNode() == aPos &&
            edge->getEndingNode() == bPos)
            ordinary = edge;
        if (edge->getStartingNode() == aPos && edge->getEndingNode() == aPos)
            selfLoop = edge;
        if (edge->connects(s, bPos))
            palindromeLink = edge;
    }
    QCOMPARE(ownReverseComplement, 2);
    QVERIFY(ordinary != nullptr);
    QVERIFY(hairpin != nullptr);
    QVERIFY(selfLoop != nullptr);
    QVERIFY(palindromeLink != nullptr);
    QVERIFY(palindromeLoop != nullptr);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), 5u);

    GraphicsItemNode *aItem = aPos->getGraphicsItemNode();
    QVERIFY(aItem != nullptr);
    aItem->shiftPoints(QPointF(25, 15));
    aItem->fixEdgePaths();
    QVERIFY(!selfLoop->getGraphicsItemEdge()->path().isEmpty());
    QVERIFY(!g_assemblyGraph->reverseGraphicsItem(selfLoop)->path().isEmpty());
    QCOMPARE(selfLoop->getGraphicsItemEdge()->edge(), selfLoop);
    QCOMPARE(g_assemblyGraph->reverseGraphicsItem(selfLoop)->edge(), selfLoop);

    GraphicsItemEdge *reverseItem = g_assemblyGraph->reverseGraphicsItem(ordinary);
    reverseItem->setSelected(true);
    std::vector<DeBruijnEdge *> selected = doubled->getSelectedEdges();
    QCOMPARE(selected.size(), 1u);
    QCOMPARE(selected.front(), ordinary);
    QVERIFY(reverseItem->reversed());
    auto [reverseFrom, reverseTo] = ordinary->orientedEnds(true);
    QCOMPARE(reverseFrom, bNeg);
    QCOMPARE(reverseTo, aNeg);

    std::vector<DeBruijnNode *> hideA{aPos};
    BandageGraphicsScene::removeGraphicsItemNodes(hideA, false);
    QVERIFY(ordinary->getGraphicsItemEdge() == nullptr);
    QVERIFY(g_assemblyGraph->reverseGraphicsItem(ordinary) != nullptr);
    QVERIFY(selfLoop->getGraphicsItemEdge() == nullptr);
    QVERIFY(g_assemblyGraph->reverseGraphicsItem(selfLoop) != nullptr);
    QVERIFY(hairpin->getGraphicsItemEdge() == nullptr);
    QVERIFY(palindromeLink->getGraphicsItemEdge() != nullptr);
    QVERIFY(g_assemblyGraph->reverseGraphicsItem(palindromeLink) != nullptr);
    countSceneItems(*doubled, nodes, edges);
    QCOMPARE(edges, 5);

    std::vector<DeBruijnNode *> hideS{s};
    BandageGraphicsScene::removeGraphicsItemNodes(hideS, false);
    QVERIFY(palindromeLink->getGraphicsItemEdge() == nullptr);
    QVERIFY(g_assemblyGraph->reverseGraphicsItem(palindromeLink) == nullptr);
    QVERIFY(palindromeLoop->getGraphicsItemEdge() == nullptr);

    cornerError.clear();
    doubled.reset(drawCorners(true, &cornerError));
    if (!cornerError.empty())
        QFAIL(cornerError.c_str());
    BandageGraphicsScene::removeGraphicsItemNodes(hideA, true);
    QVERIFY(ordinary->getGraphicsItemEdge() == nullptr);
    QVERIFY(g_assemblyGraph->reverseGraphicsItem(ordinary) == nullptr);
    QVERIFY(selfLoop->getGraphicsItemEdge() == nullptr);
    QVERIFY(g_assemblyGraph->reverseGraphicsItem(selfLoop) == nullptr);
    QVERIFY(hairpin->getGraphicsItemEdge() == nullptr);

    doubled->addGraphicsItemsToScene(*g_assemblyGraph,
                                    GraphLayoutWorker(g_settings->graphLayoutQuality,
                                                      g_settings->linearLayout,
                                                      g_settings->componentSeparation)
                                            .layoutGraph(*g_assemblyGraph));
    countSceneItems(*doubled, nodes, edges);
    QCOMPARE(nodes, 5);
    QCOMPARE(edges, 8);
    for (DeBruijnEdge *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        QCOMPARE(edge->getGraphicsItemEdge()->scene(), doubled.data());
        if (edge->isOwnReverseComplement())
            QVERIFY(g_assemblyGraph->reverseGraphicsItem(edge) == nullptr);
        else
            QCOMPARE(g_assemblyGraph->reverseGraphicsItem(edge)->scene(), doubled.data());
    }
}

void BandageTests::duplicateNodeWithSelfLoop()
{
    Sequence seq(std::string(8, 'A'));
    auto addPair = [&](const char *name) {
        auto *pos = new DeBruijnNode(name, true, 1.0, seq);
        auto *neg = new DeBruijnNode(name, false, 1.0, seq);
        pos->setReverseComplement(neg);
        neg->setReverseComplement(pos);
        g_assemblyGraph->m_deBruijnGraphNodes[pos->getName()] = pos;
        g_assemblyGraph->m_deBruijnGraphNodes[neg->getName()] = neg;
        return pos;
    };
    auto *original = addPair("a");
    addPair("b");
    addPair("c");
    addPair("d");
    g_assemblyGraph->createDeBruijnEdge("a+", "a+", 1, EXACT_OVERLAP);
    g_assemblyGraph->createDeBruijnEdge("a+", "b+", 1, EXACT_OVERLAP);
    g_assemblyGraph->createDeBruijnEdge("a+", "c+", 1, EXACT_OVERLAP);
    g_assemblyGraph->createDeBruijnEdge("a+", "d+", 1, EXACT_OVERLAP);
    g_assemblyGraph->duplicateNodePair(original, nullptr);

    auto *copy = g_assemblyGraph->m_deBruijnGraphNodes["a_copy+"];
    QVERIFY(copy != nullptr);
    bool sawOriginal = false;
    bool sawB = false;
    bool sawC = false;
    bool sawD = false;
    for (auto link : copy->outgoing_edges()) {
        if (link.otherNode() == original)
            sawOriginal = true;
        else if (link.otherNode()->getName() == "b+")
            sawB = true;
        else if (link.otherNode()->getName() == "c+")
            sawC = true;
        else if (link.otherNode()->getName() == "d+")
            sawD = true;
    }
    QVERIFY(sawOriginal);
    QVERIFY(sawB);
    QVERIFY(sawC);
    QVERIFY(sawD);
}

void BandageTests::loadDistinctGfaOverlaps()
{
    const QString gfaPath = tempFile("distinct-overlaps.gfa");
    {
        QFile gfa(gfaPath);
        QVERIFY(gfa.open(QIODevice::WriteOnly));
        gfa.write("S\ta\tAAAA\n"
                  "S\tb\tCCCC\n"
                  "L\ta\t+\tb\t+\t*\n"
                  "L\ta\t+\tb\t+\t0M\n"
                  "L\ta\t+\tb\t+\t5M1I4M\n"
                  "J\ta\t+\tb\t+\t*\n"
                  "J\tb\t-\ta\t-\t*\tCL:Z:blue\n"
                  "J\ta\t+\tb\t+\t0\n");
    }
    requireGraphLoad(gfaPath);
    auto *aPos = g_assemblyGraph->m_deBruijnGraphNodes["a+"];
    auto *bPos = g_assemblyGraph->m_deBruijnGraphNodes["b+"];
    int starLinks = 0;
    int zeroLinks = 0;
    int cigarLinks = 0;
    int starJumps = 0;
    int zeroJumps = 0;
    DeBruijnEdge *starJump = nullptr;
    for (auto *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (!edge->connects(aPos, bPos))
            continue;
        auto keyIt = g_assemblyGraph->m_overlapKeys.find(edge);
        const bool star = keyIt != g_assemblyGraph->m_overlapKeys.end() && keyIt->second == "*";
        const bool cigar = keyIt != g_assemblyGraph->m_overlapKeys.end() && keyIt->second == "5M1I4M";
        if (edge->getOverlapType() == JUMP) {
            if (star) {
                ++starJumps;
                starJump = edge;
            } else if (edge->getOverlap() == 0 && keyIt == g_assemblyGraph->m_overlapKeys.end()) {
                ++zeroJumps;
            }
        } else if (edge->getOverlapType() == EXACT_OVERLAP) {
            if (star)
                ++starLinks;
            else if (cigar)
                ++cigarLinks;
            else if (edge->getOverlap() == 0 && keyIt == g_assemblyGraph->m_overlapKeys.end())
                ++zeroLinks;
        }
    }
    QCOMPARE(starLinks, 1);
    QCOMPARE(zeroLinks, 1);
    QCOMPARE(cigarLinks, 1);
    QCOMPARE(starJumps, 1);
    QCOMPARE(zeroJumps, 1);
    QVERIFY(starJump != nullptr);
    QCOMPARE(g_assemblyGraph->getCustomColour(starJump), QColor("red"));
    QCOMPARE(g_assemblyGraph->getCustomColour(starJump, true), QColor("blue"));

    const QString savedPath = tempFile("distinct-overlaps-out.gfa");
    QVERIFY(gfa::saveEntireGraph(savedPath, *g_assemblyGraph));
    QFile saved(savedPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray written = saved.readAll();
    QVERIFY(written.contains("L\ta\t+\tb\t+\t*"));
    QVERIFY(written.contains("L\ta\t+\tb\t+\t5M1I4M"));
    QVERIFY(written.contains("J\ta\t+\tb\t+\t*"));
    QVERIFY(written.contains("J\ta\t+\tb\t+\t0"));
}

void BandageTests::loadLoganPalindromicSegment()
{
    const QString loganPath = tempFile("palindrome.contigs.fa.gz");
    {
        QFile logan(loganPath);
        QVERIFY(logan.open(QIODevice::WriteOnly));
        logan.write(">contig_a ka:f:1 L:+:s:+ L:+:s:-\n"
                    "AAAA\n"
                    ">contig_s ka:f:1\n"
                    "ATAT\n");
    }
    requireGraphLoad(loganPath);
    QCOMPARE(static_cast<int>(g_assemblyGraph->m_deBruijnGraphEdges.size()), 1);
}

static bool writeTextFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

static int drawScope(const graph::Scope &scope, QString &errorTitle, QString &errorMessage) {
    errorTitle.clear();
    errorMessage.clear();
    auto startingNodes = graph::getStartingNodes(*g_assemblyGraph, scope);
    g_assemblyGraph->resetNodes();
    if (!startingNodes) {
        bandage::handleAllErrors(startingNodes.takeError(), [&](const bandage::TextError &err) {
            errorTitle = QString::fromStdString(err.title());
            errorMessage = QString::fromStdString(err.text());
        });
    } else {
        g_assemblyGraph->markNodesToDraw(scope, *startingNodes);
    }
    return g_assemblyGraph->getDrawnNodeCount();
}

static bool nodeIsDrawn(const char *name) {
    auto it = g_assemblyGraph->m_deBruijnGraphNodes.find(name);
    return it != g_assemblyGraph->m_deBruijnGraphNodes.end() && (*it)->isDrawn();
}

static bool hasTag(const std::vector<gfa::tag> &tags, const char *name, const char *value) {
    auto found = gfa::getTag<std::string>(name, tags);
    return found && *found == value;
}

static std::unique_ptr<GraphicsItemNode> makeItem(DeBruijnNode *node) {
    return std::make_unique<GraphicsItemNode>(
            node, 1.0, std::vector<QPointF>{QPointF(0, 0), QPointF(10, 0)});
}

void BandageTests::loadWalks() {
    requireGraphLoad(testFile("test_components.gfa"));
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphWalks.size(), 2u);

    auto walkA = g_assemblyGraph->m_deBruijnGraphWalks.find("WALK_A");
    QVERIFY(walkA != g_assemblyGraph->m_deBruijnGraphWalks.end());
    QCOMPARE(walkA->sampleId, std::string("sampleA"));
    QCOMPARE(walkA->hapIndex, 0u);
    QCOMPARE(walkA->seqStart, 0u);
    QCOMPARE(walkA->seqEnd, walkA->walk.getLength());
    QCOMPARE(walkA->walk.nodes().size(), 2u);
    QCOMPARE(walkA->walk.nodes()[0]->getName(), std::string("a1+"));
    QCOMPARE(walkA->walk.nodes()[1]->getName(), std::string("a2+"));

    const QString roundTrip = tempFile("components-roundtrip.gfa");
    QVERIFY(gfa::saveEntireGraph(roundTrip, *g_assemblyGraph));
    QFile roundFile(roundTrip);
    QVERIFY(roundFile.open(QIODevice::ReadOnly));
    const QByteArray roundText = roundFile.readAll();
    QVERIFY(roundText.contains("P\tPATH_A\ta1+,a2+\t*"));
    QVERIFY(roundText.contains("W\tsampleA\t0\tWALK_A\t"));
    QVERIFY(roundText.contains(">a1>a2"));
    requireGraphLoad(roundTrip);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphPaths.size(), 2u);
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphWalks.size(), 2u);
    QVERIFY(g_assemblyGraph->m_deBruijnGraphWalks.find("WALK_A") !=
            g_assemblyGraph->m_deBruijnGraphWalks.end());

    const QString badOrientation = tempFile("walk-bad-orientation.gfa");
    QVERIFY(writeTextFile(badOrientation,
                          "S\ta\tACGT\n"
                          "W\ts\t0\twalk\t*\t*\txa\n"));
    auto badOrientationResult = g_assemblyGraph->loadGraphFromFile(badOrientation);
    QVERIFY(!badOrientationResult);
    QVERIFY(QString::fromStdString(bandage::toString(badOrientationResult.takeError()))
                    .contains("invalid walk string"));
    QVERIFY(g_assemblyGraph->m_deBruijnGraphNodes.empty());
    QCOMPARE(g_assemblyGraph->m_nodeCount, 0);
    QCOMPARE(g_assemblyGraph->m_edgeCount, 0);
    QVERIFY(g_assemblyGraph->m_filename.isEmpty());
    QCOMPARE(g_assemblyGraph->m_sequencesLoadedFromFasta, NOT_READY);

    const QString missingNode = tempFile("walk-missing-node.gfa");
    QVERIFY(writeTextFile(missingNode,
                          "S\ta\tACGT\n"
                          "W\ts\t0\twalk\t*\t*\t>missing\n"));
    auto missingNodeResult = g_assemblyGraph->loadGraphFromFile(missingNode);
    QVERIFY(!missingNodeResult);
    QVERIFY(QString::fromStdString(bandage::toString(missingNodeResult.takeError()))
                    .contains("invalid node"));
    QVERIFY(g_assemblyGraph->m_deBruijnGraphNodes.empty());
    QCOMPARE(g_assemblyGraph->m_nodeCount, 0);
    QCOMPARE(g_assemblyGraph->m_edgeCount, 0);
    QVERIFY(g_assemblyGraph->m_filename.isEmpty());
    QCOMPARE(g_assemblyGraph->m_sequencesLoadedFromFasta, NOT_READY);
}

void BandageTests::graphScopePathsWalksAndErrors() {
    requireGraphLoad(testFile("test_components.gfa"));
    QString errorTitle;
    QString errorMessage;

    g_settings->doubleMode = false;
    QCOMPARE(drawScope(graph::Scope::aroundPath("PATH_A"), errorTitle, errorMessage), 2);
    QVERIFY(errorMessage.isEmpty());
    QVERIFY(nodeIsDrawn("a1+"));
    QVERIFY(nodeIsDrawn("a2+"));
    QVERIFY(!nodeIsDrawn("a3+"));

    QCOMPARE(drawScope(graph::Scope::aroundPath("PATH_A", 1), errorTitle, errorMessage), 3);
    QVERIFY(nodeIsDrawn("a3+"));
    QVERIFY(!nodeIsDrawn("b1+"));
    QVERIFY(!nodeIsDrawn("c1+"));

    QCOMPARE(drawScope(graph::Scope::aroundPath("NO_SUCH_PATH"), errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("Invalid path"));
    QCOMPARE(errorMessage, QString("No path with such name is loaded"));

    QCOMPARE(drawScope(graph::Scope::aroundWalk("WALK_A"), errorTitle, errorMessage), 2);
    QVERIFY(nodeIsDrawn("a1+"));
    QVERIFY(nodeIsDrawn("a2+"));
    QVERIFY(!nodeIsDrawn("a3+"));

    QCOMPARE(drawScope(graph::Scope::aroundWalk("WALK_A", 1), errorTitle, errorMessage), 3);
    QVERIFY(nodeIsDrawn("a3+"));
    QVERIFY(!nodeIsDrawn("b1+"));
    QVERIFY(!nodeIsDrawn("c1+"));

    QCOMPARE(drawScope(graph::Scope::aroundWalk("NO_SUCH_WALK"), errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("Invalid walk"));
    QCOMPARE(errorMessage, QString("No walk with such sequence name is loaded"));

    QCOMPARE(drawScope(graph::Scope::aroundNodes(""), errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("No starting nodes"));
    QVERIFY(errorMessage.contains("at least one node"));

    QCOMPARE(drawScope(graph::Scope::aroundNodes("not_a_node"), errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("Nodes not found"));

    requireGraphLoad(testFile("test.fastg"));
    QCOMPARE(drawScope(graph::Scope::depthRange(10.0, 1.0), errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("Invalid depth range"));
    QCOMPARE(drawScope(graph::Scope::depthRange(1000000.0, 1000001.0), errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("No nodes in range"));

    QCOMPARE(drawScope(graph::Scope::aroundHits(g_graphSearch->queries(), "all"),
                       errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("No hits"));

    auto errorString = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                        testFile("test_queries1.fasta"));
    QCOMPARE(errorString, "");
    g_settings->doubleMode = false;
    const int allHits = drawScope(graph::Scope::aroundHits(g_graphSearch->queries(), "all"),
                                  errorTitle, errorMessage);
    QVERIFY(errorMessage.isEmpty());
    QVERIFY(allHits > 0);
    const int oneQuery = drawScope(graph::Scope::aroundHits(g_graphSearch->queries(),
                                                           "test_query_exact"),
                                   errorTitle, errorMessage);
    QVERIFY(errorMessage.isEmpty());
    QVERIFY(oneQuery > 0);
    QVERIFY(oneQuery <= allHits);

    search::Query *exact = g_graphSearch->getQueryFromName("test_query_exact");
    QVERIFY(exact != nullptr);
    QVERIFY(!exact->getHits().empty());
    DeBruijnNode *hitNode = exact->getHits().front()->m_node;
    g_settings->doubleMode = true;
    drawScope(graph::Scope::aroundHits(g_graphSearch->queries(), "test_query_exact"),
              errorTitle, errorMessage);
    QVERIFY(errorMessage.isEmpty());
    QVERIFY(hitNode->isDrawn());
}

void BandageTests::layoutRoundTrip() {
    requireGraphLoad(testFile("test.gfa"));
    g_settings->doubleMode = false;
    auto scope = graph::Scope::wholeGraph();
    REQUIRE_OK(startingNodes, graph::getStartingNodes(*g_assemblyGraph, scope));
    g_assemblyGraph->resetNodes();
    g_assemblyGraph->markNodesToDraw(scope, startingNodes);
    QCOMPARE(g_assemblyGraph->getDrawnNodeCount(), 17);

    auto layout = GraphLayoutWorker(g_settings->graphLayoutQuality,
                                    g_settings->linearLayout,
                                    g_settings->componentSeparation)
                          .layoutGraph(*g_assemblyGraph);
    const QString layoutPath = tempFile("roundtrip.layout");
    QVERIFY(layout::io::save(layoutPath, layout));

    GraphLayout loaded(*g_assemblyGraph);
    QVERIFY(layout::io::load(layoutPath, loaded));
    QCOMPARE(loaded.size(), layout.size());
    for (const auto &entry : layout) {
        QVERIFY(loaded.contains(entry.first));
        QCOMPARE(loaded.segments(entry.first).size(), entry.second.size());
    }

    const QString missing = tempFile("missing-node.layout");
    QVERIFY(writeTextFile(missing, "{\"not-a-node\":[[0,0],[1,1]]}"));
    GraphLayout rejected(*g_assemblyGraph);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, layout::io::load(missing, rejected));
}

void BandageTests::loadBedAnnotations() {
    requireGraphLoad(testFile("test.gfa"));
    const QString bedPath = tempFile("features.bed");
    QVERIFY(writeTextFile(bedPath,
                          "1\t0\t10\tshort\t0\t+\t2\t8\t255,0,0\t2\t3,4\t0,5\n"
                          "2\t0\t4\tplain\t0\t+\t0\t0\n"
                          "3\t5\t5\tempty\t0\t+\n"
                          "missing\t0\t4\tgone\t0\t+\n"));
    const auto lines = bed::load(bedPath.toStdString());
    QCOMPARE(lines.size(), 4u);

    auto &group = g_annotationsManager->createAnnotationGroup(
            g_settings->bedAnnotationGroupName);
    for (const auto &bedLine : lines) {
        auto nodeName = g_assemblyGraph->getNodeNameFromString(bedLine.chrom.c_str());
        auto it = g_assemblyGraph->m_deBruijnGraphNodes.find(nodeName.toStdString());
        if (it == g_assemblyGraph->m_deBruijnGraphNodes.end())
            continue;
        DeBruijnNode *node = it.value();
        if (bedLine.strand == bed::Strand::REVERSE_COMPLEMENT)
            node = node->getReverseComplement();
        addBedAnnotation(group, node, bedLine);
    }

    auto node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    const auto &annotations = group.getAnnotations(node1);
    QCOMPARE(annotations.size(), 1u);
    QCOMPARE(annotations.front()->start(), 1);
    QCOMPARE(annotations.front()->end(), 10);
    QCOMPARE(annotations.front()->text(), std::string("short"));
    QCOMPARE(annotations.front()->getViews().size(), 3u);
    QCOMPARE(annotations.front()->getViews()[0]->getTypeName(), QString("Solid"));
    QCOMPARE(annotations.front()->getViews()[1]->getTypeName(), QString("BED Thick"));
    QCOMPARE(annotations.front()->getViews()[2]->getTypeName(), QString("BED Blocks"));
    auto *thick = dynamic_cast<BedThickView *>(annotations.front()->getViews()[1].get());
    auto *blocks = dynamic_cast<BedBlockView *>(annotations.front()->getViews()[2].get());
    QVERIFY(thick != nullptr);
    QVERIFY(blocks != nullptr);
    QCOMPARE(thick->thickStart(), 3);
    QCOMPARE(thick->thickEnd(), 8);
    QCOMPARE(blocks->blocks().size(), 2u);
    QCOMPARE(blocks->blocks()[0].thickStart(), 1);
    QCOMPARE(blocks->blocks()[0].thickEnd(), 3);
    QCOMPARE(blocks->blocks()[1].thickStart(), 6);
    QCOMPARE(blocks->blocks()[1].thickEnd(), 9);

    auto node2 = g_assemblyGraph->m_deBruijnGraphNodes["2+"];
    const auto &plain = group.getAnnotations(node2);
    QCOMPARE(plain.size(), 1u);
    QCOMPARE(plain.front()->start(), 1);
    QCOMPARE(plain.front()->end(), 4);
    QCOMPARE(plain.front()->getViews().size(), annotations.front()->getViews().size());
    for (size_t i = 0; i < annotations.front()->getViews().size(); ++i)
        QCOMPARE(plain.front()->getViews()[i]->getTypeName(),
                 annotations.front()->getViews()[i]->getTypeName());
    auto *plainThick = dynamic_cast<BedThickView *>(plain.front()->getViews()[1].get());
    auto *plainBlocks = dynamic_cast<BedBlockView *>(plain.front()->getViews()[2].get());
    QVERIFY(plainThick != nullptr);
    QVERIFY(plainBlocks != nullptr);
    QVERIFY(plainThick->thickEnd() < plainThick->thickStart());
    QVERIFY(plainBlocks->blocks().empty());

    auto node3 = g_assemblyGraph->m_deBruijnGraphNodes["3+"];
    const auto &emptyInterval = group.getAnnotations(node3);
    QCOMPARE(emptyInterval.size(), 1u);
    QVERIFY(emptyInterval.front()->end() < emptyInterval.front()->start());
    QCOMPARE(emptyInterval.front()->getViews().size(), annotations.front()->getViews().size());
    for (size_t i = 0; i < annotations.front()->getViews().size(); ++i)
        QCOMPARE(emptyInterval.front()->getViews()[i]->getTypeName(),
                 annotations.front()->getViews()[i]->getTypeName());
    QCOMPARE(group.getNodesWithAnnotations(true).size(), 3u);
}

void BandageTests::gfaTagsAndCsvColours() {
    const QString gfaPath = tempFile("tags.gfa");
    QVERIFY(writeTextFile(gfaPath,
                          "S\ta\tAAAA\tCL:Z:#ff0000\tC2:Z:blue\tLB:Z:pos\tL2:Z:neg\tXX:Z:extra\n"
                          "S\tb\tCCCC\n"
                          "L\ta\t+\tb\t+\t0M\tCL:Z:green\tWD:f:2.5\tPS:i:2\tZZ:Z:edge\n"));
    requireGraphLoad(gfaPath);

    auto *aPos = g_assemblyGraph->m_deBruijnGraphNodes["a+"];
    auto *aNeg = g_assemblyGraph->m_deBruijnGraphNodes["a-"];
    auto *bPos = g_assemblyGraph->m_deBruijnGraphNodes["b+"];
    QCOMPARE(g_assemblyGraph->getCustomColour(aPos), QColor("#ff0000"));
    QCOMPARE(g_assemblyGraph->getCustomColour(aNeg), QColor("blue"));
    QCOMPARE(g_assemblyGraph->getCustomLabel(aPos), QString("pos"));
    QCOMPARE(g_assemblyGraph->getCustomLabel(aNeg), QString("neg"));

    auto posTags = g_assemblyGraph->m_nodeTags.find(aPos);
    auto negTags = g_assemblyGraph->m_nodeTags.find(aNeg);
    QVERIFY(posTags != g_assemblyGraph->m_nodeTags.end());
    QVERIFY(negTags != g_assemblyGraph->m_nodeTags.end());
    QVERIFY(hasTag(posTags->second, "XX", "extra"));
    QVERIFY(hasTag(negTags->second, "XX", "extra"));
    QVERIFY(!gfa::getTag<std::string>("CL", posTags->second));
    QVERIFY(!gfa::getTag<std::string>("DP", posTags->second));

    DeBruijnEdge *link = nullptr;
    for (auto *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (edge->getStartingNode() == aPos && edge->getEndingNode() == bPos)
            link = edge;
    }
    QVERIFY(link != nullptr);
    QCOMPARE(g_assemblyGraph->getCustomColour(link), QColor("green"));
    auto style = g_assemblyGraph->getCustomStyle(link);
    QVERIFY(qFuzzyCompare(style.width, 2.5f));
    QCOMPARE(style.lineStyle, Qt::DashLine);
    auto edgeTags = g_assemblyGraph->m_edgeTags.find(link);
    QVERIFY(edgeTags != g_assemblyGraph->m_edgeTags.end());
    QVERIFY(hasTag(edgeTags->second, "ZZ", "edge"));

    const QString written = tempFile("tags-out.gfa");
    QVERIFY(gfa::saveEntireGraph(written, *g_assemblyGraph));
    QFile writtenFile(written);
    QVERIFY(writtenFile.open(QIODevice::ReadOnly));
    const QByteArray saved = writtenFile.readAll();
    QVERIFY(saved.contains("XX:Z:extra"));
    QVERIFY(saved.contains("ZZ:Z:edge"));
    QVERIFY(saved.contains("LB:Z:pos"));
    QVERIFY(saved.contains("L2:Z:neg"));
    QVERIFY(saved.contains("CL:Z:"));

    requireGraphLoad(testFile("test.gfa"));
    const QString csvPath = tempFile("colours.csv");
    QVERIFY(writeTextFile(csvPath,
                          "Node name,Colour\n"
                          "1,#00ff00\n"
                          "2,skyblue\n"
                          "3,plasmid\n"
                          "4,\n"
                          "5,other\n"
                          "6,plasmid\n"));
    QString errormsg;
    QStringList columns;
    bool coloursLoaded = false;
    QVERIFY(g_assemblyGraph->loadCSV(csvPath, &columns, &errormsg, &coloursLoaded));
    QVERIFY(coloursLoaded);
    QCOMPARE(columns, QStringList{"Colour"});
    auto *node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    auto *node2 = g_assemblyGraph->m_deBruijnGraphNodes["2+"];
    auto *node3 = g_assemblyGraph->m_deBruijnGraphNodes["3+"];
    auto *node4 = g_assemblyGraph->m_deBruijnGraphNodes["4+"];
    auto *node5 = g_assemblyGraph->m_deBruijnGraphNodes["5+"];
    auto *node6 = g_assemblyGraph->m_deBruijnGraphNodes["6+"];
    QCOMPARE(g_assemblyGraph->getCustomColour(node1), QColor("#00ff00"));
    QCOMPARE(g_assemblyGraph->getCustomColour(node2), QColor("skyblue"));
    QCOMPARE(g_assemblyGraph->getCustomColour(node3), g_assemblyGraph->getCustomColour(node6));
    QVERIFY(g_assemblyGraph->getCustomColour(node3) != g_assemblyGraph->getCustomColour(node5));
    QVERIFY(!g_assemblyGraph->hasCustomColour(node4));
    QVERIFY(!g_assemblyGraph->m_csvHeaders.isEmpty());

    requireGraphLoad(testFile("test.fastg"));
    QCOMPARE(g_assemblyGraph->m_depthTag, QString("KC"));
    requireGraphLoad(gfaPath);
    QCOMPARE(g_assemblyGraph->m_depthTag, QString());
    QCOMPARE(g_assemblyGraph->m_csvHeaders, QStringList());
    const QString depthWritten = tempFile("no-depth.gfa");
    QVERIFY(gfa::saveEntireGraph(depthWritten, *g_assemblyGraph));
    QFile depthFile(depthWritten);
    QVERIFY(depthFile.open(QIODevice::ReadOnly));
    const QByteArray depthText = depthFile.readAll();
    QVERIFY(!depthText.contains("KC:"));
    QVERIFY(!depthText.contains("DP:"));

    DeBruijnEdge *coloured = nullptr;
    for (auto *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (edge->getStartingNode()->getName() == "a+" && edge->getEndingNode()->getName() == "b+")
            coloured = edge;
    }
    QVERIFY(coloured != nullptr);
    const auto edgeCount = g_assemblyGraph->m_deBruijnGraphEdges.size();
    const auto edgeColourCount = g_assemblyGraph->m_edgeColors.size();
    const auto edgeStyleCount = g_assemblyGraph->m_edgeStyles.size();
    const auto edgeTagCount = g_assemblyGraph->m_edgeTags.size();
    g_assemblyGraph->deleteEdges({coloured});
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphEdges.size(), edgeCount - 1);
    QVERIFY(g_assemblyGraph->m_edgeColors.size() < edgeColourCount);
    QVERIFY(g_assemblyGraph->m_edgeStyles.size() < edgeStyleCount);
    QVERIFY(g_assemblyGraph->m_edgeTags.size() < edgeTagCount);

    g_assemblyGraph->cleanUp();
    auto *self = new DeBruijnNode("s", true, 1.0f, Sequence("ATAT"));
    self->setReverseComplement(self);
    g_assemblyGraph->m_deBruijnGraphNodes["s+"] = self;
    g_assemblyGraph->m_deBruijnGraphNodes["s-"] = self;
    g_assemblyGraph->setCustomColour(self, QColor("#00ff00"));
    g_assemblyGraph->setCustomLabel(self, "self");
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 2);
    QCOMPARE(g_assemblyGraph->m_nodeColors.size(), 1);
    QCOMPARE(g_assemblyGraph->m_nodeLabels.size(), 1);
    g_assemblyGraph->deleteNodes({self});
    QCOMPARE(g_assemblyGraph->m_deBruijnGraphNodes.size(), 0);
    QCOMPARE(g_assemblyGraph->m_nodeColors.size(), 0);
    QCOMPARE(g_assemblyGraph->m_nodeLabels.size(), 0);
}

void BandageTests::nodeColorers() {
    requireGraphLoad(testFile("test.gfa"));
    auto *node1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    auto *node1Neg = node1->getReverseComplement();
    node1->setAsSpecial();

    g_settings->doubleMode = false;
    auto singlePos = makeItem(node1);
    auto singleNeg = makeItem(node1Neg);
    QVERIFY(!singlePos->m_hasArrow);
    auto gray = INodeColorer::create(GRAY_COLOR);
    QCOMPARE(gray->get(singlePos.get()), g_settings->grayColor);
    QCOMPARE(gray->get(singleNeg.get()), g_settings->grayColor);

    auto uniform = INodeColorer::create(UNIFORM_COLOURS);
    QCOMPARE(uniform->get(singlePos.get()), g_settings->uniformNodeSpecialColour);
    node1->setAsNotSpecial();
    QCOMPARE(uniform->get(singlePos.get()), g_settings->uniformPositiveNodeColour);
    QCOMPARE(uniform->get(singleNeg.get()), g_settings->uniformPositiveNodeColour);

    g_settings->doubleMode = true;
    auto doublePos = makeItem(node1);
    auto doubleNeg = makeItem(node1Neg);
    QVERIFY(doublePos->m_hasArrow);
    QCOMPARE(uniform->get(doublePos.get()), g_settings->uniformPositiveNodeColour);
    QCOMPARE(uniform->get(doubleNeg.get()), g_settings->uniformNegativeNodeColour);

    g_assemblyGraph->setCustomColour(node1, QColor("purple"));
    auto custom = INodeColorer::create(CUSTOM_COLOURS);
    QCOMPARE(custom->get(doublePos.get()), QColor("purple"));
    auto *node2 = g_assemblyGraph->m_deBruijnGraphNodes["2+"];
    auto plain = makeItem(node2);
    QCOMPARE(custom->get(plain.get()), g_assemblyGraph->getCustomColourForDisplay(node2));

    g_settings->autoDepthValue = false;
    g_settings->lowDepthValue = 0;
    g_settings->highDepthValue = 100;
    node1->setDepth(0);
    node2->setDepth(100);
    auto *node3 = g_assemblyGraph->m_deBruijnGraphNodes["3+"];
    node3->setDepth(50);
    auto depth = INodeColorer::create(DEPTH_COLOUR);
    auto depth0 = makeItem(node1);
    auto depth100 = makeItem(node2);
    auto depth50 = makeItem(node3);
    QVERIFY(depth->get(depth0.get()) != depth->get(depth100.get()));
    QVERIFY(depth->get(depth0.get()) != depth->get(depth50.get()));
    QVERIFY(depth->get(depth0.get()).isValid());
    g_settings->autoDepthValue = true;
    g_assemblyGraph->determineGraphInfo();
    QVERIFY(depth->get(depth50.get()).isValid());

    Sequence atSeq(std::string(20, 'A'));
    Sequence gcSeq(std::string(10, 'G') + std::string(10, 'C'));
    auto *atNode = new DeBruijnNode("at", true, 1.0, atSeq);
    auto *atTwin = new DeBruijnNode("at", false, 1.0, atSeq);
    auto *gcNode = new DeBruijnNode("gc", true, 1.0, gcSeq);
    auto *gcTwin = new DeBruijnNode("gc", false, 1.0, gcSeq);
    atNode->setReverseComplement(atTwin);
    atTwin->setReverseComplement(atNode);
    gcNode->setReverseComplement(gcTwin);
    gcTwin->setReverseComplement(gcNode);
    g_assemblyGraph->m_deBruijnGraphNodes[atNode->getName()] = atNode;
    g_assemblyGraph->m_deBruijnGraphNodes[atTwin->getName()] = atTwin;
    g_assemblyGraph->m_deBruijnGraphNodes[gcNode->getName()] = gcNode;
    g_assemblyGraph->m_deBruijnGraphNodes[gcTwin->getName()] = gcTwin;
    auto *atAgain = new DeBruijnNode("at2", true, 1.0, atSeq);
    auto *atAgainTwin = new DeBruijnNode("at2", false, 1.0, atSeq);
    atAgain->setReverseComplement(atAgainTwin);
    atAgainTwin->setReverseComplement(atAgain);
    g_assemblyGraph->m_deBruijnGraphNodes[atAgain->getName()] = atAgain;
    g_assemblyGraph->m_deBruijnGraphNodes[atAgainTwin->getName()] = atAgainTwin;
    auto gcColorer = INodeColorer::create(GC_CONTENT);
    auto atItem = makeItem(atNode);
    auto atItem2 = makeItem(atAgain);
    auto gcItem = makeItem(gcNode);
    QCOMPARE(gcColorer->get(atItem.get()), gcColorer->get(atItem2.get()));
    QVERIFY(gcColorer->get(atItem.get()) != gcColorer->get(gcItem.get()));

    auto random = INodeColorer::create(RANDOM_COLOURS);
    auto pair = random->get(doublePos.get(), doubleNeg.get());
    QCOMPARE(pair.first.hslHue(), pair.second.hslHue());
    QVERIFY(pair.first.hslSaturation() != pair.second.hslSaturation() ||
            pair.first.lightness() != pair.second.lightness());

    Sequence lineSeq(std::string(8, 'A'));
    auto addPair = [&](const char *name) {
        auto *pos = new DeBruijnNode(name, true, 1.0, lineSeq);
        auto *neg = new DeBruijnNode(name, false, 1.0, lineSeq);
        pos->setReverseComplement(neg);
        neg->setReverseComplement(pos);
        g_assemblyGraph->m_deBruijnGraphNodes[pos->getName()] = pos;
        g_assemblyGraph->m_deBruijnGraphNodes[neg->getName()] = neg;
        return pos;
    };
    g_assemblyGraph->cleanUp();
    g_assemblyGraph.reset(new AssemblyGraph());
    auto *a = addPair("a");
    auto *b = addPair("b");
    auto *c = addPair("c");
    auto *d = addPair("d");
    g_assemblyGraph->createDeBruijnEdge("a+", "b+", 0, EXACT_OVERLAP);
    g_assemblyGraph->createDeBruijnEdge("b+", "c+", 0, EXACT_OVERLAP);
    auto contiguity = INodeColorer::create(CONTIGUITY_COLOUR);
    auto *contiguityColorer = dynamic_cast<ContiguityNodeColorer *>(contiguity.get());
    QVERIFY(contiguityColorer != nullptr);
    contiguityColorer->determineContiguity(b);
    QCOMPARE(contiguityColorer->getContiguityStatus(b), STARTING);
    QCOMPARE(contiguityColorer->getContiguityStatus(a), CONTIGUOUS_STRAND_SPECIFIC);
    QCOMPARE(contiguityColorer->getContiguityStatus(c), CONTIGUOUS_STRAND_SPECIFIC);
    QCOMPARE(contiguityColorer->getContiguityStatus(d), NOT_CONTIGUOUS);
    g_settings->doubleMode = true;
    auto bItem = makeItem(b);
    QCOMPARE(contiguity->get(bItem.get()), g_settings->contiguityStartingColour);
    g_settings->doubleMode = false;
    auto aSingle = makeItem(a);
    QVERIFY(!aSingle->m_hasArrow);
    QCOMPARE(contiguity->get(aSingle.get()), g_settings->contiguousStrandSpecificColour);

    auto *s = addPair("s");
    auto *m = addPair("m");
    auto *p = addPair("p");
    auto *q = addPair("q");
    g_assemblyGraph->createDeBruijnEdge("s+", "m+", 0, EXACT_OVERLAP);
    g_assemblyGraph->createDeBruijnEdge("m+", "p+", 0, EXACT_OVERLAP);
    g_assemblyGraph->createDeBruijnEdge("m+", "q+", 0, EXACT_OVERLAP);
    auto branchColorer = INodeColorer::create(CONTIGUITY_COLOUR);
    auto *branch = dynamic_cast<ContiguityNodeColorer *>(branchColorer.get());
    QVERIFY(branch != nullptr);
    branch->determineContiguity(s);
    QCOMPARE(branch->getContiguityStatus(m), CONTIGUOUS_STRAND_SPECIFIC);
    QCOMPARE(branch->getContiguityStatus(p), CONTIGUOUS_STRAND_SPECIFIC);
    QCOMPARE(branch->getContiguityStatus(q), CONTIGUOUS_STRAND_SPECIFIC);

    requireGraphLoad(testFile("test_rgfa.gfa"));
    auto tags = INodeColorer::create(TAG_VALUE);
    auto *tagColorer = dynamic_cast<TagValueNodeColorer *>(tags.get());
    QVERIFY(tagColorer != nullptr);
    auto names = tagColorer->tagNames();
    QVERIFY(std::find(names.begin(), names.end(), "SN") != names.end());
    tagColorer->setTagName("SN");
    auto *v1 = g_assemblyGraph->m_deBruijnGraphNodes["v1+"];
    auto *v2 = g_assemblyGraph->m_deBruijnGraphNodes["v2+"];
    auto *v5 = g_assemblyGraph->m_deBruijnGraphNodes["v5+"];
    auto v1Item = makeItem(v1);
    auto v2Item = makeItem(v2);
    auto v5Item = makeItem(v5);
    QCOMPARE(tags->get(v1Item.get()), tags->get(v2Item.get()));
    QVERIFY(tags->get(v1Item.get()) != tags->get(v5Item.get()));
    tagColorer->setTagName("NO");
    QCOMPARE(tags->get(v1Item.get()), g_assemblyGraph->getCustomColourForDisplay(v1));

    requireGraphLoad(testFile("test.gfa"));
    const QString csvPath = tempFile("colorer.csv");
    QVERIFY(writeTextFile(csvPath,
                          "Node name,Colour\n"
                          "1,#00ff00\n"
                          "3,plasmid\n"
                          "5,plasmid\n"
                          "4,\n"));
    QString errormsg;
    QStringList columns;
    bool coloursLoaded = false;
    QVERIFY(g_assemblyGraph->loadCSV(csvPath, &columns, &errormsg, &coloursLoaded));
    auto csv = INodeColorer::create(CSV_COLUMN);
    auto *csvColorer = dynamic_cast<CSVNodeColorer *>(csv.get());
    QVERIFY(csvColorer != nullptr);
    csvColorer->setColumnIdx(0);
    csvColorer->reset();
    auto *csv1 = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    auto *csv3 = g_assemblyGraph->m_deBruijnGraphNodes["3+"];
    auto *csv4 = g_assemblyGraph->m_deBruijnGraphNodes["4+"];
    auto *csv5 = g_assemblyGraph->m_deBruijnGraphNodes["5+"];
    auto *csv6 = g_assemblyGraph->m_deBruijnGraphNodes["6+"];
    auto csv1Item = makeItem(csv1);
    auto csv3Item = makeItem(csv3);
    auto csv4Item = makeItem(csv4);
    auto csv5Item = makeItem(csv5);
    auto csv6Item = makeItem(csv6);
    QCOMPARE(csv->get(csv3Item.get()), csv->get(csv5Item.get()));
    QVERIFY(csv->get(csv3Item.get()) != csv->get(csv1Item.get()));
    QColor emptyCell = csv->get(csv4Item.get());
    QVERIFY(emptyCell.isValid());
    csvColorer->setColumnIdx(5);
    QCOMPARE(csv->get(csv6Item.get()), g_assemblyGraph->getCustomColourForDisplay(csv6));
}

void BandageTests::hitAnnotationCoordinates() {
    requireGraphLoad(testFile("test.gfa"));
    auto *node = g_assemblyGraph->m_deBruijnGraphNodes["1+"];
    auto *query = new search::Query("constructed", "ACGTACGTACGTACGT");
    query->addHit(new search::Hit(query, node, 100.0, 10, 0, 0, 1, 10, 5, 14,
                                  SciNot(1.0, -10), 20.0));
    g_annotationsManager->updateGroupFromHits(g_graphSearch->annotationGroupName(), {query});
    const auto *group = g_annotationsManager->findGroupByName(g_graphSearch->annotationGroupName());
    QVERIFY(group != nullptr);
    const auto &annotations = group->getAnnotations(node);
    QCOMPARE(annotations.size(), 1u);
    QCOMPARE(annotations.front()->start(), 5);
    QCOMPARE(annotations.front()->end(), 14);
    QCOMPARE(annotations.front()->text(), std::string("constructed"));
    delete query;
}

static QString buildHmm(const QString &stoPath, const QString &hmmPath,
                          const QByteArray &stockholm, const QString &alphabet) {
    if (!writeTextFile(stoPath, stockholm))
        return "could not write Stockholm alignment";
    QProcess hmmbuild;
    hmmbuild.start("hmmbuild", {alphabet, hmmPath, stoPath});
    if (!hmmbuild.waitForFinished(20000) || hmmbuild.exitCode() != 0)
        return QString("hmmbuild failed: ") + hmmbuild.readAllStandardError();
    return {};
}

static QByteArray searchNodeSequence() {
    return "ATGAAAACCCTGCTGATCCTGGCAGCAGCAGCAGCAAAAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCAGCA";
}

void BandageTests::minimap2Search() {
    const QByteArray sequence = searchNodeSequence();
    const QByteArray window = sequence.left(40);
    const QString gfaPath = tempFile("minimap2-search.gfa");
    QVERIFY(writeTextFile(gfaPath, "S\ts\t" + sequence + "\n"));
    requireGraphLoad(gfaPath);
    auto *node = g_assemblyGraph->m_deBruijnGraphNodes["s+"];
    QVERIFY(node != nullptr);

    const QString fastaPath = tempFile("minimap2-query.fasta");
    QVERIFY(writeTextFile(fastaPath, ">window\n" + window + "\n"));
    g_graphSearch = search::GraphSearch::get(search::Minimap2, QDir(m_tmpDir.path()));
    QString error = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph, fastaPath, false, "-x sr --for-only");
    QVERIFY2(error.isEmpty(), qPrintable(error));
    search::Query *windowQuery = g_graphSearch->getQueryFromName("window");
    QVERIFY(windowQuery != nullptr);
    QVERIFY2(!windowQuery->getHits().empty(), qPrintable(error));
    bool covered = false;
    for (const auto &hit : windowQuery->getHits()) {
        if (hit->m_node != node && hit->m_node != node->getReverseComplement())
            continue;
        if (hit->m_nodeEnd > hit->m_nodeStart && hit->m_nodeStart <= 40 && hit->m_nodeEnd >= 20)
            covered = true;
    }
    QVERIFY(covered);

    const QString proteinPath = tempFile("minimap2-protein.fasta");
    QVERIFY(writeTextFile(proteinPath, ">pep\nMKTLLILF\n"));
    g_graphSearch = search::GraphSearch::get(search::Minimap2, QDir(m_tmpDir.path()));
    error = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph, proteinPath);
    QVERIFY(error.contains("Cannot handle non-nucleotide query"));
    for (auto *query : g_graphSearch->queries().queries())
        QCOMPARE(query->getHits().size(), 0u);
}

void BandageTests::hmmerSearch() {
    const QByteArray sequence = searchNodeSequence();
    const QByteArray window = sequence.left(40);
    const QString gfaPath = tempFile("hmmer-search.gfa");
    QVERIFY(writeTextFile(gfaPath, "S\ts\t" + sequence + "\n"));
    requireGraphLoad(gfaPath);
    auto *node = g_assemblyGraph->m_deBruijnGraphNodes["s+"];
    QVERIFY(node != nullptr);

    const QString nuclHmm = tempFile("nucl.hmm");
    QString hmmError = buildHmm(tempFile("nucl.sto"), nuclHmm,
                                "# STOCKHOLM 1.0\n"
                                "#=GF ID nuclq\n"
                                "nuclq\t" + window + "\n"
                                "//\n",
                                "--dna");
    QVERIFY2(hmmError.isEmpty(), qPrintable(hmmError));
    g_graphSearch = search::GraphSearch::get(search::NHMMER, QDir(m_tmpDir.path()));
    QString error = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph, nuclHmm);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    bool nuclHit = false;
    for (auto *query : g_graphSearch->queries().queries()) {
        QCOMPARE(query->getSequenceType(), search::NUCLEOTIDE);
        for (const auto &hit : query->getHits())
            nuclHit = nuclHit || hit->m_node == node || hit->m_node == node->getReverseComplement();
    }
    QVERIFY(nuclHit);

    const QString aaHmm = tempFile("aa.hmm");
    hmmError = buildHmm(tempFile("aa.sto"), aaHmm,
                        "# STOCKHOLM 1.0\n"
                        "#=GF ID aaq\n"
                        "aaq\tMKTLLIL\n"
                        "//\n",
                        "--amino");
    QVERIFY2(hmmError.isEmpty(), qPrintable(hmmError));
    g_graphSearch = search::GraphSearch::get(search::NHMMER, QDir(m_tmpDir.path()));
    error = g_graphSearch->doAutoGraphSearch(*g_assemblyGraph, aaHmm);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(g_graphSearch->annotationGroupName(), QString("HMMER hits"));
    bool proteinHit = false;
    for (auto *query : g_graphSearch->queries().queries()) {
        QCOMPARE(query->getSequenceType(), search::PROTEIN);
        for (const auto &hit : query->getHits())
            proteinHit = proteinHit || hit->m_node == node || hit->m_node == node->getReverseComplement();
    }
    QVERIFY(proteinHit);
}

void BandageTests::gfaJumpsOverlapsAndFastaCircularity() {
    const QString jumpsPath = tempFile("jumps.gfa");
    QVERIFY(writeTextFile(jumpsPath,
                          "S\ta\tAAAA\n"
                          "S\tb\tCCCC\n"
                          "J\ta\t+\tb\t+\t15\n"
                          "J\ta\t+\tb\t+\t*\n"));
    requireGraphLoad(jumpsPath);
    int distance15 = 0;
    int distanceStar = 0;
    for (auto *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (edge->getOverlapType() != JUMP)
            continue;
        QCOMPARE(g_assemblyGraph->getCustomColour(edge), QColor("red"));
        QCOMPARE(g_assemblyGraph->getCustomStyle(edge).lineStyle, Qt::DashLine);
        if (edge->getOverlap() == 15)
            ++distance15;
        if (edge->getOverlap() == 0)
            ++distanceStar;
    }
    QVERIFY(distance15 > 0);
    QVERIFY(distanceStar > 0);
    const QString writtenJumps = tempFile("jumps-out.gfa");
    QVERIFY(gfa::saveEntireGraph(writtenJumps, *g_assemblyGraph));
    QFile jumpFile(writtenJumps);
    QVERIFY(jumpFile.open(QIODevice::ReadOnly));
    const QByteArray jumpText = jumpFile.readAll();
    QVERIFY(jumpText.contains("\nJ\t") || jumpText.startsWith("J\t"));

    g_settings->jumpsAsLinks = true;
    requireGraphLoad(jumpsPath);
    int extraLinks = 0;
    for (auto *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (edge->getOverlapType() != EXTRA_LINK)
            continue;
        ++extraLinks;
        QCOMPARE(edge->getOverlap(), 0);
        QCOMPARE(g_assemblyGraph->getCustomColour(edge), QColor("green"));
        QCOMPARE(g_assemblyGraph->getCustomStyle(edge).lineStyle, Qt::DotLine);
    }
    QVERIFY(extraLinks > 0);
    const QString writtenLinks = tempFile("jumps-as-links.gfa");
    QVERIFY(gfa::saveEntireGraph(writtenLinks, *g_assemblyGraph));
    QFile linkFile(writtenLinks);
    QVERIFY(linkFile.open(QIODevice::ReadOnly));
    const QByteArray linkText = linkFile.readAll();
    QVERIFY(linkText.contains("\nL\t") || linkText.startsWith("L\t"));
    QVERIFY(linkText.contains("\t*"));
    QVERIFY(!linkText.contains("15M"));
    QVERIFY(!linkText.contains("\nJ\t") && !linkText.startsWith("J\t"));
    g_settings->jumpsAsLinks = false;

    g_assemblyGraph->cleanUp();
    auto addPair = [&](const char *name, const std::string &bases) {
        Sequence seq(bases);
        auto *pos = new DeBruijnNode(name, true, 1.0, seq);
        auto *neg = new DeBruijnNode(name, false, 1.0, seq);
        pos->setReverseComplement(neg);
        neg->setReverseComplement(pos);
        g_assemblyGraph->m_deBruijnGraphNodes[pos->getName()] = pos;
        g_assemblyGraph->m_deBruijnGraphNodes[neg->getName()] = neg;
        return pos;
    };
    auto *jumpFrom = addPair("ja", "AAAA");
    auto *jumpTo = addPair("jb", "CCCC");
    g_assemblyGraph->createDeBruijnEdge("ja+", "jb+", 15, JUMP);
    auto jumpPath = Path::makeFromOrderedNodes({jumpFrom, jumpTo}, false);
    QCOMPARE(jumpPath.nodes().size(), 2u);
    QCOMPARE(jumpPath.getLength(), 8);
    QCOMPARE(jumpPath.getPathSequence(), QByteArray("AAAACCCC"));
    QCOMPARE(jumpPath.getPosition(jumpTo).front(), 5);
    auto *exactFrom = addPair("ea", "AAAA");
    auto *exactTo = addPair("eb", "AACC");
    g_assemblyGraph->createDeBruijnEdge("ea+", "eb+", 2, EXACT_OVERLAP);
    auto exactPath = Path::makeFromOrderedNodes({exactFrom, exactTo}, false);
    QCOMPARE(exactPath.getLength(), 6);
    QCOMPARE(exactPath.getPathSequence(), QByteArray("AAAACC"));
    QCOMPARE(exactPath.getPosition(exactTo).front(), 3);

    const QString overlapPath = tempFile("overlaps.gfa");
    QVERIFY(writeTextFile(overlapPath,
                          "S\ta\tAAAAAAAAAAAA\n"
                          "S\tb\tCCCCCCCCCCCC\n"
                          "L\ta\t+\tb\t+\t5M1I4M\n"
                          "L\ta\t+\tb\t+\t12M\n"));
    g_assemblyGraph->cleanUp();
    auto builder = io::AssemblyGraphBuilder::get(overlapPath);
    QVERIFY(builder != nullptr);
    if (auto err = builder->build(*g_assemblyGraph))
        QFAIL(bandage::toString(std::move(err)).c_str());
    QVERIFY(builder->hasComplexOverlaps());
    auto hasCigar = [](const DeBruijnEdge *edge) {
        auto keyIt = g_assemblyGraph->m_overlapKeys.find(edge);
        return keyIt != g_assemblyGraph->m_overlapKeys.end() && keyIt->second == "5M1I4M" &&
               edge->getOverlap() == 0;
    };
    bool sawCigar = false;
    bool sawTwelve = false;
    for (auto *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (hasCigar(edge))
            sawCigar = true;
        if (edge->getOverlapType() == EXACT_OVERLAP && edge->getOverlap() == 12)
            sawTwelve = true;
    }
    QVERIFY(sawCigar);
    QVERIFY(sawTwelve);
    const QString writtenOverlaps = tempFile("overlaps-out.gfa");
    QVERIFY(gfa::saveEntireGraph(writtenOverlaps, *g_assemblyGraph));
    QFile overlapFile(writtenOverlaps);
    QVERIFY(overlapFile.open(QIODevice::ReadOnly));
    const QByteArray overlapText = overlapFile.readAll();
    QVERIFY(overlapText.contains("5M1I4M"));
    QVERIFY(overlapText.contains("12M"));
    QVERIFY(!overlapText.contains("0M"));
    requireGraphLoad(writtenOverlaps);
    bool reloadedCigar = false;
    bool reloadedTwelve = false;
    for (auto *edge : g_assemblyGraph->m_deBruijnGraphEdges) {
        if (hasCigar(edge))
            reloadedCigar = true;
        if (edge->getOverlapType() == EXACT_OVERLAP && edge->getOverlap() == 12)
            reloadedTwelve = true;
    }
    QVERIFY(reloadedCigar);
    QVERIFY(reloadedTwelve);

    const QString fastaPath = tempFile("circular.fa");
    QVERIFY(writeTextFile(fastaPath,
                          ">seq1 circular=true depth=4.5\n"
                          "ACGTACGT\n"
                          ">Contig_2_3.5_Circ\n"
                          "AAAAAAAA\n"
                          ">plain\n"
                          "CCCCCCCC\n"));
    requireGraphLoad(fastaPath);
    auto *seq1 = g_assemblyGraph->m_deBruijnGraphNodes["seq1+"];
    auto *skesa = g_assemblyGraph->m_deBruijnGraphNodes["2+"];
    auto *plain = g_assemblyGraph->m_deBruijnGraphNodes["plain+"];
    QVERIFY(seq1 != nullptr);
    QVERIFY(skesa != nullptr);
    QVERIFY(plain != nullptr);
    QCOMPARE(seq1->getDepth(), 4.5);
    auto selfLoop = [](DeBruijnNode *node) {
        for (auto *edge : node->edges()) {
            if (edge->getStartingNode() == node && edge->getEndingNode() == node)
                return true;
        }
        return false;
    };
    QVERIFY(selfLoop(seq1));
    QVERIFY(selfLoop(skesa));
    QVERIFY(!selfLoop(plain));
}

void BandageTests::partialNodeNameScope() {
    const QString gfaPath = tempFile("partial-names.gfa");
    QVERIFY(writeTextFile(gfaPath,
                          "S\t12\tAAAA\n"
                          "S\t123\tCCCC\n"));
    requireGraphLoad(gfaPath);
    QString errorTitle;
    QString errorMessage;
    g_settings->doubleMode = false;
    g_settings->startingNodesExactMatch = true;
    QCOMPARE(drawScope(graph::Scope::aroundNodes("12"), errorTitle, errorMessage), 1);
    QVERIFY(nodeIsDrawn("12+"));
    QVERIFY(!nodeIsDrawn("123+"));

    g_settings->startingNodesExactMatch = false;
    QCOMPARE(drawScope(graph::Scope::aroundNodes("12"), errorTitle, errorMessage), 2);
    QVERIFY(nodeIsDrawn("12+"));
    QVERIFY(nodeIsDrawn("123+"));

    QCOMPARE(drawScope(graph::Scope::aroundNodes("zzzz"), errorTitle, errorMessage), 0);
    QCOMPARE(errorTitle, QString("Nodes not found"));
}

QTEST_MAIN(BandageTests)
#include "bandagetests.moc"
