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


#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "graph/debruijnnode.h"
#include "graph/nodecolorer.h"
#include "graph/graphscope.h"

#include "layout/graphlayout.h"
#include "program/globals.h"

#include <QMainWindow>
#include <QGraphicsScene>
#include <QMap>
#include <QString>
#include <vector>
#include <QLineEdit>
#include <QRectF>
#include <QThread>

Q_MOC_INCLUDE("graph/debruijnnode.h")

class GraphicsViewZoom;
class BandageGraphicsScene;
class DeBruijnNode;
class DeBruijnEdge;
class BlastSearchDialog;

enum UiState {NO_GRAPH_LOADED, GRAPH_LOADED, GRAPH_DRAWN};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QString fileToLoadOnStartup = "", bool drawGraphAfterLoad = false);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    BandageGraphicsScene * m_scene;

    GraphicsViewZoom * m_graphicsViewZoom;
    double m_previousZoomSpinBoxValue;
    QString m_imageFilter;
    QString m_fileToLoadOnStartup;
    bool m_drawGraphAfterLoad;
    UiState m_uiState;
    BlastSearchDialog * m_blastSearchDialog;
    bool m_alreadyShown;

    void cleanUp();
    void displayGraphDetails();
    void clearGraphDetails();
    void resetScene();
    void resetAllNodeColours();
    void layoutGraph();
    void zoomToFitRect(QRectF rect);
    void setZoomSpinBoxStep();
    void getSelectedNodeInfo(int & selectedNodeCount, QString & selectedNodeCountText,
                             QString & selectedNodeListText, QString & selectedNodeLengthText, QString &selectedNodeDepthText,
                             QString &selectNodeTagText);
    QString getSelectedEdgeListText();
    std::vector<DeBruijnNode *> getNodesFromLineEdit(QLineEdit * lineEdit, bool exactMatch, std::vector<QString> * nodesNotInGraph = nullptr);
    void setUiState(UiState uiState);
    void selectBasedOnContiguity(ContiguityStatus contiguityStatus);
    void setWidgetsFromSettings();
    QString getDefaultImageFileName();
    void setGraphScopeComboBox(GraphScope graphScope);
    void setupBlastQueryComboBox();
    void setupPathSelectionLineEdit(QLineEdit *lineEdit);
    bool checkForImageSave();

    void setSelectedNodesWidgetsVisibility(bool visible);
    void setSelectedEdgesWidgetsVisibility(bool visible);
    void setStartingNodesWidgetVisibility(bool visible);
    void setNodeDistanceWidgetVisibility(bool visible);
    void setDepthRangeWidgetVisibility(bool visible);
    void setPathSelectionWidgetVisibility(bool visible);
    static QByteArray makeStringUrlSafe(QByteArray s);
    std::vector<DeBruijnNode *> addComplementaryNodes(std::vector<DeBruijnNode *> nodes);

private slots:
    void loadGraph(QString fullFileName = "");
    void loadCSV(QString fullFileName = "");
    void loadGraphLayout(QString fullFileName = "");
    void loadGraphPaths(QString fullFileName = "");
    void selectionChanged();
    void graphScopeChanged();
    void drawGraph();
    void zoomSpinBoxChanged();
    void zoomedWithMouseWheel();
    void zoomToFitScene();
    void copySelectedSequencesToClipboardActionTriggered();
    void copySelectedSequencesToClipboard();
    void saveSelectedSequencesToFileActionTriggered();
    void saveSelectedSequencesToFile();
    void copySelectedPathToClipboard();
    void saveSelectedPathToFile();
    void switchColourScheme(int idx = -1);
    void switchTagValue();
    void determineContiguityFromSelectedNode();
    void saveImageCurrentView();
    void saveImageEntireScene();
    void setTextDisplaySettings();
    void fontButtonPressed();
    void setNodeCustomColour();
    void setNodeCustomLabel();
    void hideNodes();
    void openSettingsDialog();
    void openAboutDialog();
    void doSelectNodes(const std::vector<DeBruijnNode *> &nodesToSelect,
                       const std::vector<QString> &nodesNotInGraph,
                       bool recolor = false);
    void selectPathNodes();
    void showPathListDialog();
    void selectUserSpecifiedNodes();
    void graphLayoutFinished(const GraphLayout &layout);
    void openBlastSearchDialog();
    void blastChanged();
    void blastQueryChanged();
    void showHidePanels();
    void bringSelectedNodesToFront();
    void selectNodesWithBlastHits();
    void selectNodesWithDeadEnds();
    void selectAll();
    void selectNone();
    void invertSelection();
    void zoomToSelection();
    void selectContiguous();
    void selectMaybeContiguous();
    void selectNotContiguous();
    void openBandageUrl();
    void nodeDistanceChanged();
    void depthRangeChanged();
    void afterMainWindowShow();
    void startingNodesExactMatchChanged();
    void openPathSpecifyDialog();
    void nodeWidthChanged();
    void saveEntireGraphToFasta();
    void saveEntireGraphToFastaOnlyPositiveNodes();
    void saveEntireGraphToGfa();
    void saveVisibleGraphToGfa();
    void webBlastSelectedNodes();
    void removeSelection();
    void duplicateSelectedNodes();
    void mergeSelectedNodes();
    void mergeAllPossible();
    void cleanUpAllBlast();
    void changeNodeName();
    void changeNodeDepth();
    void openGraphInfoDialog();
    void exportGraphLayout();

protected:
      void showEvent(QShowEvent *ev) override;

signals:
      void windowLoaded();

};

#endif // MAINWINDOW_H
