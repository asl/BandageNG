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


#include "mainwindow.h"
#include "graph/graphscope.h"
#include "ui_mainwindow.h"

#include "ui/dialogs/settingsdialog.h"
#include "ui/dialogs/aboutdialog.h"
#include "ui/dialogs/graphsearchdialog.h"
#include "bandagegraphicsview.h"
#include "graphicsviewzoom.h"
#include "bandagegraphicsscene.h"
#include "ui/dialogs/myprogressdialog.h"
#include "ui/dialogs/pathspecifydialog.h"
#include "ui/dialogs/changenodenamedialog.h"
#include "ui/dialogs/changenodedepthdialog.h"
#include "ui/dialogs/graphinfodialog.h"
#include "ui/dialogs/pathlistdialog.h"
#include "ui/dialogs/walklistdialog.h"

#include "graphsearch/blast/blastsearch.h"
#include "graphsearch/minimap2/minimap2search.h"

#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"
#include "graph/debruijnedge.h"
#include "graph/graphicsitemnode.h"
#include "graph/graphicsitemedge.h"
#include "graph/graphicsitemlink.h"
#include "graph/path.h"
#include "graph/sequenceutils.h"
#include "graph/io.h"
#include "graph/nodecolorers.h"
#include "graph/gfawriter.h"
#include "graph/fastawriter.h"

#include "layout/graphlayoutworker.h"
#include "layout/io.h"

#include "program/globals.h"
#include "program/memory.h"
#include "program/settings.h"

#include <QFileDialog>
#include <QLatin1String>
#include <QTextStream>
#include <QClipboard>
#include <QTransform>
#include <QFontDialog>
#include <QColorDialog>
#include <QFile>
#include <QScrollBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QShortcut>
#include <QMainWindow>
#include <QDesktopServices>
#include <QSvgGenerator>
#include <QCompleter>
#include <QStringListModel>
#include <QtConcurrent>
#include <QFutureWatcher>

#include <iterator>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <filesystem>

MainWindow::MainWindow(QString fileToLoadOnStartup, bool drawGraphAfterLoad) :
    QMainWindow(nullptr),
    ui(new Ui::MainWindow), m_imageFilter("PNG (*.png)"),
    m_fileToLoadOnStartup(fileToLoadOnStartup), m_drawGraphAfterLoad(drawGraphAfterLoad),
    m_uiState(NO_GRAPH_LOADED), m_blastSearchDialog(nullptr), m_alreadyShown(false)
{
    ui->setupUi(this);

    QApplication::setWindowIcon(QIcon(QPixmap(":/icons/icon.png")));
    ui->graphicsViewWidget->layout()->addWidget(g_graphicsView);

    srand(time(nullptr));

    m_previousZoomSpinBoxValue = ui->zoomSpinBox->value();
    ui->zoomSpinBox->setMinimum(g_settings->minZoom * 100.0);
    ui->zoomSpinBox->setMaximum(g_settings->maxZoom * 100.0);

    //The normal height of the QPlainTextEdit objects is a bit much,
    //so fix them at a smaller height.
    ui->selectedNodesTextEdit->setFixedHeight(ui->selectedNodesTextEdit->sizeHint().height() / 2.5);
    ui->selectedEdgesTextEdit->setFixedHeight(ui->selectedEdgesTextEdit->sizeHint().height() / 2.5);

    setUiState(NO_GRAPH_LOADED);

    m_graphicsViewZoom = new GraphicsViewZoom(g_graphicsView);
    g_graphicsView->m_zoom = m_graphicsViewZoom;

    m_scene = new BandageGraphicsScene(this);
    g_graphicsView->setScene(m_scene);

    //Nothing is selected yet, so this will hide the appropriate labels.
    selectionChanged();

    //The user may have specified settings on the command line, so it is now
    //necessary to update the UI to match these settings.
    setWidgetsFromSettings();
    setTextDisplaySettings();

    graphScopeChanged();
    switchColourScheme();

    ui->bedButton->setContent(ui->bedLoadWidget);
    ui->annotationsButton->setContent(ui->annotationsListWidget);
    ui->blastDetailsButton->setContent(ui->blastDetailsWidget);

    //If this is a Mac, change the 'Delete' shortcuts to 'Backspace' instead.
#ifdef Q_OS_MAC
    ui->actionHide_selected_nodes->setShortcut(Qt::Key_Backspace);
    ui->actionRemove_selection_from_graph->setShortcut(Qt::SHIFT | Qt::Key_Backspace);
#endif

    connect(ui->drawGraphButton, SIGNAL(clicked()), this, SLOT(drawGraph()));
    connect(ui->actionLoad_graph, SIGNAL(triggered()), this, SLOT(loadGraph()));
    connect(ui->actionLoad_CSV, SIGNAL(triggered(bool)), this, SLOT(loadCSV()));
    connect(ui->actionLoad_layout, SIGNAL(triggered()), this, SLOT(loadGraphLayout()));
    connect(ui->actionLoad_paths, SIGNAL(triggered()), this, SLOT(loadGraphPaths()));
    connect(ui->actionLoad_links, SIGNAL(triggered()), this, SLOT(loadGraphLinks()));
    connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(close()));
    connect(ui->graphScopeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(graphScopeChanged()));
    connect(ui->zoomSpinBox, SIGNAL(valueChanged(double)), this, SLOT(zoomSpinBoxChanged()));
    connect(m_graphicsViewZoom, SIGNAL(zoomed()), this, SLOT(zoomedWithMouseWheel()));
    connect(ui->actionCopy_selected_node_sequences_to_clipboard, SIGNAL(triggered()), this, SLOT(copySelectedSequencesToClipboardActionTriggered()));
    connect(ui->actionSave_selected_node_sequences_to_FASTA, SIGNAL(triggered()), this, SLOT(saveSelectedSequencesToFileActionTriggered()));
    connect(ui->actionCopy_selected_node_path_to_clipboard, SIGNAL(triggered(bool)), this, SLOT(copySelectedPathToClipboard()));
    connect(ui->actionSave_selected_node_path_to_FASTA, SIGNAL(triggered(bool)), this, SLOT(saveSelectedPathToFile()));
    connect(ui->coloursComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(switchColourScheme()));
    connect(ui->tagsComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(switchTagValue()));
    connect(ui->actionSave_image_current_view, SIGNAL(triggered()), this, SLOT(saveImageCurrentView()));
    connect(ui->actionSave_image_entire_scene, SIGNAL(triggered()), this, SLOT(saveImageEntireScene()));
    connect(ui->actionExport_layout, SIGNAL(triggered()), this, SLOT(exportGraphLayout()));
    connect(ui->nodeCustomLabelsCheckBox, SIGNAL(toggled(bool)), this, SLOT(setTextDisplaySettings()));
    connect(ui->nodeNamesCheckBox, SIGNAL(toggled(bool)), this, SLOT(setTextDisplaySettings()));
    connect(ui->nodeLengthsCheckBox, SIGNAL(toggled(bool)), this, SLOT(setTextDisplaySettings()));
    connect(ui->nodeDepthCheckBox, SIGNAL(toggled(bool)), this, SLOT(setTextDisplaySettings()));
    connect(ui->csvCheckBox, SIGNAL(toggled(bool)), this, SLOT(setTextDisplaySettings()));
    connect(ui->csvComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(setTextDisplaySettings()));
    connect(ui->textOutlineCheckBox, SIGNAL(toggled(bool)), this, SLOT(setTextDisplaySettings()));
    connect(ui->fontButton, SIGNAL(clicked()), this, SLOT(fontButtonPressed()));
    connect(ui->setNodeCustomColourButton, SIGNAL(clicked()), this, SLOT(setNodeCustomColour()));
    connect(ui->setNodeCustomLabelButton, SIGNAL(clicked()), this, SLOT(setNodeCustomLabel()));
    connect(ui->actionSettings, SIGNAL(triggered()), this, SLOT(openSettingsDialog()));
    connect(ui->selectNodesButton, SIGNAL(clicked()), this, SLOT(selectUserSpecifiedNodes()));
    connect(ui->pathSelectButton, SIGNAL(clicked()), this, SLOT(selectPathNodes()));
    connect(ui->pathListButton, &QPushButton::clicked, this, &MainWindow::showPathListDialog);
    connect(ui->walkSelectButton, SIGNAL(clicked()), this, SLOT(selectWalkNodes()));
    connect(ui->walkListButton, &QPushButton::clicked, this, &MainWindow::showWalkListDialog);
    connect(ui->selectionSearchNodesLineEdit, SIGNAL(returnPressed()), this, SLOT(selectUserSpecifiedNodes()));
    connect(ui->actionAbout, SIGNAL(triggered()), this, SLOT(openAboutDialog()));
    connect(ui->blastSearchButton, SIGNAL(clicked()), this, SLOT(openBlastSearchDialog()));
    connect(ui->blastQueryComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(blastQueryChanged()));
    connect(ui->actionControls_panel, SIGNAL(toggled(bool)), this, SLOT(showHidePanels()));
    connect(ui->actionSelection_panel, SIGNAL(toggled(bool)), this, SLOT(showHidePanels()));
    connect(ui->contiguityButton, SIGNAL(clicked()), this, SLOT(determineContiguityFromSelectedNode()));
    connect(ui->actionBring_selected_nodes_to_front, SIGNAL(triggered()), this, SLOT(bringSelectedNodesToFront()));
    connect(ui->actionSelect_nodes_with_BLAST_hits, SIGNAL(triggered()), this, SLOT(selectNodesWithBlastHits()));
    connect(ui->actionSelect_nodes_with_dead_ends, SIGNAL(triggered()), this, SLOT(selectNodesWithDeadEnds()));
    connect(ui->actionSelect_all, SIGNAL(triggered()), this, SLOT(selectAll()));
    connect(ui->actionSelect_none, SIGNAL(triggered()), this, SLOT(selectNone()));
    connect(ui->actionInvert_selection, SIGNAL(triggered()), this, SLOT(invertSelection()));
    connect(ui->actionZoom_to_selection, SIGNAL(triggered()), this, SLOT(zoomToSelection()));
    connect(ui->actionZoom_to_fit_graph, SIGNAL(triggered()), this, SLOT(zoomToFitScene()));
    connect(ui->actionSelect_contiguous_nodes, SIGNAL(triggered()), this, SLOT(selectContiguous()));
    connect(ui->actionSelect_possibly_contiguous_nodes, SIGNAL(triggered()), this, SLOT(selectMaybeContiguous()));
    connect(ui->actionSelect_not_contiguous_nodes, SIGNAL(triggered()), this, SLOT(selectNotContiguous()));
    connect(ui->actionBandage_online_help, SIGNAL(triggered()), this, SLOT(openBandageUrl()));
    connect(ui->nodeDistanceSpinBox, SIGNAL(valueChanged(int)), this, SLOT(nodeDistanceChanged()));
    connect(ui->minDepthSpinBox, SIGNAL(valueChanged(double)), this, SLOT(depthRangeChanged()));
    connect(ui->maxDepthSpinBox, SIGNAL(valueChanged(double)), this, SLOT(depthRangeChanged()));
    connect(ui->startingNodesExactMatchRadioButton, SIGNAL(toggled(bool)), this, SLOT(startingNodesExactMatchChanged()));
    connect(ui->actionSpecify_exact_path_for_copy_save, SIGNAL(triggered()), this, SLOT(openPathSpecifyDialog()));
    connect(ui->nodeWidthSpinBox, SIGNAL(valueChanged(double)), this, SLOT(nodeWidthChanged()));
    connect(g_graphicsView, SIGNAL(copySelectedSequencesToClipboard()), this, SLOT(copySelectedSequencesToClipboard()));
    connect(g_graphicsView, SIGNAL(saveSelectedSequencesToFile()), this, SLOT(saveSelectedSequencesToFile()));
    connect(ui->actionSave_entire_graph_to_FASTA, SIGNAL(triggered(bool)), this, SLOT(saveEntireGraphToFasta()));
    connect(ui->actionSave_entire_graph_to_FASTA_only_positive_nodes, SIGNAL(triggered(bool)), this, SLOT(saveEntireGraphToFastaOnlyPositiveNodes()));
    connect(ui->actionSave_entire_graph_to_GFA, SIGNAL(triggered(bool)), this, SLOT(saveEntireGraphToGfa()));
    connect(ui->actionSave_visible_graph_to_GFA, SIGNAL(triggered(bool)), this, SLOT(saveVisibleGraphToGfa()));
    connect(ui->actionWeb_BLAST_selected_nodes, SIGNAL(triggered(bool)), this, SLOT(webBlastSelectedNodes()));
    connect(ui->actionHide_selected_nodes, SIGNAL(triggered(bool)), this, SLOT(hideNodes()));
    connect(ui->actionRemove_selection_from_graph, SIGNAL(triggered(bool)), this, SLOT(removeSelection()));
    connect(ui->actionDuplicate_selected_nodes, SIGNAL(triggered(bool)), this, SLOT(duplicateSelectedNodes()));
    connect(ui->actionMerge_selected_nodes, SIGNAL(triggered(bool)), this, SLOT(mergeSelectedNodes()));
    connect(ui->actionMerge_all_possible_nodes, SIGNAL(triggered(bool)), this, SLOT(mergeAllPossible()));
    connect(ui->actionChange_node_name, SIGNAL(triggered(bool)), this, SLOT(changeNodeName()));
    connect(ui->actionChange_node_depth, SIGNAL(triggered(bool)), this, SLOT(changeNodeDepth()));
    connect(ui->moreInfoButton, SIGNAL(clicked(bool)), this, SLOT(openGraphInfoDialog()));

    connect(this, SIGNAL(windowLoaded()), this, SLOT(afterMainWindowShow()), Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
}


