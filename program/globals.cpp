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


#include "globals.h"
#include <QLocale>
#include <QDir>
#include <QTextStream>
#include <QApplication>
#include <cmath>

QSharedPointer<Settings> g_settings;
QSharedPointer<Memory> g_memory;
MyGraphicsView * g_graphicsView;
double g_absoluteZoom;
QSharedPointer<BlastSearch> g_blastSearch;
QSharedPointer<AssemblyGraph> g_assemblyGraph;
std::shared_ptr<AnnotationsManager> g_annotationsManager;


QString formatIntForDisplay(int num)
{
    QLocale locale;
    return locale.toString(num);
}

QString formatIntForDisplay(long long num)
{
    QLocale locale;
    return locale.toString(num);
}

QString formatDoubleForDisplay(double num, int decimalPlacesToDisplay)
{
    //Add a bit for rounding
    double addValue = 0.5 / pow(10, decimalPlacesToDisplay);
    num += addValue;

    QLocale locale;
    QString withCommas = locale.toString(num, 'f');

    QString final;
    bool pastDecimalPoint = false;
    int numbersPastDecimalPoint = 0;
    for (auto withComma : withCommas)
    {
        final += withComma;

        if (pastDecimalPoint)
            ++numbersPastDecimalPoint;

        if (numbersPastDecimalPoint >= decimalPlacesToDisplay)
            return final;

        if (withComma == locale.decimalPoint())
            pastDecimalPoint = true;
    }
    return final;
}


QString formatDepthForDisplay(double depth)
{
    if (depth == 0.0)
        return "0.0x";

    int decimals = 1;
    double multipliedDepth = fabs(depth);
    while (multipliedDepth < 10.0)
    {
        multipliedDepth *= 10.0;
        decimals += 1;
    }
    return formatDoubleForDisplay(depth, decimals) + "x";
}