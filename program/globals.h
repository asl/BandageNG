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


#ifndef GLOBALS_H
#define GLOBALS_H

#include <QString>
#include <QSharedPointer>

#include <utility>

class Settings;
class Memory;
class BandageGraphicsView;
class BlastSearch;
class AssemblyGraph;
class AnnotationsManager;

enum GraphScope {WHOLE_GRAPH, AROUND_NODE, AROUND_PATHS, AROUND_BLAST_HITS, DEPTH_RANGE};

// Some of the program's common components are made global, so they don't have
// to be passed around as parameters.
// FIXME: This is bad. Factor this out when possible
extern QSharedPointer<Settings> g_settings;
extern QSharedPointer<Memory> g_memory;
extern BandageGraphicsView * g_graphicsView;
extern double g_absoluteZoom;
extern QSharedPointer<BlastSearch> g_blastSearch;
extern QSharedPointer<AssemblyGraph> g_assemblyGraph;
extern std::shared_ptr<AnnotationsManager> g_annotationsManager;


//Functions for formatting numbers are used in many places, and are made global.
QString formatIntForDisplay(int num);
QString formatIntForDisplay(long long num);
QString formatIntForDisplay(size_t num);
QString formatDoubleForDisplay(double num, int decimalPlacesToDisplay);
QString formatDepthForDisplay(double depth);


#endif // GLOBALS_H