// This function runs after the MainWindow has been shown.  This code is not
// included in the constructor because it can perform a BLAST search, which
// will fill the BLAST query combo box and screw up widget sizes.
void MainWindow::afterMainWindowShow() {
    if (m_alreadyShown)
        return;

    // If the user passed a filename as a command line argument, try to open it now.
    if (!m_fileToLoadOnStartup.isEmpty()) {
        auto start = std::chrono::system_clock::now();
        loadGraph(m_fileToLoadOnStartup);
        auto end = std::chrono::system_clock::now();
        std::cerr << std::filesystem::path(m_fileToLoadOnStartup.toStdString()).filename().string()
                  << ", " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << std::endl;
    }

    // If a BLAST query filename is present, do the BLAST search now automatically.
    if (!g_settings->blastQueryFilename.isEmpty()) {
        GraphSearchDialog blastSearchDialog(this, g_settings->blastQueryFilename);
        setupBlastQueryComboBox();
    }

    // If the draw option was used and the graph appears to have loaded (i.e. there
    // is at least one node), then draw the graph.
    // FIXME: This does not work as graph loading is asynchronous now. We need to wait until it is loaded
    if (!m_fileToLoadOnStartup.isEmpty() && m_drawGraphAfterLoad && !g_assemblyGraph->m_deBruijnGraphNodes.empty())
        drawGraph();

    // If a csv query filename is present, pull the info automatically.
    if (!g_settings->csvFilename.isEmpty())
        loadCSV(g_settings->csvFilename);

    m_alreadyShown = true;
}

MainWindow::~MainWindow() {
    cleanUp();
    delete m_graphicsViewZoom;
    delete ui;
}


void MainWindow::cleanUp() {
    ui->blastQueryComboBox->clear();
    ui->blastQueryComboBox->addItem("none");

    if (m_blastSearchDialog) {
        m_blastSearchDialog->search()->cleanUp();
        delete m_blastSearchDialog;
        m_blastSearchDialog = nullptr;
    }

    g_assemblyGraph->cleanUp();
    setWindowTitle("Bandage-NG");

    g_memory->userSpecifiedPath = Path();
    g_memory->userSpecifiedPathString = "";
    g_memory->userSpecifiedPathCircular = false;

    ui->csvComboBox->clear();
    ui->csvComboBox->setEnabled(false);
    g_settings->displayNodeCsvDataCol = 0;

    switchColourScheme(RANDOM_COLOURS);
}

void MainWindow::loadCSV(QString fullFileName) {
    QString selectedFilter = "Comma separated value (*.csv)";
    if (fullFileName == "")
        fullFileName = QFileDialog::getOpenFileName(this, "Load CSV", g_memory->rememberedPath,
                                                    "Comma separated value (*.csv)",
                                                    &selectedFilter);

    if (fullFileName == "")
        return; // user clicked on cancel

    QString errormsg;
    try {
        MyProgressDialog progress(this, "Loading CSV...", false);
        progress.setWindowModality(Qt::WindowModal);
        progress.show();

        bool coloursLoaded = false;
        QStringList columns;
        if (g_assemblyGraph->loadCSV(fullFileName, &columns, &errormsg, &coloursLoaded)) {
            ui->csvCheckBox->setChecked(true);
            ui->csvComboBox->setEnabled(true);
            ui->csvComboBox->clear();
            ui->csvComboBox->addItems(columns);
            g_settings->displayNodeCsvDataCol = 0;
            switchColourScheme(coloursLoaded ? CUSTOM_COLOURS : CSV_COLUMN);
        }
    } catch (...) {
        QString errorTitle = "Error loading CSV";
        QString errorMessage = "There was an error when attempting to load:\n"
                               + fullFileName + "\n\n"
                               "Please verify that this file has the correct format.";
        QMessageBox::warning(this, errorTitle, errorMessage);
    }
}


void MainWindow::loadGraph(QString fullFileName) {
    QString selectedFilter = "Any supported graph (*)";
    if (fullFileName.isEmpty())
        fullFileName =
                QFileDialog::getOpenFileName(this, "Load graph", g_memory->rememberedPath,
                                             "Any supported graph (*);;"
                                             "FASTG (*.fastg);;"
                                             "GFA (*.gfa);;"
                                             "Trinity.fasta (*.fasta);;"
                                             "ASQG (*.asqg);;"
                                             "Plain FASTA (*.fasta)",
                                             &selectedFilter);

    if (fullFileName.isEmpty()) //User did hit cancel
        return;

    // We need to convert unique_ptr to shared_ptr in order to get builder shared between future and callback
    std::shared_ptr<io::AssemblyGraphBuilder> builder = io::AssemblyGraphBuilder::get(fullFileName);
    if (!builder) {
        QMessageBox::warning(this,
                             "Graph format not recognised",
                             "Cannot load file. The selected file's format was not recognised as any supported graph type.");
        return;
    }
    builder->treatJumpsAsLinks(g_settings->jumpsAsLinks);

    resetScene();
    cleanUp();
    ui->selectionSearchNodesLineEdit->clear();

    auto *progress = new MyProgressDialog(this, "Loading " + fullFileName, false);
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    auto *watcher = new QFutureWatcher<llvm::Error>;
    connect(watcher, &QFutureWatcher<bool>::finished,
            this,
            [=]() {
                if (auto E = watcher->future().takeResult()) {
                    QString errorTitle = "Error loading graph";
                    QString errorMessage = "There was an error when attempting to load\n"
                                           + fullFileName + ":\n"
                                           + QString::fromStdString(llvm::toString(std::move(E))) + "\n\n"
                                           + "Please verify that this file has the correct format.";
                    QMessageBox::warning(this, errorTitle, errorMessage);
                    resetScene();
                    cleanUp();
                    clearGraphDetails();
                    setUiState(NO_GRAPH_LOADED);
                    return;
                }

                if (builder->hasComplexOverlaps())
                    QMessageBox::warning(this, "Unsupported CIGAR",
                                         "This GFA file contains "
                                         "links with complex CIGAR strings (containing "
                                         "operators other than M).\n\n"
                                         "Bandage does not support edge overlaps that are not "
                                         "perfect, so the behaviour of such edges in this graph "
                                         "is undefined.");

                setUiState(GRAPH_LOADED);
                setWindowTitle("BandageNG - " + fullFileName);

                g_assemblyGraph->determineGraphInfo();
                displayGraphDetails();
                g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
                g_memory->clearGraphSpecificMemory();

                bool customColours = builder->hasCustomColours(),
                      customLabels = builder->hasCustomLabels();

                // If the graph has custom colours, automatically switch the colour scheme to custom colours.
                if (customColours)
                    switchColourScheme(CUSTOM_COLOURS);

                // If the graph doesn't have custom colours, but the colour scheme is on 'Custom', automatically switch it back
                // to the default of 'Random colours'.
                if (!customColours && ui->coloursComboBox->currentIndex() == 6)
                    ui->coloursComboBox->setCurrentIndex(0);

                setupPathSelectionLineEdit(ui->pathSelectionLineEdit);
                setupPathSelectionLineEdit(ui->pathSelectionLineEdit2);
                setupWalkSelectionLineEdit(ui->walkSelectionLineEdit);
                setupWalkSelectionLineEdit(ui->walkSelectionLineEdit2);
    });
    connect(watcher, SIGNAL(finished()), progress, SLOT(deleteLater()));
    connect(watcher, SIGNAL(finished()), watcher, SLOT(deleteLater()));

    auto res = QtConcurrent::run(&io::AssemblyGraphBuilder::build, builder, std::ref(*g_assemblyGraph));
    watcher->setFuture(res);
}

void MainWindow::loadGraphLayout(QString fullFileName) {
    if (fullFileName.isEmpty())
        fullFileName = QFileDialog::getOpenFileName(this, "Load Bandage layout", "",
                                                    "Bandage layout (*.layout)");

    if (fullFileName.isEmpty())
        return; // user clicked on cancel

    GraphLayout layout(*g_assemblyGraph);
    try {
        layout::io::load(fullFileName, layout);
    } catch (std::runtime_error &err) {
        QString errorTitle = "Error loading layout";
        QString errorMessage = "There was an error when attempting to load:\n"
                               + fullFileName + ":\n"
                               + err.what() + "\n\n"
                                 "Please verify that this file has the correct format.";
        QMessageBox::warning(this, errorTitle, errorMessage);
        return;
    }

    layout::apply(*g_assemblyGraph, layout);

    graphLayoutFinished(layout);
}

void MainWindow::loadGraphPaths(QString fullFileName) {
    QString selectedFilter = "GAF paths (*.gaf)";
    if (fullFileName.isEmpty())
        fullFileName = QFileDialog::getOpenFileName(this, "Load graph paths", "",
                                                    "GAF paths (*.gaf);;GFA paths (*.gfa);;SPAligner TSV paths (*.tsv);;SPAdes paths (*.paths)",
                                                    &selectedFilter);

    if (fullFileName.isEmpty())
        return; // user clicked on cancel

    auto ErrorHandler = [&](llvm::Error Err) {
        QString errorTitle = "Error loading graph paths";
        QString errorMessage = "There was an error when attempting to load:\n"
                               + fullFileName + ":\n"
                               + toString(std::move(Err)).c_str() + "\n\n"
                               + "Please verify that this file has the correct format.";
        QMessageBox::warning(this, errorTitle, errorMessage);
    };


    if (selectedFilter == "GFA paths (*.gfa)") {
        if (auto Err = io::loadGFAPaths(*g_assemblyGraph, fullFileName)) {
            ErrorHandler(std::move(Err));
            return;
        }
    } else if (selectedFilter == "SPAligner TSV paths (*.tsv)") {
        if (auto Err = io::loadSPAlignerPaths(*g_assemblyGraph, fullFileName)) {
            ErrorHandler(std::move(Err));
            return;
        }
    } else if (selectedFilter == "GAF paths (*.gaf)") {
        if (auto Err = io::loadGAFPaths(*g_assemblyGraph, fullFileName)) {
            ErrorHandler(std::move(Err));
            return;
        }
    } else {
        if (auto Err = io::loadSPAdesPaths(*g_assemblyGraph, fullFileName)) {
            ErrorHandler(std::move(Err));
            return;
        }
    }

    // FIXME: ugly!
    g_assemblyGraph->determineGraphInfo();
    displayGraphDetails();
    setupPathSelectionLineEdit(ui->pathSelectionLineEdit);
    setupPathSelectionLineEdit(ui->pathSelectionLineEdit2);
}

void MainWindow::loadGraphLinks(QString fullFileName) {
    QString selectedFilter = "Links in tab-separated format (*.tsv)";
    if (fullFileName.isEmpty())
        fullFileName = QFileDialog::getOpenFileName(this, "Load additional links", "",
                                                    "Links in tab-separated format (*.tsv);;GFA links (*.gfa)",
                                                    &selectedFilter);

    if (fullFileName.isEmpty())
        return; // user clicked on cancel

    auto ErrorHandler = [&](llvm::Error Err) {
        QString errorTitle = "Error loading graph links";
        QString errorMessage = "There was an error when attempting to load:\n"
                               + fullFileName + ":\n"
                               + toString(std::move(Err)).c_str() + "\n\n"
                               + "Please verify that this file has the correct format.";
        QMessageBox::warning(this, errorTitle, errorMessage);
    };

    std::vector<DeBruijnEdge*> newEdges;
    if (selectedFilter == "Links in tab-separated format (*.tsv)") {
        if (auto Err = io::loadLinks(*g_assemblyGraph, fullFileName, &newEdges)) {
            ErrorHandler(std::move(Err));
            return;
        }
    } else if (selectedFilter == "GFA links (*.gfa)") {
        if (auto Err = io::loadGFALinks(*g_assemblyGraph, fullFileName, &newEdges)) {
            ErrorHandler(std::move(Err));
            return;
        }
    }

    // FIXME: ugly!
    g_assemblyGraph->determineGraphInfo();
    displayGraphDetails();

    if (m_uiState == GRAPH_LOADED)
        return;

    // Now we need to iterate over new edges and add them to scene
    m_scene->addGraphicsItemsToScene(*g_assemblyGraph, newEdges);

    m_scene->setSceneRectangle();
    zoomToFitScene();
    g_graphicsView->viewport()->update();
    selectionChanged();

    // Move the focus to the view so the user can use keyboard controls to navigate.
    g_graphicsView->setFocus();
}

