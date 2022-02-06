cmake_minimum_required(VERSION 3.13)
project(Bandage VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Test)

set(LIB_SOURCES
        program/settings.cpp
        program/globals.cpp
        program/graphlayoutworker.cpp
        graph/debruijnnode.cpp
        graph/debruijnedge.cpp
        graph/graphicsitemnode.cpp
        graph/graphicsitemedge.cpp
        ui/mainwindow.cpp
        ui/graphicsviewzoom.cpp
        ui/settingsdialog.cpp
        ui/mygraphicsview.cpp
        ui/mygraphicsscene.cpp
        ui/aboutdialog.cpp
        ui/enteroneblastquerydialog.cpp
        blast/blasthit.cpp
        blast/blastqueries.cpp
        ui/blastsearchdialog.cpp
        ui/infotextwidget.cpp
        graph/assemblygraph.cpp
        ui/verticalscrollarea.cpp
        ui/myprogressdialog.cpp
        ui/nodewidthvisualaid.cpp
        ui/verticallabel.cpp
        command_line/load.cpp
        command_line/image.cpp
        command_line/commoncommandlinefunctions.cpp
        ui/mytablewidget.cpp
        blast/buildblastdatabaseworker.cpp
        ui/colourbutton.cpp
        blast/blastquery.cpp
        blast/runblastsearchworker.cpp
        blast/blastsearch.cpp
        graph/path.cpp
        ui/pathspecifydialog.cpp
        graph/graphlocation.cpp
        ui/tablewidgetitemint.cpp
        ui/tablewidgetitemdouble.cpp
        ui/tablewidgetitemshown.cpp
        program/memory.cpp
        ui/querypathspushbutton.cpp
        ui/querypathsdialog.cpp
        blast/blastquerypath.cpp
        ui/blasthitfiltersdialog.cpp
        program/scinot.cpp
        ui/changenodenamedialog.cpp
        ui/querypathsequencecopybutton.cpp
        command_line/querypaths.cpp
        command_line/info.cpp
        command_line/reduce.cpp
        ogdf/basic/Graph.cpp
        ogdf/basic/GraphAttributes.cpp
        ogdf/energybased/FMMMLayout.cpp
        ogdf/basic/geometry.cpp
        ogdf/cluster/ClusterGraphAttributes.cpp
        ogdf/internal/energybased/FruchtermanReingold.cpp
        ogdf/internal/energybased/NMM.cpp
        ogdf/fileformats/GmlParser.cpp
        ogdf/basic/simple_graph_alg.cpp
        ogdf/basic/basic.cpp
        ogdf/fileformats/XmlParser.cpp
        ogdf/basic/String.cpp
        ogdf/basic/Hashing.cpp
        ogdf/internal/basic/PoolMemoryAllocator.cpp
        ogdf/basic/GraphCopy.cpp
        ogdf/basic/CombinatorialEmbedding.cpp
        ogdf/fileformats/OgmlParser.cpp
        ogdf/cluster/ClusterGraph.cpp
        ogdf/basic/Math.cpp
        ogdf/internal/energybased/EdgeAttributes.cpp
        ogdf/internal/energybased/NodeAttributes.cpp
        ogdf/energybased/MAARPacking.cpp
        ogdf/energybased/Multilevel.cpp
        ogdf/energybased/numexcept.cpp
        ogdf/energybased/Set.cpp
        ogdf/fileformats/Ogml.cpp
        ogdf/fileformats/DinoXmlParser.cpp
        ogdf/fileformats/DinoXmlScanner.cpp
        ogdf/fileformats/DinoTools.cpp
        ogdf/fileformats/DinoLineBuffer.cpp
        ogdf/basic/System.cpp
        ogdf/internal/energybased/QuadTreeNM.cpp
        ogdf/internal/energybased/QuadTreeNodeNM.cpp
        ogdf/basic/Constraint.cpp
        ogdf/internal/energybased/MultilevelGraph.cpp
        ui/graphinfodialog.cpp
        ui/tablewidgetitemname.cpp
        ui/changenodedepthdialog.cpp)

set(FORMS
        ui/mainwindow.ui
        ui/settingsdialog.ui
        ui/aboutdialog.ui
        ui/enteroneblastquerydialog.ui
        ui/blastsearchdialog.ui
        ui/myprogressdialog.ui
        ui/pathspecifydialog.ui
        ui/querypathsdialog.ui
        ui/blasthitfiltersdialog.ui
        ui/changenodenamedialog.ui
        ui/graphinfodialog.ui
        ui/changenodedepthdialog.ui)

set(RESOURCES images/images.qrc)

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
add_compile_definitions(APP_VERSION="0.9.0-dev")
include_directories(.)
include_directories(ui)

add_executable(Bandage program/main.cpp ${LIB_SOURCES} ${FORMS} ${RESOURCES})
target_link_libraries(Bandage PRIVATE Qt6::Widgets Qt6::Svg)

add_executable(BandageTests tests/bandagetests.cpp ${LIB_SOURCES} ${FORMS} ${RESOURCES})
target_link_libraries(BandageTests PRIVATE Qt6::Widgets Qt6::Svg Qt6::Test)
