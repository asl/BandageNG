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


#ifndef COMMANDCOMMANDLINEFUNCTIONS_H
#define COMMANDCOMMANDLINEFUNCTIONS_H

#include "program/globals.h"
#include <QString>
#include <QStringList>
#include <QColor>
#include <QTextStream>
#include "program/scinot.h"
#include <QDateTime>
#include <QStringList>
#include "program/settings.h"

QStringList getArgumentList(int argc, char *argv[]);

bool checkForHelp(const QStringList& arguments);
bool checkForHelpAll(const QStringList& arguments);
bool checkForVersion(const QStringList& arguments);

QString checkOptionForInt(const QString& option, QStringList * arguments, IntSetting setting, bool offOkay);
QString checkOptionForFloat(const QString& option, QStringList * arguments, FloatSetting setting, bool offOkay);
QString checkOptionForSciNot(const QString& option, QStringList * arguments, SciNotSetting setting, bool offOkay);
QString checkOptionForString(const QString& option, QStringList * arguments,
                             const QStringList& validOptionsList,
                             QString validDescription = "");
QString checkOptionForColour(const QString& option, QStringList * arguments);
QString checkOptionForFile(const QString& option, QStringList * arguments);
bool checkIfFileExists(const QString& filename);
void checkOptionWithoutValue(const QString& option, QStringList * arguments);
QString checkTwoOptionsForFloats(const QString& option1, const QString& option2,
                                 QStringList * arguments,
                                 FloatSetting setting1,
                                 FloatSetting setting2,
                                 bool secondMustBeEqualOrLarger = false);

bool isOptionPresent(const QString& option, QStringList * arguments);
bool isOptionAndValuePresent(const QString& option, const QString& value,
                             QStringList * arguments);

int getIntOption(const QString& option, QStringList * arguments);
double getFloatOption(const QString& option, QStringList * arguments);
SciNot getSciNotOption(const QString& option, QStringList * arguments);
QColor getColourOption(const QString& option, QStringList * arguments);
ColorMap getColorMapOption(const QString& option, QStringList * arguments);
NodeColorScheme getColourSchemeOption(const QString& option, QStringList * arguments);
std::set<ViewId> getBlastAnnotationViews(const QString& option, QStringList * arguments);
GraphScope getGraphScopeOption(const QString& option, QStringList * arguments);
QString getStringOption(const QString& option, QStringList * arguments);

QString checkForInvalidOrExcessSettings(QStringList * arguments);
QString checkForExcessArguments(const QStringList& arguments);

void parseSettings(QStringList arguments);

void getCommonHelp(QStringList * text);
void getSettingsUsage(QStringList *text);

QString getElapsedTime(const QDateTime& start, const QDateTime& end);

void getGraphScopeOptions(QStringList * text);

QStringList wrapText(QString text, int width, int firstLineIndent, int laterLineIndent);
QString rstrip(const QString& str);


QString getRangeAndDefault(IntSetting setting);
QString getRangeAndDefault(IntSetting setting, QString defaultVal);
QString getRangeAndDefault(FloatSetting setting);
QString getRangeAndDefault(FloatSetting setting, QString defaultVal);
QString getRangeAndDefault(SciNotSetting setting);
QString getRangeAndDefault(int min, int max, int defaultVal);
QString getRangeAndDefault(int min, int max, int defaultVal);
QString getRangeAndDefault(int min, int max, QString defaultVal);
QString getRangeAndDefault(double min, double max, double defaultVal);
QString getRangeAndDefault(double min, double max, QString defaultVal);
QString getRangeAndDefault(const QString& min, QString max, QString defaultVal);
QString getDefaultColour(QColor colour);
QString getDefaultColorMap(ColorMap colorMap);

QString getBandageTitleAsciiArt();
bool isOption(QString text);
bool isSectionHeader(const QString& text);
bool isListItem(QString text);
bool isCommand(const QString& text);
bool isError(const QString& text);
void outputText(const QString& text, QTextStream * out);
void outputText(const QStringList& text, QTextStream * out);
void getOnlineHelpMessage(QStringList * text);

#endif // COMMANDCOMMANDLINEFUNCTIONS_H