void MainWindow::displayGraphDetails()
{
    ui->nodeCountLabel->setText(formatIntForDisplay(g_assemblyGraph->m_nodeCount));
    ui->edgeCountLabel->setText(formatIntForDisplay(g_assemblyGraph->m_edgeCount));
    ui->pathCountLabel->setText(formatIntForDisplay(g_assemblyGraph->pathCount()));
    ui->walkCountLabel->setText(formatIntForDisplay(g_assemblyGraph->walkCount()));
    ui->totalLengthLabel->setText(formatIntForDisplay(g_assemblyGraph->m_totalLength));
}

void MainWindow::clearGraphDetails()
{
    ui->nodeCountLabel->setText("0");
    ui->edgeCountLabel->setText("0");
    ui->pathCountLabel->setText("0");
    ui->totalLengthLabel->setText("0");
}

void MainWindow::selectionChanged()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    std::vector<DeBruijnEdge *> selectedEdges = m_scene->getSelectedEdges();

    if (selectedNodes.empty())
    {
        ui->selectedNodesTextEdit->setPlainText("");
        setSelectedNodesWidgetsVisibility(false);
    }

    else //One or more nodes selected
    {
        setSelectedNodesWidgetsVisibility(true);

        int selectedNodeCount;
        QString selectedNodeCountText;
        QString selectedNodeListText;
        QString selectedNodeLengthText;
        QString selectedNodeDepthText;
        QString selectedNodeTagText;

        getSelectedNodeInfo(selectedNodeCount, selectedNodeCountText, selectedNodeListText, selectedNodeLengthText,
                            selectedNodeDepthText, selectedNodeTagText);

        if (selectedNodeCount == 1)
        {
            ui->selectedNodesTitleLabel->setText("Selected node");
            ui->selectedNodesLengthLabel->setText("Length: " + selectedNodeLengthText);
            ui->selectedNodesDepthLabel->setText("Depth: " + selectedNodeDepthText);
            if (selectedNodeTagText.length()) {
                ui->selectedNodesTagLabel->setVisible(true);
                ui->selectedNodesTagLabel->setText("Tags: " + selectedNodeTagText);
            } else
                ui->selectedNodesTagLabel->setVisible(false);
        }
        else
        {
            ui->selectedNodesTitleLabel->setText("Selected nodes (" + selectedNodeCountText + ")");
            ui->selectedNodesLengthLabel->setText("Total length: " + selectedNodeLengthText);
            ui->selectedNodesDepthLabel->setText("Mean depth: " + selectedNodeDepthText);
            ui->selectedNodesTagLabel->setVisible(false);
        }

        ui->selectedNodesTextEdit->setPlainText(selectedNodeListText);
    }


    if (selectedEdges.empty())
    {
        ui->selectedEdgesTextEdit->setPlainText("");
        setSelectedEdgesWidgetsVisibility(false);
    }

    else //One or more edges selected
    {
        setSelectedEdgesWidgetsVisibility(true);
        if (selectedEdges.size() == 1)
            ui->selectedEdgesTitleLabel->setText("Selected edge");
        else
            ui->selectedEdgesTitleLabel->setText("Selected edges (" + formatIntForDisplay(int(selectedEdges.size())) + ")");

        ui->selectedEdgesTextEdit->setPlainText(getSelectedEdgeListText());
    }
}


void MainWindow::getSelectedNodeInfo(int & selectedNodeCount, QString & selectedNodeCountText,
                                     QString & selectedNodeListText, QString & selectedNodeLengthText, QString & selectedNodeDepthText,
                                     QString &selectNodeTagsText)
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();

    selectedNodeCount = int(selectedNodes.size());
    selectedNodeCountText = formatIntForDisplay(selectedNodeCount);

    long long totalLength = 0;

    for (int i = 0; i < selectedNodeCount; ++i)
    {
        QString nodeName = selectedNodes[i]->getName();

        //If we are in single mode, don't include +/i in the node name
        if (!g_settings->doubleMode)
            nodeName.chop(1);

        selectedNodeListText += nodeName;
        if (i != int(selectedNodes.size()) - 1)
            selectedNodeListText += ", ";

        totalLength += selectedNodes[i]->getLength();
    }

    selectedNodeLengthText = formatIntForDisplay(totalLength) + " bp";
    selectedNodeDepthText = formatDepthForDisplay(g_assemblyGraph->getMeanDepth(selectedNodes));

    if (selectedNodeCount == 1) {
        // FIXME: Hack!
        selectedNodeDepthText += " GC: " + formatDoubleForDisplay(100 * selectedNodes[0]->getGC(), 1) + "%";

        auto tags = g_assemblyGraph->m_nodeTags.find(selectedNodes.front());
        if (tags != g_assemblyGraph->m_nodeTags.end()) {
            std::stringstream txt;
            for (const auto &tag : tags->second)
                txt << tag << ' ';
            selectNodeTagsText = txt.str().c_str();
        }
    }
}




QString MainWindow::getSelectedEdgeListText()
{
    std::vector<DeBruijnEdge *> selectedEdges = m_scene->getSelectedEdges();

    std::sort(selectedEdges.begin(), selectedEdges.end(), DeBruijnEdge::compareEdgePointers);

    QString edgeText;
    for (size_t i = 0; i < selectedEdges.size(); ++i) {
        const auto *edge = selectedEdges[i];
        edgeText += edge->getStartingNode()->getName();
        edgeText += " to ";
        edgeText += edge->getEndingNode()->getName();
        int overlap = edge->getOverlap();

        switch (edge->getOverlapType()) {
            case EdgeOverlapType::EXTRA_LINK: {
                edgeText += " (link";
                auto tags = g_assemblyGraph->m_edgeTags.find(edge);
                if (tags != g_assemblyGraph->m_edgeTags.end()) {
                    if (auto wt = gfa::getTag<float>("WT", tags->second))
                        edgeText += QString(", weight: %1").arg(*wt);
                    if (auto wt = gfa::getTag<int64_t>("WT", tags->second))
                        edgeText += QString(", weight: %1").arg(*wt);
                }

                edgeText += ")";
                break;
            }
            case EdgeOverlapType::JUMP:
                edgeText += " (jump link" +
                            (overlap ? QString(" %1bp)").arg(overlap)
                             : ")");
                break;
            default:
                edgeText += QString(" (%1bp)").arg(overlap);
        }

        if (i != selectedEdges.size() - 1)
            edgeText += ", ";
    }

    if (selectedEdges.size() == 1) {
        auto tags = g_assemblyGraph->m_edgeTags.find(selectedEdges.front());
        if (tags != g_assemblyGraph->m_edgeTags.end()) {
            std::stringstream txt;
            for (const auto &tag : tags->second)
                txt << tag << ' ';
            edgeText += ", tags: ";
            edgeText += txt.str().c_str();
        }
    }


    return edgeText;
}



//This function shows/hides UI elements depending on which
//graph scope is currently selected.  It also reorganises
//the widgets in the layout to prevent gaps when widgets
//are hidden.
void MainWindow::graphScopeChanged()
{
    switch (ui->graphScopeComboBox->currentIndex())
    {
    case 0:
        g_settings->graphScope = WHOLE_GRAPH;

        setStartingNodesWidgetVisibility(false);
        setNodeDistanceWidgetVisibility(false);
        setDepthRangeWidgetVisibility(false);
        setPathSelectionWidgetVisibility(false);
        setWalkSelectionWidgetVisibility(false);

        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleInfoText, 1, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleLabel, 1, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleWidget, 1, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphInfoText, 2, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphButton, 2, 1, 1, 2);

        break;

    case 1:
        g_settings->graphScope = AROUND_NODE;

        setStartingNodesWidgetVisibility(true);
        setNodeDistanceWidgetVisibility(true);
        setDepthRangeWidgetVisibility(false);
        setPathSelectionWidgetVisibility(false);
        setWalkSelectionWidgetVisibility(false);

        ui->nodeDistanceInfoText->setToolTip("<html>Nodes will be drawn if they are specified in the above list or are "
                                              "within this many steps of those nodes.<br><br>"
                                              "A value of 0 will result in only the specified nodes being drawn. "
                                              "A large value will result in large sections of the graph around "
                                              "the specified nodes being drawn.</html>");

        ui->graphDrawingGridLayout->addWidget(ui->startingNodesInfoText, 1, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->startingNodesLabel, 1, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->startingNodesLineEdit, 1, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->startingNodesMatchTypeInfoText, 2, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->startingNodesMatchTypeLabel, 2, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->startingNodesMatchTypeWidget, 2, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceInfoText, 3, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceLabel, 3, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceSpinBox, 3, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleInfoText, 4, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleLabel, 4, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleWidget, 4, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphInfoText, 5, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphButton, 5, 1, 1, 2);

        break;

    case 2:
        g_settings->graphScope = AROUND_PATHS;

        setStartingNodesWidgetVisibility(false);
        setNodeDistanceWidgetVisibility(true);
        setDepthRangeWidgetVisibility(false);
        setPathSelectionWidgetVisibility(true);
        setWalkSelectionWidgetVisibility(false);

        ui->nodeDistanceInfoText->setToolTip("<html>Path nodes will be drawn if they are specified in the above list or are "
                                              "within this many steps of those nodes.<br><br>"
                                              "A value of 0 will result in only the specified nodes being drawn. "
                                              "A large value will result in large sections of the graph around "
                                              "the specified nodes being drawn.</html>");

        ui->graphDrawingGridLayout->addWidget(ui->pathSelectionInfoText, 1, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->pathSelectionLabel,    1, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->pathSelectionLineEdit,  1, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceInfoText, 2, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceLabel, 2, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceSpinBox, 2, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleInfoText, 3, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleLabel, 3, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleWidget, 3, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphInfoText, 4, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphButton, 4, 1, 1, 2);

        break;

    case 3:
        g_settings->graphScope = AROUND_WALKS;

        setStartingNodesWidgetVisibility(false);
        setNodeDistanceWidgetVisibility(true);
        setDepthRangeWidgetVisibility(false);
        setPathSelectionWidgetVisibility(false);
        setWalkSelectionWidgetVisibility(true);

        ui->nodeDistanceInfoText->setToolTip("<html>Walk nodes will be drawn if they are specified in the above list or are "
                                              "within this many steps of those nodes.<br><br>"
                                              "A value of 0 will result in only the specified nodes being drawn. "
                                              "A large value will result in large sections of the graph around "
                                              "the specified nodes being drawn.</html>");

        ui->graphDrawingGridLayout->addWidget(ui->walkSelectionInfoText, 1, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->walkSelectionLabel,    1, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->walkSelectionLineEdit,  1, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceInfoText, 2, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceLabel, 2, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceSpinBox, 2, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleInfoText, 3, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleLabel, 3, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleWidget, 3, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphInfoText, 4, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphButton, 4, 1, 1, 2);

        break;

    case 4:
        g_settings->graphScope = AROUND_BLAST_HITS;

        setStartingNodesWidgetVisibility(false);
        setNodeDistanceWidgetVisibility(true);
        setDepthRangeWidgetVisibility(false);
        setPathSelectionWidgetVisibility(false);
        setWalkSelectionWidgetVisibility(false);

        ui->nodeDistanceInfoText->setToolTip("<html>Nodes will be drawn if they contain a BLAST hit or are within this "
                                              "many steps of nodes with a BLAST hit.<br><br>"
                                              "A value of 0 will result in only nodes with BLAST hits being drawn. "
                                              "A large value will result in large sections of the graph around "
                                              "nodes with BLAST hits being drawn.</html>");

        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceInfoText, 1, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceLabel, 1, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeDistanceSpinBox, 1, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleInfoText, 2, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleLabel, 2, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleWidget, 2, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphInfoText, 3, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphButton, 3, 1, 1, 2);

        break;

    case 5:
        g_settings->graphScope = DEPTH_RANGE;

        setStartingNodesWidgetVisibility(false);
        setNodeDistanceWidgetVisibility(false);
        setDepthRangeWidgetVisibility(true);
        setPathSelectionWidgetVisibility(false);
        setWalkSelectionWidgetVisibility(false);

        ui->graphDrawingGridLayout->addWidget(ui->minDepthInfoText, 1, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->minDepthLabel, 1, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->minDepthSpinBox, 1, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->maxDepthInfoText, 2, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->maxDepthLabel, 2, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->maxDepthSpinBox, 2, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleInfoText, 3, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleLabel, 3, 1, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->nodeStyleWidget, 3, 2, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphInfoText, 4, 0, 1, 1);
        ui->graphDrawingGridLayout->addWidget(ui->drawGraphButton, 4, 1, 1, 2);

        break;
    }
}


void MainWindow::setStartingNodesWidgetVisibility(bool visible)
{
    ui->startingNodesInfoText->setVisible(visible);
    ui->startingNodesLabel->setVisible(visible);
    ui->startingNodesLineEdit->setVisible(visible);
    ui->startingNodesMatchTypeInfoText->setVisible(visible);
    ui->startingNodesMatchTypeLabel->setVisible(visible);
    ui->startingNodesExactMatchRadioButton->setVisible(visible);
    ui->startingNodesPartialMatchRadioButton->setVisible(visible);
}
void MainWindow::setNodeDistanceWidgetVisibility(bool visible)
{
    ui->nodeDistanceInfoText->setVisible(visible);
    ui->nodeDistanceLabel->setVisible(visible);
    ui->nodeDistanceSpinBox->setVisible(visible);
}
void MainWindow::setDepthRangeWidgetVisibility(bool visible)
{
    ui->minDepthInfoText->setVisible(visible);
    ui->minDepthLabel->setVisible(visible);
    ui->minDepthSpinBox->setVisible(visible);
    ui->maxDepthInfoText->setVisible(visible);
    ui->maxDepthLabel->setVisible(visible);
    ui->maxDepthSpinBox->setVisible(visible);
}
void MainWindow::setPathSelectionWidgetVisibility(bool visible)
{
    ui->pathSelectionInfoText->setVisible(visible);
    ui->pathSelectionLabel->setVisible(visible);
    ui->pathSelectionLineEdit->setVisible(visible);
}

