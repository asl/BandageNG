cmake_minimum_required(VERSION 3.13)
project(Bandage VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Test)

set(LIB_SOURCES
        blast/blasthit.cpp
        blast/blastqueries.cpp
        blast/blastquery.cpp
        blast/blastquerypath.cpp
        blast/blastsearch.cpp
        blast/buildblastdatabaseworker.cpp
        blast/runblastsearchworker.cpp
        command_line/commoncommandlinefunctions.cpp
        command_line/image.cpp
        command_line/info.cpp
        command_line/load.cpp
        command_line/querypaths.cpp
        command_line/reduce.cpp
        graph/assemblygraph.cpp
        graph/debruijnedge.cpp
        graph/debruijnnode.cpp
        graph/graphicsitemedge.cpp
        graph/graphicsitemnode.cpp
        graph/graphlocation.cpp
        graph/path.cpp
        ogdf/basic/CombinatorialEmbedding.cpp
        ogdf/basic/Constraint.cpp
        ogdf/basic/Graph.cpp
        ogdf/basic/GraphAttributes.cpp
        ogdf/basic/GraphCopy.cpp
        ogdf/basic/Hashing.cpp
        ogdf/basic/Math.cpp
        ogdf/basic/String.cpp
        ogdf/basic/System.cpp
        ogdf/basic/basic.cpp
        ogdf/basic/geometry.cpp
        ogdf/basic/simple_graph_alg.cpp
        ogdf/cluster/ClusterGraph.cpp
        ogdf/cluster/ClusterGraphAttributes.cpp
        ogdf/energybased/FMMMLayout.cpp
        ogdf/energybased/MAARPacking.cpp
        ogdf/energybased/Multilevel.cpp
        ogdf/energybased/Set.cpp
        ogdf/energybased/numexcept.cpp
        ogdf/fileformats/DinoLineBuffer.cpp
        ogdf/fileformats/DinoTools.cpp
        ogdf/fileformats/DinoXmlParser.cpp
        ogdf/fileformats/DinoXmlScanner.cpp
        ogdf/fileformats/GmlParser.cpp
        ogdf/fileformats/Ogml.cpp
        ogdf/fileformats/OgmlParser.cpp
        ogdf/fileformats/XmlParser.cpp
        ogdf/internal/basic/PoolMemoryAllocator.cpp
        ogdf/internal/energybased/EdgeAttributes.cpp
        ogdf/internal/energybased/FruchtermanReingold.cpp
        ogdf/internal/energybased/MultilevelGraph.cpp
        ogdf/internal/energybased/NMM.cpp
        ogdf/internal/energybased/NodeAttributes.cpp
        ogdf/internal/energybased/QuadTreeNM.cpp
        ogdf/internal/energybased/QuadTreeNodeNM.cpp
        program/globals.cpp
        program/graphlayoutworker.cpp
        program/memory.cpp
        program/scinot.cpp
        program/settings.cpp
        ui/aboutdialog.cpp
        ui/blasthitfiltersdialog.cpp
        ui/blastsearchdialog.cpp
        ui/changenodedepthdialog.cpp
        ui/changenodenamedialog.cpp
        ui/colourbutton.cpp
        ui/enteroneblastquerydialog.cpp
        ui/graphicsviewzoom.cpp
        ui/graphinfodialog.cpp
        ui/infotextwidget.cpp
        ui/mainwindow.cpp
        ui/mygraphicsscene.cpp
        ui/mygraphicsview.cpp
        ui/myprogressdialog.cpp
        ui/mytablewidget.cpp
        ui/nodewidthvisualaid.cpp
        ui/pathspecifydialog.cpp
        ui/querypathsdialog.cpp
        ui/querypathsequencecopybutton.cpp
        ui/querypathspushbutton.cpp
        ui/settingsdialog.cpp
        ui/tablewidgetitemdouble.cpp
        ui/tablewidgetitemint.cpp
        ui/tablewidgetitemname.cpp
        ui/tablewidgetitemshown.cpp
        ui/verticallabel.cpp
        ui/verticalscrollarea.cpp)

set(FORMS
        ui/aboutdialog.ui
        ui/blasthitfiltersdialog.ui
        ui/blastsearchdialog.ui
        ui/changenodedepthdialog.ui
        ui/changenodenamedialog.ui
        ui/enteroneblastquerydialog.ui
        ui/graphinfodialog.ui
        ui/mainwindow.ui
        ui/myprogressdialog.ui
        ui/pathspecifydialog.ui
        ui/querypathsdialog.ui
        ui/settingsdialog.ui)

set(RESOURCES images/images.qrc)

add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x050F00)
add_compile_definitions(APP_VERSION="${PROJECT_VERSION}-dev")
include_directories(ui)

add_subdirectory(thirdparty)

add_executable(Bandage program/main.cpp ${LIB_SOURCES} ${FORMS} ${RESOURCES})
target_link_libraries(Bandage PRIVATE Qt6::Widgets Qt6::Svg gfa1)

if (APPLE)
  set_target_properties(Bandage PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "Bandge"
        MACOSX_BUNDLE_BUNDLE_IDENTIFIER "org.bandage.app")
endif()

add_executable(BandageTests tests/bandagetests.cpp ${LIB_SOURCES} ${FORMS} ${RESOURCES})
target_link_libraries(BandageTests PRIVATE Qt6::Widgets Qt6::Svg Qt6::Test gfa1)
