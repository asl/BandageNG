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

#include "program/colormap.h"

#include <QString>
#include <QStringList>
#include <QColor>
#include <QTextStream>
#include <QDateTime>
#include <QStringList>

QString getElapsedTime(const QDateTime& start, const QDateTime& end);

QStringList wrapText(QString text, int width, int firstLineIndent, int laterLineIndent);
QString rstrip(const QString& str);


QString getDefaultColour(QColor colour);
QString getDefaultColorMap(ColorMap colorMap);

std::string getBandageTitleAsciiArt();
bool isOption(QString text);
bool isSectionHeader(const QString& text);
bool isListItem(QString text);
bool isCommand(const QString& text);
bool isError(const QString& text);
void outputText(const QString& text, QTextStream * out);
void outputText(const QStringList& text, QTextStream * out);
void getOnlineHelpMessage(QStringList * text);

#endif // COMMANDCOMMANDLINEFUNCTIONS_H