void MainWindow::setWalkSelectionWidgetVisibility(bool visible)
{
    ui->walkSelectionInfoText->setVisible(visible);
    ui->walkSelectionLabel->setVisible(visible);
    ui->walkSelectionLineEdit->setVisible(visible);
}

void MainWindow::setupPathSelectionLineEdit(QLineEdit *lineEdit) {
    lineEdit->clear();

    if (g_assemblyGraph->m_deBruijnGraphPaths.empty())
        return;

    auto *matchedPaths = new QStringListModel(this);
    auto *completer = new QCompleter(matchedPaths);
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    lineEdit->setCompleter(completer);

    connect(lineEdit, &QLineEdit::textEdited,
            [matchedPaths](const QString &text) {
                QStringList res;

                auto prefix_range = g_assemblyGraph->m_deBruijnGraphPaths.equal_prefix_range(text.toStdString());
                size_t sz = std::distance(prefix_range.first, prefix_range.second);
                if (sz > 1000) {
                    res << "Too many paths to show";
                } else {
                    for (auto it = prefix_range.first; it != prefix_range.second; ++it)
                        res.push_back(it.key().c_str());
                }

                if (res.empty())
                    res << "No paths matching prefix";

                res.sort();

                matchedPaths->setStringList(res);
            });

    lineEdit->setEnabled(true);
}

// FIXME: dedup with path search
void MainWindow::setupWalkSelectionLineEdit(QLineEdit *lineEdit) {
    lineEdit->clear();

    if (g_assemblyGraph->m_deBruijnGraphWalks.empty())
        return;

    auto *matchedPaths = new QStringListModel(this);
    auto *completer = new QCompleter(matchedPaths);
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    lineEdit->setCompleter(completer);

    connect(lineEdit, &QLineEdit::textEdited,
            [matchedPaths](const QString &text) {
                QStringList res;

                auto prefix_range = g_assemblyGraph->m_deBruijnGraphWalks.equal_prefix_range(text.toStdString());
                size_t sz = std::distance(prefix_range.first, prefix_range.second);
                if (sz > 1000) {
                    res << "Too many walks to show";
                } else {
                    for (auto it = prefix_range.first; it != prefix_range.second; ++it)
                        res.push_back(it.key().c_str());
                }

                if (res.empty())
                    res << "No walks matching prefix";

                res.sort();

                matchedPaths->setStringList(res);
            });

    lineEdit->setEnabled(true);
}


void MainWindow::drawGraph() {
    QString errorTitle;
    QString errorMessage;
    g_settings->doubleMode = ui->doubleNodesRadioButton->isChecked();

    auto scope =
            graph::scope(g_settings->graphScope,
                         ui->startingNodesLineEdit->text(),
                         ui->minDepthSpinBox->value(), ui->maxDepthSpinBox->value(),
                         m_blastSearchDialog ? &m_blastSearchDialog->search()->queries() : nullptr,
                         ui->blastQueryComboBox->currentText(),
                         g_settings->graphScope == GraphScope::AROUND_PATHS ?
                         ui->pathSelectionLineEdit->displayText() : ui->walkSelectionLineEdit->displayText(),
                         ui->nodeDistanceSpinBox->value());

    auto startingNodes = graph::getStartingNodes(&errorTitle, &errorMessage,
                                                 *g_assemblyGraph, scope);

    if (!errorMessage.isEmpty()) {
        QMessageBox::information(this, errorTitle, errorMessage);
        return;
    }

    resetScene();
    g_assemblyGraph->resetNodes();
    g_assemblyGraph->markNodesToDraw(scope, startingNodes);
    layoutGraph();
}


void MainWindow::graphLayoutFinished(const GraphLayout &layout) {
    size_t items = m_scene->addGraphicsItemsToScene(*g_assemblyGraph, layout);
    // Looks like Qt has problems where there are many overlapping items with default BSP
    // height, so override it so something saner
    if (items > 10000)
        m_scene->setBspTreeDepth(10);
    m_scene->setSceneRectangle();
    zoomToFitScene();

    double averageNodeWidth = g_settings->averageNodeWidth / pow(g_absoluteZoom, 0.75);
    ui->nodeWidthSpinBox->setValue(averageNodeWidth);
    g_assemblyGraph->recalculateAllNodeWidths(averageNodeWidth,
                                              g_settings->depthPower, g_settings->depthEffectOnWidth);
    g_graphicsView->viewport()->update();

    selectionChanged();

    setUiState(GRAPH_DRAWN);

    //Move the focus to the view so the user can use keyboard controls to navigate.
    g_graphicsView->setFocus();
}


void MainWindow::resetScene() {
    m_scene->blockSignals(true);

    g_assemblyGraph->resetEdges();

    g_graphicsView->setScene(nullptr);
    delete m_scene;
    m_scene = new BandageGraphicsScene(this);

    g_graphicsView->setScene(m_scene);
    connect(m_scene, SIGNAL(selectionChanged()), this, SLOT(selectionChanged()));
    selectionChanged();

    g_graphicsView->undoRotation();
}


std::vector<DeBruijnNode *> MainWindow::getNodesFromLineEdit(QLineEdit * lineEdit, bool exactMatch, std::vector<QString> * nodesNotInGraph)
{
    return g_assemblyGraph->getNodesFromStringList(lineEdit->text(), exactMatch, nodesNotInGraph);
}




void MainWindow::layoutGraph()
{
    //The actual layout is done in a different thread so the UI will stay responsive.
    auto *progress = new MyProgressDialog(this, "Laying out graph...", true, "Cancel layout", "Cancelling layout...",
                                          "Clicking this button will halt the graph layout and display "
                                          "the graph in its current, incomplete state.<br><br>"
                                          "Layout can take a long time for very large graphs.  There are "
                                          "three strategies to reduce the amount of time required:<ul>"
                                          "<li>Change the scope of the graph from 'Entire graph' to either "
                                          "'Around nodes' or 'Around BLAST hits'.  This will reduce the "
                                          "number of nodes that are drawn to the screen.</li>"
                                          "<li>Increase the 'Base pairs per segment' setting.  This will "
                                          "result in shorter contigs which take less time to lay out.</li>"
                                          "<li>Reduce the 'Graph layout iterations' setting.</li></ul>");
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    double aspectRatio = double(g_graphicsView->width()) / g_graphicsView->height();
    auto *graphLayoutWorker = new GraphLayoutWorker(g_settings->graphLayoutQuality,
                                                    g_settings->linearLayout,
                                                    g_settings->componentSeparation, aspectRatio);

    connect(progress, SIGNAL(halt()), graphLayoutWorker, SLOT(cancelLayout()));

    auto *watcher = new QFutureWatcher<GraphLayout>;

    connect(watcher, &QFutureWatcher<GraphLayout>::finished,
            this, [=]() { this->graphLayoutFinished(watcher->future().result()); });
    connect(watcher, SIGNAL(finished()), graphLayoutWorker, SLOT(deleteLater()));
    connect(watcher, SIGNAL(finished()), progress, SLOT(deleteLater()));
    connect(watcher, SIGNAL(finished()), watcher, SLOT(deleteLater()));

    auto res = QtConcurrent::run(&GraphLayoutWorker::layoutGraph, graphLayoutWorker, std::cref(*g_assemblyGraph));
    watcher->setFuture(res);
}




void MainWindow::zoomSpinBoxChanged()
{
    double newValue = ui->zoomSpinBox->value();
    double zoomFactor = newValue / m_previousZoomSpinBoxValue;
    setZoomSpinBoxStep();

    m_graphicsViewZoom->gentleZoom(zoomFactor, SPIN_BOX);

    m_previousZoomSpinBoxValue = newValue;
}

void MainWindow::setZoomSpinBoxStep()
{
    double newSingleStep = ui->zoomSpinBox->value() * (g_settings->zoomFactor - 1.0) * 100.0;

    //Round up to nearest 0.1
    newSingleStep = int((newSingleStep + 0.1) * 10.0) / 10.0;

    ui->zoomSpinBox->setSingleStep(newSingleStep);
}


void MainWindow::zoomedWithMouseWheel()
{
    ui->zoomSpinBox->blockSignals(true);
    double newSpinBoxValue = g_absoluteZoom * 100.0;
    ui->zoomSpinBox->setValue(newSpinBoxValue);
    setZoomSpinBoxStep();
    m_previousZoomSpinBoxValue = newSpinBoxValue;
    ui->zoomSpinBox->blockSignals(false);
}



void MainWindow::zoomToFitScene()
{
    zoomToFitRect(m_scene->sceneRect());
}


void MainWindow::zoomToFitRect(QRectF rect)
{
    double startingZoom = g_graphicsView->transform().m11();
    g_graphicsView->fitInView(rect, Qt::KeepAspectRatio);
    double endingZoom = g_graphicsView->transform().m11();
    double zoomFactor = endingZoom / startingZoom;
    g_absoluteZoom *= zoomFactor;
    double newSpinBoxValue = ui->zoomSpinBox->value() * zoomFactor;

    double minZoom = std::max(g_settings->minZoom, g_settings->minZoomOnGraphDraw);

    //Limit the zoom to the minimum and maximum
    if (g_absoluteZoom < minZoom)
    {
        double newZoomFactor = minZoom / g_absoluteZoom;
        m_graphicsViewZoom->gentleZoom(newZoomFactor, SPIN_BOX);
        g_absoluteZoom = minZoom;
        newSpinBoxValue = g_absoluteZoom * 100.0;
    } else if (g_absoluteZoom > g_settings->maxAutomaticZoom) {
        double newZoomFactor = g_settings->maxAutomaticZoom / g_absoluteZoom;
        m_graphicsViewZoom->gentleZoom(newZoomFactor, SPIN_BOX);
        g_absoluteZoom = g_settings->maxAutomaticZoom;
        newSpinBoxValue = g_absoluteZoom * 100.0;
    }

    ui->zoomSpinBox->blockSignals(true);
    ui->zoomSpinBox->setValue(newSpinBoxValue);
    m_previousZoomSpinBoxValue = newSpinBoxValue;
    ui->zoomSpinBox->blockSignals(false);
}



//This function copies selected sequences to clipboard, if any sequences are
//selected.  If there aren't, then it will prompt the user.
void MainWindow::copySelectedSequencesToClipboardActionTriggered()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty()) {
        QMessageBox::information(this, "Copy sequences to clipboard",
                                 "No nodes are selected.\n\n"
                                 "You must first select nodes in the graph before you can copy their sequences to the clipboard.");
        return;
    }

    copySelectedSequencesToClipboard();
}


//This function copies selected sequences to clipboard, if any sequences are
//selected.
void MainWindow::copySelectedSequencesToClipboard() {
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
        return;

    QClipboard * clipboard = QApplication::clipboard();
    QString clipboardText;

    for (size_t i = 0; i < selectedNodes.size(); ++i)
    {
        clipboardText += utils::sequenceToQByteArray(selectedNodes[i]->getSequence());
        if (i != selectedNodes.size() - 1)
            clipboardText += "\n";
    }

    clipboard->setText(clipboardText);
}


//This function saves selected sequences to file, with a save file prompt, if
//any sequences are selected.  If there aren't, then it will prompt the user.
void MainWindow::saveSelectedSequencesToFileActionTriggered()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())  {
        QMessageBox::information(this, "Save sequences to FASTA", "No nodes are selected.\n\n"
                                 "You must first select nodes in the graph before you can save their sequences to a FASTA file.");
        return;
    }

    saveSelectedSequencesToFile();
}


//This function saves selected sequences to file, with a save file prompt, if
//any sequences are selected.
void MainWindow::saveSelectedSequencesToFile()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
        return;

    QString defaultFileNameAndPath = g_memory->rememberedPath + "/selected_sequences.fasta";

    QString fullFileName = QFileDialog::getSaveFileName(this, "Save node sequences", defaultFileNameAndPath, "FASTA (*.fasta)");

    if (fullFileName.isEmpty()) //User did hit cancel
        return;

    QFile file(fullFileName);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&file);

    for (auto & selectedNode : selectedNodes)
        out << selectedNode->getFasta(true);

    g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
}

