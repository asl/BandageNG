cmake_minimum_required(VERSION 3.13)
project(BandageNG VERSION 2022.08 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Test Concurrent)
find_package(ZLIB REQUIRED)

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
        graph/assemblygraphbuilder.cpp
        graph/assemblygraph.cpp
        graph/annotationsmanager.cpp
        graph/debruijnedge.cpp
        graph/debruijnnode.cpp
        graph/graphicsitemedge.cpp
        graph/graphicsitemnode.cpp
        graph/graphlocation.cpp
        graph/path.cpp
        program/globals.cpp
        program/memory.cpp
        program/scinot.cpp
        program/settings.cpp
        ui/dialogs/aboutdialog.cpp
        ui/annotationswidget.cpp
        ui/bedwidget.cpp
        ui/dialogs/blasthitfiltersdialog.cpp
        ui/dialogs/blastsearchdialog.cpp
        ui/dialogs/changenodedepthdialog.cpp
        ui/dialogs/changenodenamedialog.cpp
        ui/widgets/colourbutton.cpp
        ui/dialogs/enteroneblastquerydialog.cpp
        ui/dialogs/pathlistdialog.cpp
        ui/graphicsviewzoom.cpp
        ui/dialogs/graphinfodialog.cpp
        ui/widgets/infotextwidget.cpp
        ui/mainwindow.cpp
        ui/bandagegraphicsscene.cpp
        ui/bandagegraphicsview.cpp
        ui/dialogs/myprogressdialog.cpp
        ui/nodewidthvisualaid.cpp
        ui/dialogs/pathspecifydialog.cpp
        ui/dialogs/querypathsdialog.cpp
        ui/dialogs/settingsdialog.cpp
        ui/widgets/verticallabel.cpp
        ui/widgets/verticalscrollarea.cpp
        graph/nodecolorer.cpp
        graph/sequenceutils.cpp 
        graph/annotation.cpp)

set(FORMS
        ui/dialogs/aboutdialog.ui
        ui/dialogs/blasthitfiltersdialog.ui
        ui/dialogs/blastsearchdialog.ui
        ui/dialogs/changenodedepthdialog.ui
        ui/dialogs/changenodenamedialog.ui
        ui/dialogs/enteroneblastquerydialog.ui
        ui/dialogs/graphinfodialog.ui
        ui/mainwindow.ui
        ui/dialogs/myprogressdialog.ui
        ui/dialogs/pathspecifydialog.ui
        ui/dialogs/querypathsdialog.ui
        ui/dialogs/settingsdialog.ui)

set(RESOURCES images/images.qrc images/application.icns)

add_compile_definitions(APP_VERSION="${PROJECT_VERSION}-dev")
include_directories(ui)

add_subdirectory(thirdparty)
include_directories(thirdparty)

include(FetchContent)
FetchContent_Declare(lexy URL https://lexy.foonathan.net/download/lexy-src.zip)
FetchContent_MakeAvailable(lexy)

add_library(BandageLayout STATIC layout/graphlayoutworker.cpp layout/io.cpp layout/graphlayout.cpp)
target_link_libraries(BandageLayout PRIVATE OGDF Qt6::Concurrent Qt6::Gui Qt6::Widgets)

add_library(BandageIo STATIC
        io/gfa.cpp
        io/bedloader.cpp
        io/fileutils.cpp io/cigar.cpp io/gaf.cpp)
target_link_libraries(BandageIo PRIVATE Qt6::Gui Qt6::Widgets foonathan::lexy)

# FIXME: Untagle this
add_library(BandageLib STATIC ${LIB_SOURCES} ${FORMS} program/colormap.cpp graph/gfawriter.cpp graph/gfawriter.h graph/fastawriter.cpp graph/io.cpp graph/graphscope.cpp)
target_link_libraries(BandageLib PRIVATE BandageLayout BandageIo Qt6::Concurrent Qt6::Widgets Qt6::Svg ZLIB::ZLIB)
target_include_directories(BandageLib INTERFACE ".")

add_executable(BandageNG program/main.cpp ${RESOURCES})
target_link_libraries(BandageNG BandageLib Qt6::Core Qt6::Widgets Qt6::Concurrent ZLIB::ZLIB)

if (APPLE)
  set_target_properties(BandageNG PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "BandageNG"
        MACOSX_BUNDLE_BUNDLE_IDENTIFIER "org.bandage-ng.app"
        MACOSX_BUNDLE_ICON_FILE "application.icns")
  set_source_files_properties(images/application.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()

add_subdirectory(resources)
install(TARGETS BandageNG
        DESTINATION bin
        COMPONENT main)

enable_testing(true)
add_subdirectory(tests)