void MainWindow::copySelectedPathToClipboard()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
    {
        QMessageBox::information(this, "Copy path sequence to clipboard", "No nodes are selected.\n\n"
                                                                          "You must first select nodes in the graph which define a unambiguous "
                                                                          "path before you can copy their path sequence to the clipboard.");
        return;
    }

    Path nodePath = Path::makeFromUnorderedNodes(selectedNodes, g_settings->doubleMode);
    if (nodePath.isEmpty())
    {
        QMessageBox::information(this, "Copy path sequence to clipboard", "Invalid path.\n\n"
                                                                          "To use copy a path sequence to the clipboard, the nodes must follow "
                                                                          "an unambiguous path through the graph.\n\n"
                                                                          "Complex paths can be defined using the '" + ui->actionSpecify_exact_path_for_copy_save->text() +
                                                                          "' tool.");
        return;
    }

    QClipboard * clipboard = QApplication::clipboard();
    clipboard->setText(nodePath.getPathSequence());
}



void MainWindow::saveSelectedPathToFile()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
    {
        QMessageBox::information(this, "Save path sequence to FASTA", "No nodes are selected.\n\n"
                                                                      "You must first select nodes in the graph which define a unambiguous "
                                                                      "path before you can save their path sequence to a FASTA file.");
        return;
    }
    Path nodePath = Path::makeFromUnorderedNodes(selectedNodes, g_settings->doubleMode);
    if (nodePath.isEmpty())
    {
        QMessageBox::information(this, "Save path sequence to FASTA", "Invalid path.\n\n"
                                                                      "To use copy a path sequence to the clipboard, the nodes must follow "
                                                                      "an unambiguous path through the graph.\n\n"
                                                                      "Complex paths can be defined using the '" + ui->actionSpecify_exact_path_for_copy_save->text() +
                                                                      "' tool.");
        return;
    }

    QString defaultFileNameAndPath = g_memory->rememberedPath + "/path_sequence.fasta";

    QString fullFileName = QFileDialog::getSaveFileName(this, "Save path sequence", defaultFileNameAndPath, "FASTA (*.fasta)");

    if (fullFileName != "") //User did not hit cancel
    {
        QFile file(fullFileName);
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream out(&file);
        out << nodePath.getFasta();
        g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
    }
}


void MainWindow::resetAllNodeColours() {
    for (auto &entry : g_assemblyGraph->m_deBruijnGraphNodes) {
        auto *graphicsItemNode = entry->getGraphicsItemNode();
        if (!graphicsItemNode)
            continue;

        graphicsItemNode->setNodeColour(g_settings->nodeColorer->get(graphicsItemNode));
    }

    g_graphicsView->viewport()->update();
}

void MainWindow::switchTagValue() {
    NodeColorScheme scheme = g_settings->nodeColorer->scheme();
    if (scheme == TAG_VALUE) {
        auto *colorer = dynamic_cast<TagValueNodeColorer *>(&*g_settings->nodeColorer);
        if (ui->tagsComboBox->currentIndex() != -1)
            colorer->setTagName(ui->tagsComboBox->currentText().toStdString());
    } else if (scheme == CSV_COLUMN) {
        auto *colorer = dynamic_cast<CSVNodeColorer *>(&*g_settings->nodeColorer);
        if (ui->tagsComboBox->currentIndex() != -1)
            colorer->setColumnIdx(ui->tagsComboBox->currentIndex());
    }

    resetAllNodeColours();
}

void MainWindow::switchColourScheme(int idx) {
    if (idx != -1) {
        if (ui->coloursComboBox->currentIndex() != idx)
            ui->coloursComboBox->setCurrentIndex(idx);
    }

    NodeColorScheme scheme = (NodeColorScheme)ui->coloursComboBox->currentIndex();
    g_settings->initializeColorer(scheme);
    ui->contiguityButton->setVisible(scheme == CONTIGUITY_COLOUR);
    ui->contiguityInfoText->setVisible(scheme == CONTIGUITY_COLOUR);

    if (scheme == TAG_VALUE) {
        ui->tagsComboBox->clear();
        auto *colorer = dynamic_cast<TagValueNodeColorer*>(&*g_settings->nodeColorer);
        auto tagNames = colorer->tagNames();
        for (const auto &tag : tagNames)
            ui->tagsComboBox->addItem(tag.c_str());
        if (!tagNames.empty())
            colorer->setTagName(tagNames.front());
        ui->tagsComboBox->setVisible(true);
    } else if (scheme == CSV_COLUMN) {
        ui->tagsComboBox->clear();
        auto *colorer = dynamic_cast<CSVNodeColorer*>(&*g_settings->nodeColorer);
        ui->tagsComboBox->addItems(g_assemblyGraph->m_csvHeaders);
        if (!g_assemblyGraph->m_csvHeaders.empty())
            colorer->setColumnIdx(0);
        ui->tagsComboBox->setVisible(true);
    } else {
        ui->tagsComboBox->setVisible(false);
    }

    resetAllNodeColours();
}



void MainWindow::determineContiguityFromSelectedNode() {
    auto *colorer = dynamic_cast<ContiguityNodeColorer*>(&*g_settings->nodeColorer);
    colorer->reset();

    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty()) {
        QMessageBox::information(this, "No nodes selected", "Please select one or more nodes for which "
                                                            "contiguity is to be determined.");
        return;
    }

    MyProgressDialog progress(this, "Determining contiguity...", false);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();

    for (auto *selectedNode : selectedNodes)
        colorer->determineContiguity(selectedNode);

    resetAllNodeColours();
}


QString MainWindow::getDefaultImageFileName()
{
    QString fileNameAndPath = g_memory->rememberedPath + "/graph";

    if (m_imageFilter == "PNG (*.png)")
        fileNameAndPath += ".png";
    else if (m_imageFilter == "JPEG (*.jpg)")
        fileNameAndPath += ".jpg";
    else if (m_imageFilter == "SVG (*.svg)")
        fileNameAndPath += ".svg";
    else
        fileNameAndPath += ".png";

    return fileNameAndPath;
}


void MainWindow::saveImageCurrentView()
{
    if (!checkForImageSave())
        return;

    QString defaultFileNameAndPath = getDefaultImageFileName();

    QString selectedFilter = m_imageFilter;
    QString fullFileName = QFileDialog::getSaveFileName(this, "Save graph image (current view)",
                                                        defaultFileNameAndPath,
                                                        "PNG (*.png);;JPEG (*.jpg);;SVG (*.svg)",
                                                        &selectedFilter);

    bool pixelImage = true;
    if (selectedFilter == "PNG (*.png)" || selectedFilter == "JPEG (*.jpg)")
        pixelImage = true;
    else if (selectedFilter == "SVG (*.svg)")
        pixelImage = false;

    if (fullFileName != "") //User did not hit cancel
    {
        m_imageFilter = selectedFilter;

        QPainter painter;
        if (pixelImage)
        {
            QImage image(g_graphicsView->viewport()->rect().size(), QImage::Format_ARGB32);
            image.fill(Qt::white);
            painter.begin(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            g_graphicsView->render(&painter);
            image.save(fullFileName);
            g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
            painter.end();
        }
        else //SVG
        {
            QSvgGenerator generator;
            generator.setFileName(fullFileName);
            QSize size = g_graphicsView->viewport()->rect().size();
            generator.setSize(size);
            generator.setViewBox(QRect(0, 0, size.width(), size.height()));
            painter.begin(&generator);
            painter.fillRect(0, 0, size.width(), size.height(), Qt::white);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            g_graphicsView->render(&painter);
            painter.end();
        }
    }
}

void MainWindow::saveImageEntireScene()
{
    if (!checkForImageSave())
        return;

    QString defaultFileNameAndPath = getDefaultImageFileName();

    QString selectedFilter = m_imageFilter;
    QString fullFileName = QFileDialog::getSaveFileName(this,
                                                        "Save graph image (entire scene)",
                                                        defaultFileNameAndPath,
                                                        "PNG (*.png);;JPEG (*.jpg);;SVG (*.svg)",
                                                        &selectedFilter);

    bool pixelImage = true;
    if (selectedFilter == "PNG (*.png)" || selectedFilter == "JPEG (*.jpg)")
        pixelImage = true;
    else if (selectedFilter == "SVG (*.svg)")
        pixelImage = false;

    if (fullFileName != "") //User did not hit cancel
    {
        //The positionTextNodeCentre setting must be used for the entire scene
        //or else only the labels in the current viewport will be shown.
        bool positionTextNodeCentreSettingBefore = g_settings->positionTextNodeCentre;
        g_settings->positionTextNodeCentre = true;

        //Temporarily undo any rotation so labels appear upright.
        double rotationBefore = g_graphicsView->getRotation();
        g_graphicsView->undoRotation();

        m_imageFilter = selectedFilter;

        QPainter painter;
        if (pixelImage)
        {
            QSize imageSize = g_absoluteZoom * m_scene->sceneRect().size().toSize();

            if (imageSize.width() > 32767 || imageSize.height() > 32767)
            {
                QString error = "Images can not be taller or wider than 32767 pixels, but at the "
                                "current zoom level, the image to be saved would be ";
                error += QString::number(imageSize.width()) + "x" + QString::number(imageSize.height()) + " pixels.\n\n";
                error += "Please reduce the zoom level before saving the entire scene to image or use the SVG format.";

                QMessageBox::information(this, "Image too large", error);
                return;
            }

            if (imageSize.width() * imageSize.height() > 50000000) //50 megapixels is used as an arbitrary large image cutoff
            {
                QString warning = "At the current zoom level, the image will be ";
                warning += QString::number(imageSize.width()) + "x" + QString::number(imageSize.height()) + " pixels. ";
                warning += "An image of this large size may take significant time and space to save.\n\n"
                           "The image size can be reduced by decreasing the zoom level or using the SVG format.\n\n"
                           "Do you want to continue saving the image?";
                QMessageBox::StandardButton response = QMessageBox::question(this, "Large image", warning);
                if (response == QMessageBox::No || response == QMessageBox::Cancel)
                    return;
            }

            QImage image(imageSize, QImage::Format_ARGB32);
            image.fill(Qt::white);
            painter.begin(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            m_scene->setSceneRectangle();
            m_scene->render(&painter);
            image.save(fullFileName);
            g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
            painter.end();
        }
        else //SVG
        {
            QSvgGenerator generator;
            generator.setFileName(fullFileName);
            QSize size = g_absoluteZoom * m_scene->sceneRect().size().toSize();
            generator.setSize(size);
            generator.setViewBox(QRect(0, 0, size.width(), size.height()));
            painter.begin(&generator);
            painter.fillRect(0, 0, size.width(), size.height(), Qt::white);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            m_scene->setSceneRectangle();
            m_scene->render(&painter);
            painter.end();
        }

        g_settings->positionTextNodeCentre = positionTextNodeCentreSettingBefore;
        g_graphicsView->setRotation(rotationBefore);
    }
}



//This function makes sure that a graph is loaded and drawn so that an image can be saved.
//It returns true if everything is fine.  If things aren't ready, it displays a message
//to the user and returns false.
bool MainWindow::checkForImageSave()
{
    if (m_uiState == NO_GRAPH_LOADED)
    {
        QMessageBox::information(this, "No image to save", "You must first load and then draw a graph before you can save an image to file.");
        return false;
    }
    if (m_uiState == GRAPH_LOADED)
    {
        QMessageBox::information(this, "No image to save", "You must first draw the graph before you can save an image to file.");
        return false;
    }
    return true;
}


void MainWindow::setTextDisplaySettings()
{
    g_settings->displayNodeCustomLabels = ui->nodeCustomLabelsCheckBox->isChecked();
    g_settings->displayNodeNames = ui->nodeNamesCheckBox->isChecked();
    g_settings->displayNodeLengths = ui->nodeLengthsCheckBox->isChecked();
    g_settings->displayNodeDepth = ui->nodeDepthCheckBox->isChecked();
    g_settings->displayNodeCsvData = ui->csvCheckBox->isChecked();
    g_settings->displayNodeCsvDataCol = ui->csvComboBox->currentIndex();
    g_settings->textOutline = ui->textOutlineCheckBox->isChecked();

    g_graphicsView->viewport()->update();
}


void MainWindow::fontButtonPressed()
{
    bool ok;
    g_settings->labelFont = QFontDialog::getFont(&ok, g_settings->labelFont, this);
    if (ok)
        g_graphicsView->viewport()->update();
}



void MainWindow::setNodeCustomColour() {
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
        return;

    QString dialogTitle = "Select custom colour for selected node";
    if (selectedNodes.size() > 1)
        dialogTitle += "s";

    QColor newColour = QColorDialog::getColor(g_assemblyGraph->getCustomColourForDisplay(selectedNodes[0]), this, dialogTitle);
    if (!newColour.isValid())
        return;

    // If we are in single mode, apply the custom colour to both nodes in
    // each complementary pair.
    if (!g_settings->doubleMode)
        selectedNodes = addComplementaryNodes(selectedNodes);

    // If the colouring scheme is not currently custom, change it to custom now
    g_settings->initializeColorer(CUSTOM_COLOURS);
    ui->coloursComboBox->setCurrentIndex(g_settings->nodeColorer->scheme());

    for (auto & selectedNode : selectedNodes) {
        g_assemblyGraph->setCustomColour(selectedNode, newColour);
        if (selectedNode->getGraphicsItemNode() != nullptr)
            selectedNode->getGraphicsItemNode()->setNodeColour(newColour);
    }

    g_graphicsView->viewport()->update();
}

void MainWindow::setNodeCustomLabel()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
        return;

    QString dialogMessage = "Type a custom label for selected node";
    if (selectedNodes.size() > 1)
        dialogMessage += "s";
    dialogMessage += ":";

    bool ok;
    QString newLabel = QInputDialog::getText(this, "Custom label", dialogMessage, QLineEdit::Normal,
                                             g_assemblyGraph->getCustomLabel(selectedNodes[0]), &ok);

    if (ok)
    {
        //If the custom label option isn't currently on, turn it on now.
        ui->nodeCustomLabelsCheckBox->setChecked(true);

        for (auto & selectedNode : selectedNodes)
            g_assemblyGraph->setCustomLabel(selectedNode, newLabel);
    }
}


//Takes a vector of nodes and returns a vector of the same nodes, along with
//their complements.  Does not check for duplicates.
std::vector<DeBruijnNode *> MainWindow::addComplementaryNodes(std::vector<DeBruijnNode *> nodes)
{
    std::vector<DeBruijnNode *> complementaryNodes;
    for (auto & node : nodes)
        complementaryNodes.push_back(node->getReverseComplement());
    nodes.insert(nodes.end(), complementaryNodes.begin(), complementaryNodes.end());
    return nodes;
}


void MainWindow::openSettingsDialog() {
    SettingsDialog settingsDialog(this);
    settingsDialog.setWidgetsFromSettings();

    if (!settingsDialog.exec()) //The user clicked OK
        return;

    settingsDialog.setSettingsFromWidgets();

    g_assemblyGraph->recalculateAllNodeWidths(ui->nodeWidthSpinBox->value(),
                                              g_settings->depthPower, g_settings->depthEffectOnWidth);
    g_graphicsView->setAntialiasing(g_settings->antialiasing);
    g_settings->nodeColorer->reset();

    resetAllNodeColours();
}

void MainWindow::doSelectNodes(const std::vector<DeBruijnNode *> &nodesToSelect,
                               const std::vector<QString> &nodesNotInGraph,
                               bool recolor) {
    m_scene->blockSignals(true);
    m_scene->clearSelection();

    //Select each node that actually has a GraphicsItemNode, and build a bounding
    //rectangle so the viewport can focus on the selected node.
    std::vector<QString> nodesNotFound;
    int foundNodes = 0;
    QColor color1, color2;
    for (size_t i = 0; i < nodesToSelect.size(); ++i) {
        GraphicsItemNode * graphicsItemNode = nodesToSelect[i]->getGraphicsItemNode();
        GraphicsItemNode * rcgraphicsItemNode = nodesToSelect[i]->getReverseComplement()->getGraphicsItemNode();

        // If the GraphicsItemNode isn't found, try the reverse complement.  This
        // is only done for single node mode.
        if (graphicsItemNode == nullptr && !g_settings->doubleMode)
            graphicsItemNode = rcgraphicsItemNode;

        if (graphicsItemNode != nullptr) {
            if (recolor) {
                if (i == 0) {
                    color1 = graphicsItemNode->m_colour;
                    if (g_settings->doubleMode)
                        color2 = rcgraphicsItemNode->m_colour;
                } else {
                    graphicsItemNode->m_colour = color1;
                    if (g_settings->doubleMode)
                        rcgraphicsItemNode->m_colour = color2;
                }

            }

            graphicsItemNode->setSelected(true);
            ++foundNodes;
        } else
            nodesNotFound.push_back(nodesToSelect[i]->getName());
    }

    if (foundNodes > 0)
        zoomToSelection();

    if (!nodesNotInGraph.empty() || !nodesNotFound.empty()) {
        QString errorMessage;
        if (!nodesNotInGraph.empty())
            errorMessage += g_assemblyGraph->generateNodesNotFoundErrorMessage(nodesNotInGraph,
                                                                               ui->selectionSearchNodesExactMatchRadioButton->isChecked());
        if (!nodesNotFound.empty()) {
            if (errorMessage.length() > 0)
                errorMessage += "\n";
            errorMessage += "The following nodes are in the graph but not currently displayed:\n";
            for (size_t i = 0; i < nodesNotFound.size(); ++i) {
                errorMessage += nodesNotFound[i];
                if (i != nodesNotFound.size() - 1)
                    errorMessage += ", ";
            }
            errorMessage += "\n\nRedraw the graph with an increased scope to see these nodes.\n";
        }
        QMessageBox::information(this, "Nodes not found", errorMessage);
    }

    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
    selectionChanged();
}

void MainWindow::selectUserSpecifiedNodes()
{
    if (g_assemblyGraph->checkIfStringHasNodes(ui->selectionSearchNodesLineEdit->text()))
    {
        QMessageBox::information(this, "No starting nodes",
                                 "Please enter at least one node when drawing the graph using the 'Around node(s)' scope. "
                                 "Separate multiple nodes with commas.");
        return;
    }

    if (ui->selectionSearchNodesLineEdit->text().length() == 0)
    {
        QMessageBox::information(this, "No nodes given", "Please enter the numbers of the nodes to find, separated by commas.");
        return;
    }

    std::vector<QString> nodesNotInGraph;
    std::vector<DeBruijnNode *> nodesToSelect = getNodesFromLineEdit(ui->selectionSearchNodesLineEdit,
                                                                     ui->selectionSearchNodesExactMatchRadioButton->isChecked(),
                                                                     &nodesNotInGraph);

    doSelectNodes(nodesToSelect, nodesNotInGraph);
}

void MainWindow::selectPathNodes() {
    std::vector<QString> nodesNotInGraph;
    std::vector<DeBruijnNode *> nodesToSelect;

    QString pathName = ui->pathSelectionLineEdit2->displayText();
    auto pathIt = g_assemblyGraph->m_deBruijnGraphPaths.find(pathName.toStdString());
    if (pathIt == g_assemblyGraph->m_deBruijnGraphPaths.end()) {
        QMessageBox::information(this, "Path not found", "Path named \"" + pathName + "\" is not found. Maybe you wanted to select nodes instead?");
        return;
    }

    const Path &p = *pathIt;

    QString posText = ui->pathSelectionPositionLineEdit->text();
    if (posText.isEmpty()) {
        for (auto *node : p.nodes())
            nodesToSelect.push_back(node);
    } else {
        bool ok = true;
        auto posParts = posText.split(":");
        if (posParts.size() != 1 && posParts.size() != 2) {
            QMessageBox::information(this, "Invalid position", "Invalid path position: " + posText);
            return;
        }

        int startPos = posParts.front().toInt(&ok);
        if (!ok) {
            QMessageBox::information(this, "Invalid position", "Invalid path position: " + posParts.front());
            return;
        }

        int endPos = startPos;
        if (posParts.size() == 2) {
            endPos = posParts.back().toInt(&ok);
            if (!ok) {
                QMessageBox::information(this, "Invalid position", "Invalid path position: " + posParts.back());
                return;
            }
        }

        nodesToSelect = p.getNodesAt(startPos, endPos);
    }

    doSelectNodes(nodesToSelect, nodesNotInGraph, ui->pathSelectionRecolorRadioButton->isChecked());
}

// FIXME: deduplicate
void MainWindow::selectWalkNodes() {
    std::vector<QString> nodesNotInGraph;
    std::vector<DeBruijnNode *> nodesToSelect;

    QString walkName = ui->walkSelectionLineEdit2->displayText();
    auto walkIt = g_assemblyGraph->m_deBruijnGraphWalks.find(walkName.toStdString());
    if (walkIt == g_assemblyGraph->m_deBruijnGraphWalks.end()) {
        QMessageBox::information(this, "Sequence walk not found", "Sequence named \"" + walkName + "\" is not found. Maybe you wanted to select nodes instead?");
        return;
    }

    const auto &p = *walkIt;

    QString posText = ui->walkSelectionPositionLineEdit->text();
    if (posText.isEmpty()) {
        for (auto *node : p.walk.nodes())
            nodesToSelect.push_back(node);
    } else {
        bool ok = true;
        auto posParts = posText.split(":");
        if (posParts.size() != 1 && posParts.size() != 2) {
            QMessageBox::information(this, "Invalid position", "Invalid sequence walk position: " + posText);
            return;
        }

        int startPos = posParts.front().toInt(&ok);
        if (!ok) {
            QMessageBox::information(this, "Invalid position", "Invalid sequence walk position: " + posParts.front());
            return;
        }

        int endPos = startPos;
        if (posParts.size() == 2) {
            endPos = posParts.back().toInt(&ok);
            if (!ok) {
                QMessageBox::information(this, "Invalid position", "Invalid sequence walk position: " + posParts.back());
                return;
            }
        }

        nodesToSelect = p.walk.getNodesAt(startPos, endPos);
    }

    doSelectNodes(nodesToSelect, nodesNotInGraph, ui->walkSelectionRecolorRadioButton->isChecked());
}


void MainWindow::openAboutDialog()
{
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
}


void MainWindow::openBlastSearchDialog() {
    // If a BLAST search dialog does not currently exist, make it.
    if (!m_blastSearchDialog) {
        m_blastSearchDialog = new GraphSearchDialog(this);
        connect(m_blastSearchDialog, SIGNAL(changed()), this, SLOT(blastChanged()));
        connect(m_blastSearchDialog, SIGNAL(queryPathSelectionChanged()), g_graphicsView->viewport(), SLOT(update()));
    }

    m_blastSearchDialog->show();
}


//This function is called whenever the user does something in the
//GraphSearchDialog that should be reflected here in MainWindow.
void MainWindow::blastChanged() {
    if (!m_blastSearchDialog)
        return;

    const auto *search = m_blastSearchDialog->search();
    QString blastQueryText = ui->blastQueryComboBox->currentText();
    const auto *queryBefore = search->queries().getQueryFromName(blastQueryText);

    // If we didn't find a currently selected query, but it isn't "none" or "all",
    // then maybe the user changed the name of the currently selected query, and
    // that's why we didn't find it.  In that case, try to find it using the
    // index.
    if (queryBefore == nullptr && blastQueryText != "none" && blastQueryText != "all") {
        int blastQueryIndex = ui->blastQueryComboBox->currentIndex();
        if (ui->blastQueryComboBox->count() > 1)
            --blastQueryIndex;
        if (blastQueryIndex < search->getQueryCount())
            queryBefore = search->query(blastQueryIndex);
    }

    //Rebuild the query combo box, in case the user changed the queries or
    //their names.
    setupBlastQueryComboBox();

    //Look to see if the query selected before is still present.  If so,
    //set the combo box to have that query selected.  If not (or if no
    //query was previously selected), leave the combo box a index 0.
    if (queryBefore && search->isQueryPresent(queryBefore)) {
        int indexOfQuery = ui->blastQueryComboBox->findText(queryBefore->getName());
        if (indexOfQuery != -1)
            ui->blastQueryComboBox->setCurrentIndex(indexOfQuery);
    }

    blastQueryChanged();
}

void MainWindow::setupBlastQueryComboBox() {
    ui->blastQueryComboBox->clear();
    if (!m_blastSearchDialog)
        return;

    const auto *search = m_blastSearchDialog->search();
    QStringList comboBoxItems;
    for (const auto &query : search->queries()) {
        if (query->hasHits())
            comboBoxItems.push_back(query->getName());
    }

    if (comboBoxItems.size() > 1)
        comboBoxItems.push_front("all");

    if (!comboBoxItems.empty()) {
        ui->blastQueryComboBox->addItems(comboBoxItems);
        ui->blastQueryComboBox->setEnabled(true);
    } else {
        ui->blastQueryComboBox->addItem("none");
        ui->blastQueryComboBox->setEnabled(false);
    }
}

void MainWindow::blastQueryChanged() {
    if (!m_blastSearchDialog)
        return;

    QString queryName = ui->blastQueryComboBox->currentText();
    const auto *search = m_blastSearchDialog->search();

    std::vector<search::Query *> shownQueries;
    // If "all" is selected, then we'll display each of the BLAST queries
    if (queryName == "all") {
        for (auto *query : search->queries()) {
            if (query->isShown())
                shownQueries.push_back(query);
        }
    }  else if (auto *query = search->getQueryFromName(queryName))
        // If only one query is selected, then just display that one.
        if (query->isShown())
            shownQueries.push_back(query);

    g_annotationsManager->updateGroupFromHits(search->annotationGroupName(), shownQueries);
    g_graphicsView->viewport()->update();
}

void MainWindow::setUiState(UiState uiState)
{
    m_uiState = uiState;

    // FIXME: simplify the code below!
    switch (uiState)
    {
    case NO_GRAPH_LOADED:
        ui->graphDetailsWidget->setEnabled(false);
        ui->graphDrawingWidget->setEnabled(false);
        ui->graphDisplayWidget->setEnabled(false);
        ui->nodeLabelsWidget->setEnabled(false);
        ui->blastSearchWidget->setEnabled(false);
        ui->bedWidget->setEnabled(false);
        ui->annotationSelectorWidget->setEnabled(false);
        ui->selectionScrollAreaWidgetContents->setEnabled(false);
        ui->actionLoad_CSV->setEnabled(false);
        ui->actionLoad_layout->setEnabled(false);
        ui->actionLoad_paths->setEnabled(false);
        ui->actionLoad_links->setEnabled(false);
        ui->actionExport_layout->setEnabled(false);
        break;
    case GRAPH_LOADED:
        ui->graphDetailsWidget->setEnabled(true);
        ui->graphDrawingWidget->setEnabled(true);
        ui->graphDisplayWidget->setEnabled(false);
        ui->nodeLabelsWidget->setEnabled(false);
        ui->blastSearchWidget->setEnabled(true);
        ui->bedWidget->setEnabled(true);
        ui->annotationSelectorWidget->setEnabled(true);
        ui->selectionScrollAreaWidgetContents->setEnabled(true);
        ui->actionLoad_CSV->setEnabled(true);
        ui->actionLoad_layout->setEnabled(true);
        ui->actionLoad_paths->setEnabled(true);
        ui->actionLoad_links->setEnabled(true);
        ui->actionExport_layout->setEnabled(false);
        break;
    case GRAPH_DRAWN:
        ui->graphDetailsWidget->setEnabled(true);
        ui->graphDrawingWidget->setEnabled(true);
        ui->graphDisplayWidget->setEnabled(true);
        ui->nodeLabelsWidget->setEnabled(true);
        ui->blastSearchWidget->setEnabled(true);
        ui->bedWidget->setEnabled(true);
        ui->annotationSelectorWidget->setEnabled(true);
        ui->selectionScrollAreaWidgetContents->setEnabled(true);
        ui->actionZoom_to_selection->setEnabled(true);
        ui->actionLoad_CSV->setEnabled(true);
        ui->actionLoad_layout->setEnabled(true);
        ui->actionLoad_paths->setEnabled(true);
        ui->actionLoad_links->setEnabled(true);
        ui->actionExport_layout->setEnabled(true);
        break;
    }
}


void MainWindow::showHidePanels()
{
    ui->controlsScrollArea->setVisible(ui->actionControls_panel->isChecked());
    ui->selectionScrollArea->setVisible(ui->actionSelection_panel->isChecked());
}


void MainWindow::bringSelectedNodesToFront() {
    m_scene->blockSignals(true);

    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty()) {
        QMessageBox::information(this, "No nodes selected",
                                 "You must first select nodes in the graph before using "
                                 "the 'Bring selected nodes to front' function.");
        return;
    }

    double topZ = m_scene->getTopZValue();
    double newZ = topZ + 1.0;

    for (auto *selectedNode : selectedNodes) {
        if (GraphicsItemNode * graphicsItemNode = selectedNode->getGraphicsItemNode())
            graphicsItemNode->setZValue(newZ);
    }

    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
}


// TODO: rewrite to selectNodesWithAnnotation
void MainWindow::selectNodesWithBlastHits() {
    const auto *blastHitsGroup = g_annotationsManager->findGroupByName(g_settings->blastAnnotationGroupName);
    if (!blastHitsGroup) {
        QMessageBox::information(this, "No BLAST hits",
                                       "To select nodes with BLAST hits, you must first conduct a BLAST search.");
        return;
    }

    m_scene->blockSignals(true);
    m_scene->clearSelection();

    bool atLeastOneNodeHasBlastHits = false;
    bool atLeastOneNodeSelected = false;

    for (auto &[node, annotations] : blastHitsGroup->annotationMap) {

        bool nodeHasBlastHits;

        //If we're in double mode, only select a node if it has a BLAST hit itself.
        nodeHasBlastHits = !annotations.empty();
        if (!g_settings->doubleMode)
            //In single mode, select a node if it or its reverse complement has a BLAST hit.
            nodeHasBlastHits = nodeHasBlastHits || !blastHitsGroup->getAnnotations(node->getReverseComplement()).empty();

        if (nodeHasBlastHits)
            atLeastOneNodeHasBlastHits = true;

        GraphicsItemNode * graphicsItemNode = node->getGraphicsItemNode();

        if (graphicsItemNode == nullptr)
            continue;

        if (nodeHasBlastHits)
        {
            graphicsItemNode->setSelected(true);
            atLeastOneNodeSelected = true;
        }
    }
    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
    selectionChanged();

    if (!atLeastOneNodeHasBlastHits)
    {
        QMessageBox::information(this, "No BLAST hits",
                                       "To select nodes with BLAST hits, you must first conduct a BLAST search.");
        return;
    }

    if (!atLeastOneNodeSelected)
        QMessageBox::information(this, "No BLAST hits in visible nodes",
                                       "No nodes with BLAST hits are currently visible, so there is nothing to select. "
                                       "Adjust the graph scope to make the nodes with BLAST hits visible.");
    else
        zoomToSelection();
}


void MainWindow::selectNodesWithDeadEnds()
{
    m_scene->blockSignals(true);
    m_scene->clearSelection();

    bool atLeastOneNodeHasDeadEnd = false;
    bool atLeastOneNodeSelected = false;

    for (auto &entry : g_assemblyGraph->m_deBruijnGraphNodes) {
        DeBruijnNode * node = entry;

        bool nodeHasDeadEnd = node->getDeadEndCount() > 0;
        if (nodeHasDeadEnd)
            atLeastOneNodeHasDeadEnd = true;

        GraphicsItemNode * graphicsItemNode = node->getGraphicsItemNode();

        if (graphicsItemNode == nullptr)
            continue;

        if (nodeHasDeadEnd)
        {
            graphicsItemNode->setSelected(true);
            atLeastOneNodeSelected = true;
        }
    }
    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
    selectionChanged();

    if (!atLeastOneNodeHasDeadEnd)
    {
        QMessageBox::information(this, "No dead ends", "Nothing was selected because this graph has no dead ends.");
        return;
    }

    if (!atLeastOneNodeSelected)
        QMessageBox::information(this, "No dead ends in visible nodes",
                                       "Nothing was selected because no dead ends are currently visible. "
                                       "Adjust the graph scope to make the nodes with dead ends hits visible.");
    else
        zoomToSelection();
}


void MainWindow::selectAll() {
    m_scene->blockSignals(true);
    for (auto *item : m_scene->items())
        item->setSelected(true);
    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
    selectionChanged();
}


void MainWindow::selectNone() {
    m_scene->blockSignals(true);
    for (auto *item : m_scene->items())
        item->setSelected(false);
    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
    selectionChanged();
}

void MainWindow::invertSelection() {
    m_scene->blockSignals(true);
    for (auto item : m_scene->items())
        item->setSelected(!item->isSelected());
    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
    selectionChanged();
}



void MainWindow::zoomToSelection()
{
    QList<QGraphicsItem *> selection = m_scene->selectedItems();
    if (selection.empty()) {
        QMessageBox::information(this, "No nodes selected", "You must first select nodes in the graph before using "
                                                            "the 'Zoom to fit selection' function.");
        return;
    }

    QRectF boundingBox;
    for (auto *selectedItem : selection)
        boundingBox = boundingBox | selectedItem->boundingRect();

    zoomToFitRect(boundingBox);
}



void MainWindow::selectContiguous()
{
    selectBasedOnContiguity(CONTIGUOUS_EITHER_STRAND);
}

void MainWindow::selectMaybeContiguous()
{
    selectBasedOnContiguity(MAYBE_CONTIGUOUS);
}

void MainWindow::selectNotContiguous()
{
    selectBasedOnContiguity(NOT_CONTIGUOUS);
}

void MainWindow::resetNodeContiguityStatus() {
    auto *colorer = dynamic_cast<ContiguityNodeColorer*>(&*g_settings->nodeColorer);
    if (!colorer)
        return;

    colorer->reset();
}

void MainWindow::selectBasedOnContiguity(ContiguityStatus targetContiguityStatus) {
    auto *colorer = dynamic_cast<ContiguityNodeColorer*>(&*g_settings->nodeColorer);
    if (!colorer || colorer->empty()) {
        QMessageBox::information(this, "Contiguity determination not done",
                                       "To select nodes by their contiguity status, while in 'Colour "
                                       "by contiguity' mode, you must select a node and then click "
                                       "'Determine contiguity'.");
        return;
    }

    m_scene->blockSignals(true);
    m_scene->clearSelection();

    for (auto *node : g_assemblyGraph->m_deBruijnGraphNodes) {
        GraphicsItemNode * graphicsItemNode = node->getGraphicsItemNode();
        if (graphicsItemNode == nullptr)
            continue;

        //For single nodes, choose the greatest contiguity status of this
        //node and its complement.
        ContiguityStatus nodeContiguityStatus = colorer->getContiguityStatus(node);
        if (!g_settings->doubleMode) {
            ContiguityStatus twinContiguityStatus = colorer->getContiguityStatus(node->getReverseComplement());
            if (twinContiguityStatus < nodeContiguityStatus)
                nodeContiguityStatus = twinContiguityStatus;
        }

        if (targetContiguityStatus == CONTIGUOUS_EITHER_STRAND &&
            (nodeContiguityStatus == CONTIGUOUS_STRAND_SPECIFIC || nodeContiguityStatus == CONTIGUOUS_EITHER_STRAND))
            graphicsItemNode->setSelected(true);
        else if (targetContiguityStatus == MAYBE_CONTIGUOUS && nodeContiguityStatus == MAYBE_CONTIGUOUS)
            graphicsItemNode->setSelected(true);
        else if (targetContiguityStatus == NOT_CONTIGUOUS && nodeContiguityStatus == NOT_CONTIGUOUS)
            graphicsItemNode->setSelected(true);
    }

    m_scene->blockSignals(false);
    g_graphicsView->viewport()->update();
    selectionChanged();
    zoomToSelection();
}


void MainWindow::openBandageUrl() {
    QDesktopServices::openUrl(QUrl("https://github.com/asl/Bandage-NG/wiki"));
}







void MainWindow::setWidgetsFromSettings()
{
    ui->singleNodesRadioButton->setChecked(!g_settings->doubleMode);
    ui->doubleNodesRadioButton->setChecked(g_settings->doubleMode);

    ui->nodeNamesCheckBox->setChecked(g_settings->displayNodeNames);
    ui->nodeLengthsCheckBox->setChecked(g_settings->displayNodeLengths);
    ui->nodeDepthCheckBox->setChecked(g_settings->displayNodeDepth);
    ui->textOutlineCheckBox->setChecked(g_settings->textOutline);

    ui->startingNodesExactMatchRadioButton->setChecked(g_settings->startingNodesExactMatch);
    ui->startingNodesPartialMatchRadioButton->setChecked(!g_settings->startingNodesExactMatch);

    ui->coloursComboBox->setCurrentIndex(g_settings->nodeColorer->scheme());

    setGraphScopeComboBox(g_settings->graphScope);
    ui->nodeDistanceSpinBox->setValue(g_settings->nodeDistance);
    ui->startingNodesLineEdit->setText(g_settings->startingNodes);

    ui->minDepthSpinBox->setValue(g_settings->minDepthRange);
    ui->maxDepthSpinBox->setValue(g_settings->maxDepthRange);
}

void MainWindow::setGraphScopeComboBox(GraphScope graphScope) {
    ui->graphScopeComboBox->setCurrentIndex(int(graphScope));
}

void MainWindow::nodeDistanceChanged() {
    g_settings->nodeDistance = ui->nodeDistanceSpinBox->value();
}

void MainWindow::depthRangeChanged() {
    g_settings->minDepthRange = ui->minDepthSpinBox->value();
    g_settings->maxDepthRange = ui->maxDepthSpinBox->value();
}

void MainWindow::showEvent(QShowEvent *ev)
{
    QMainWindow::showEvent(ev);
    emit windowLoaded();
}


void MainWindow::startingNodesExactMatchChanged()
{
    g_settings->startingNodesExactMatch = ui->startingNodesExactMatchRadioButton->isChecked();
}


void MainWindow::openPathSpecifyDialog()
{
    //Don't open a second dialog if one's already up.
    if (g_memory->pathDialogIsVisible)
        return;

    auto *pathSpecifyDialog = new PathSpecifyDialog(this);
    connect(g_graphicsView, SIGNAL(doubleClickedNode(DeBruijnNode*)), pathSpecifyDialog, SLOT(addNodeName(DeBruijnNode*)));
    pathSpecifyDialog->show();
}


void MainWindow::setSelectedNodesWidgetsVisibility(bool visible)
{
    ui->selectedNodesTitleLabel->setVisible(visible);
    ui->selectedNodesLine1->setVisible(visible);
    ui->selectedNodesLine2->setVisible(visible);
    ui->selectedNodesTextEdit->setVisible(visible);
    ui->selectedNodesModificationWidget->setVisible(visible);
    ui->selectedNodesLengthLabel->setVisible(visible);
    ui->selectedNodesDepthLabel->setVisible(visible);
    ui->selectedNodesTagLabel->setVisible(visible);
    ui->selectedNodesSpacerWidget->setVisible(visible);
}

void MainWindow::setSelectedEdgesWidgetsVisibility(bool visible)
{
    ui->selectedEdgesTitleLabel->setVisible(visible);
    ui->selectedEdgesTextEdit->setVisible(visible);
    ui->selectedEdgesLine->setVisible(visible);
    ui->selectedEdgesSpacerWidget->setVisible(visible);
}


void MainWindow::nodeWidthChanged()
{
    g_assemblyGraph->recalculateAllNodeWidths(ui->nodeWidthSpinBox->value(),
                                              g_settings->depthPower, g_settings->depthEffectOnWidth);
    g_graphicsView->viewport()->update();
}


void MainWindow::saveEntireGraphToFasta() {
    QString defaultFileNameAndPath = g_memory->rememberedPath + "/all_graph_nodes.fasta";
    QString fullFileName = QFileDialog::getSaveFileName(this, "Save entire graph", defaultFileNameAndPath,
                                                        "FASTA (*.fasta)");

    if (fullFileName.isEmpty())
        return; //User did hit cancel

    g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
    if (!utils::saveEntireGraphToFasta(fullFileName, *g_assemblyGraph))
        QMessageBox::warning(this, "Error saving file", "Bandage was unable to save the FASTA file.");
}

void MainWindow::saveEntireGraphToFastaOnlyPositiveNodes() {
    QString defaultFileNameAndPath = g_memory->rememberedPath + "/all_positive_graph_nodes.fasta";
    QString fullFileName = QFileDialog::getSaveFileName(this, "Save entire graph (only positive nodes)",
                                                        defaultFileNameAndPath, "FASTA (*.fasta)");

    if (fullFileName.isEmpty())
        return; //User did hit cancel

    g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
    if (!utils::saveEntireGraphToFastaOnlyPositiveNodes(fullFileName, *g_assemblyGraph))
        QMessageBox::warning(this, "Error saving file", "Bandage was unable to save the FASTA file.");
}


void MainWindow::saveEntireGraphToGfa() {
    QString defaultFileNameAndPath = g_memory->rememberedPath + "/graph.gfa";
    QString fullFileName = QFileDialog::getSaveFileName(this, "Save entire graph", defaultFileNameAndPath,
                                                        "GFA (*.gfa)");

    if (fullFileName.isEmpty())
        return; //User hit cancel

    g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
    if (!gfa::saveEntireGraph(fullFileName, *g_assemblyGraph))
        QMessageBox::warning(this, "Error saving file", "Bandage was unable to save the graph file.");
}

void MainWindow::saveVisibleGraphToGfa() {
    QString defaultFileNameAndPath = g_memory->rememberedPath + "/graph.gfa";
    QString fullFileName = QFileDialog::getSaveFileName(this, "Save visible graph", defaultFileNameAndPath,
                                                        "GFA (*.gfa)");

    if (fullFileName.isEmpty())
        return; //User hit cancel

    g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();
    if (!gfa::saveVisibleGraph(fullFileName, *g_assemblyGraph))
        QMessageBox::warning(this, "Error saving file", "Bandage was unable to save the graph file.");
}


void MainWindow::webBlastSelectedNodes()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
    {
        QMessageBox::information(this, "Web BLAST selected nodes", "No nodes are selected.\n\n"
                                                                   "You must first select nodes in the graph before you can can use web BLAST.");
        return;
    }

    QByteArray selectedNodesFasta;
    for (auto & selectedNode : selectedNodes)
        selectedNodesFasta += selectedNode->getFasta(true, false);
    selectedNodesFasta.chop(1); //remove last newline

    QByteArray urlSafeFasta = makeStringUrlSafe(selectedNodesFasta);
    QByteArray url = "http://blast.ncbi.nlm.nih.gov/Blast.cgi?PROGRAM=blastn&PAGE_TYPE=BlastSearch&LINK_LOC=blasthome&QUERY=" + urlSafeFasta;

    if (url.length() < 8190)
        QDesktopServices::openUrl(QUrl(url));

    else
    {
        QMessageBox::information(this, "Long sequences", "The selected node sequences are too long to pass to the BLAST web "
                                                         "interface via the URL.  Bandage has put them in your clipboard so "
                                                         "you can paste them in.");
        QClipboard * clipboard = QApplication::clipboard();
        clipboard->setText(selectedNodesFasta);

        QByteArray url = "http://blast.ncbi.nlm.nih.gov/Blast.cgi?PROGRAM=blastn&PAGE_TYPE=BlastSearch&LINK_LOC=blasthome";
        QDesktopServices::openUrl(QUrl(url));
    }
}

//http://www.ncbi.nlm.nih.gov/staff/tao/URLAPI/new/node101.html#sub:Escape-of-Unsafe
QByteArray MainWindow::makeStringUrlSafe(QByteArray s)
{
    s.replace("%", "%25");
    s.replace(">", "%3E");
    s.replace("[", "%5B");
    s.replace("]", "%5D");
    s.replace("\n", "%0D%0A");
    s.replace("|", "%7C");
    s.replace("@", "%40");
    s.replace("#", "%23");
    s.replace("+", "%2B");
    s.replace(" ", "+");
    s.replace("\t", "+");

    return s;
}


//This function removes nodes from the visualisation, but leaves them in the
//actual graph.
void MainWindow::hideNodes()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    m_scene->removeGraphicsItemNodes(selectedNodes, !g_settings->doubleMode);
}


//This function removes selected nodes/edges from the graph.
void MainWindow::removeSelection()
{
    std::vector<DeBruijnEdge *> selectedEdges = m_scene->getSelectedEdges();
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();

    m_scene->removeGraphicsItemEdges(selectedEdges, true);
    m_scene->removeGraphicsItemNodes(selectedNodes, true);

    g_assemblyGraph->deleteEdges(selectedEdges);
    g_assemblyGraph->deleteNodes(selectedNodes);

    g_assemblyGraph->determineGraphInfo();
    displayGraphDetails();

    // Now that the graph has changed, we have to reset BLAST and contiguity
    // stuff, as they may no longer apply.
    cleanUpAllBlast();

    resetNodeContiguityStatus();
    resetAllNodeColours();
}



void MainWindow::duplicateSelectedNodes()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.empty())
    {
        QMessageBox::information(this, "No nodes selected", "You must first select one or more nodes before using the 'Duplicate selected nodes' function.");
        return;
    }

    //Nodes are always duplicated in pairs (both positive and negative), so we
    //want to compile a list of only positive nodes.
    QList<DeBruijnNode *> nodesToDuplicate;
    for (auto node : selectedNodes)
    {
        if (node->isNegativeNode())
            node = node->getReverseComplement();
        if (!nodesToDuplicate.contains(node))
            nodesToDuplicate.push_back(node);
    }

    for (auto & i : nodesToDuplicate)
        g_assemblyGraph->duplicateNodePair(i, m_scene);

    g_assemblyGraph->determineGraphInfo();
    displayGraphDetails();

    // Now that the graph has changed, we have to reset BLAST and contiguity
    // stuff, as they may no longer apply.
    cleanUpAllBlast();
    resetNodeContiguityStatus();
    resetAllNodeColours();
}

void MainWindow::mergeSelectedNodes() {
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedNodes();
    if (selectedNodes.size() < 2) {
        QMessageBox::information(this, "Not enough nodes selected", "You must first select two or more nodes before using the 'Merge selected nodes' function.");
        return;
    }

    //Nodes are always merged in pairs (both positive and negative), so we
    //want to compile a list of only positive nodes.
    QList<DeBruijnNode *> nodesToMerge;
    for (auto node : selectedNodes) {
        if (node->isNegativeNode())
            node = node->getReverseComplement();
        if (!nodesToMerge.contains(node))
            nodesToMerge.push_back(node);
    }

    if (nodesToMerge.size() < 2) {
        QMessageBox::information(this, "Not enough nodes selected", "You must first select two or more nodes before using the 'Merge selected nodes' function. "
                                                                    "Note that two complementary nodes only count as a single node regarding a merge.");
        return;
    }

    if (!g_assemblyGraph->mergeNodes(nodesToMerge, m_scene)) {
        QMessageBox::information(this, "Nodes cannot be merged", "You can only merge nodes that are in a single, unbranching path with no extra edges.");
        return;
    }

    g_assemblyGraph->determineGraphInfo();
    displayGraphDetails();

    // Now that the graph has changed, we have to reset BLAST and contiguity
    // stuff, as they may no longer apply.
    cleanUpAllBlast();
    resetNodeContiguityStatus();
    resetAllNodeColours();
}

void MainWindow::mergeAllPossible()
{
    int merges;
    {
        MyProgressDialog progress(this, "Merging nodes", true, "Cancel merge", "Cancelling merge...",
                                  "Clicking this button will stop the merging process. Merges that have already completed will remain "
                                  "merged but no further merging will take place.");

        progress.setWindowModality(Qt::WindowModal);
        progress.setMaxValue(100);
        progress.show();

        connect(g_assemblyGraph.data(), SIGNAL(setMergeTotalCount(int)), &progress, SLOT(setMaxValue(int)));
        connect(g_assemblyGraph.data(), SIGNAL(setMergeCompletedCount(int)), &progress, SLOT(setValue(int)));


        g_graphicsView->viewport()->setUpdatesEnabled(false);
        merges = g_assemblyGraph->mergeAllPossible(m_scene, &progress);
        g_graphicsView->viewport()->setUpdatesEnabled(true);
    }

    if (merges > 0)
    {
        g_assemblyGraph->determineGraphInfo();
        displayGraphDetails();

        //Now that the graph has changed, we have to reset BLAST and contiguity
        //stuff, as they may no longer apply.
        cleanUpAllBlast();
        resetNodeContiguityStatus();
        resetAllNodeColours();
    }
    else
        QMessageBox::information(this, "No possible merges", "The graph contains no nodes that can be merged.");
}


void MainWindow::cleanUpAllBlast() {
    if (m_blastSearchDialog) {
        auto *search = m_blastSearchDialog->search();
        search->cleanUp();
        g_annotationsManager->removeGroupByName(search->annotationGroupName());
    }
    ui->blastQueryComboBox->clear();

    if (m_blastSearchDialog) {
        delete m_blastSearchDialog;
        m_blastSearchDialog = nullptr;
    }
}



void MainWindow::changeNodeName()
{
    DeBruijnNode * selectedNode = m_scene->getOnePositiveSelectedNode();
    if (selectedNode == nullptr) {
        QMessageBox::information(this, "Improper selection", "You must select exactly one node in the graph before using this function.");
        return;
    }

    QString oldName = selectedNode->getNameWithoutSign();
    ChangeNodeNameDialog changeNodeNameDialog(this, oldName);

    if (changeNodeNameDialog.exec()) //The user clicked OK
    {
        g_assemblyGraph->changeNodeName(oldName, changeNodeNameDialog.getNewName());
        selectionChanged();
        cleanUpAllBlast();
    }
}

void MainWindow::changeNodeDepth()
{
    std::vector<DeBruijnNode *> selectedNodes = m_scene->getSelectedPositiveNodes();
    if (selectedNodes.empty()) {
        QMessageBox::information(this, "Improper selection", "You must select at least one node in the graph before using this function.");
        return;
    }

    double oldDepth = g_assemblyGraph->getMeanDepth(selectedNodes);
    ChangeNodeDepthDialog changeNodeDepthDialog(this, &selectedNodes,
                                                oldDepth);

    if (!changeNodeDepthDialog.exec())
        return;

    g_assemblyGraph->changeNodeDepth(selectedNodes,
                                     changeNodeDepthDialog.getNewDepth());
    selectionChanged();
    g_assemblyGraph->recalculateAllNodeWidths(ui->nodeWidthSpinBox->value(),
                                              g_settings->depthPower, g_settings->depthEffectOnWidth);
    g_graphicsView->viewport()->update();
}


void MainWindow::openGraphInfoDialog()
{
    GraphInfoDialog graphInfoDialog(this);
    graphInfoDialog.exec();
}

void MainWindow::exportGraphLayout() {
    QString filter = "Bandage layout (*.layout)";
    QString fullFileName = QFileDialog::getSaveFileName(this, "Export graph layout",
                                                        "",
                                                        "Bandage layout (*.layout);;TSV (*.tsv)",
                                                        &filter);

    if (fullFileName.isEmpty())
        return;

    bool isTSV = filter == "TSV (*.tsv)";
    GraphLayout layout = layout::fromGraph(*g_assemblyGraph,
                                           /* simplified */ isTSV);
    if (isTSV)
        layout::io::saveTSV(fullFileName, layout);
    else
        layout::io::save(fullFileName, layout);
}

void MainWindow::showPathListDialog() {
    std::vector<DeBruijnNode*> selectedNodes;
    for (auto *node : m_scene->getSelectedNodes()) {
        selectedNodes.push_back(node);
        // In single mode add also reverse-complements
        if (!g_settings->doubleMode)
            selectedNodes.push_back(node->getReverseComplement());
    }

    PathListDialog pathListDialog(*g_assemblyGraph, selectedNodes, this);
    pathListDialog.exec();
}

// FIXME: dedup
void MainWindow::showWalkListDialog() {
    std::vector<DeBruijnNode*> selectedNodes;
    for (auto *node : m_scene->getSelectedNodes()) {
        selectedNodes.push_back(node);
        // In single mode add also reverse-complements
        if (!g_settings->doubleMode)
            selectedNodes.push_back(node->getReverseComplement());
    }

    WalkListDialog walkListDialog(*g_assemblyGraph, selectedNodes, this);
    walkListDialog.exec();
}
