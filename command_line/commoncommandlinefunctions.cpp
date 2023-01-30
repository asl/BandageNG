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


#include "commoncommandlinefunctions.h"

#include "graph/assemblygraph.h"
#include "program/globals.h"
#include "program/memory.h"

#include <QDir>
#include <utility>
#include <QApplication>
#include <QRegularExpression>


void outputText(const QString& text, QTextStream * out) {
    QStringList list;
    list << text;
    outputText(list, out);
}

void outputText(const QStringList& text, QTextStream * out) {
    QStringList wrapped;

    bool seenHeaderOrList = false;
    for (const auto& line : text)
    {
        if (isSectionHeader(line) || isListItem(line))
            seenHeaderOrList = true;

        if (isError(line))
            wrapped << wrapText(line, g_memory->terminalWidth, 0, 0);
        else if (!seenHeaderOrList)
            wrapped << wrapText(line, g_memory->terminalWidth, 0, 0);
        else if (isSectionHeader(line) && line.contains("--"))
            wrapped << wrapText(line, g_memory->terminalWidth, 0, 30);
        else if (isSectionHeader(line))
            wrapped << wrapText(line, g_memory->terminalWidth, 0, 10);
        else if (isListItem(line))
            wrapped << wrapText(line, g_memory->terminalWidth, 2, 4);
        else if (isCommand(line))
            wrapped << wrapText(line, g_memory->terminalWidth, 10, 23);
        else if (isOption(line))
            wrapped << wrapText(line, g_memory->terminalWidth, 10, 30);
        else
            wrapped << wrapText(line, g_memory->terminalWidth, 10, 10);
    }

    *out << Qt::endl;
    for (auto & i : wrapped)
    {
        *out << i;
        *out << Qt::endl;
    }
}

QString getElapsedTime(const QDateTime& start, const QDateTime& end)
{
    int msecElapsed = start.msecsTo(end);
    int secElapsed = msecElapsed / 1000;
    msecElapsed = msecElapsed % 1000;
    int minElapsed = secElapsed / 60;
    secElapsed = secElapsed % 60;
    int hoursElapsed = minElapsed / 60;
    minElapsed = minElapsed % 60;

    QString msecString = QString("%1").arg(msecElapsed, 2, 10, QChar('0'));
    QString secString = QString("%1").arg(secElapsed, 2, 10, QChar('0'));
    QString minString = QString("%1").arg(minElapsed, 2, 10, QChar('0'));
    QString hourString = QString("%1").arg(hoursElapsed, 2, 10, QChar('0'));

    return hourString + ":" + minString + ":" + secString + "." + msecString;
}


QStringList wrapText(QString text, int width, int firstLineIndent, int laterLineIndent)
{
    QStringList returnList;

    QString firstLineSpaces = "";
    for (int i = 0; i < firstLineIndent; ++i)
        firstLineSpaces += ' ';
    QString laterLineSpaces = "";
    for (int i = 0; i < laterLineIndent; ++i)
        laterLineSpaces += ' ';

    text = firstLineSpaces + text;

    //If the terminal width is at the minimum, don't bother wrapping.
    if (g_memory->terminalWidth <= 50)
    {
        returnList << text;
        return returnList;
    }

    while (text.length() > width)
    {
        QString leftString = text.left(width);
        int spaceIndex = leftString.lastIndexOf(' ');
        if (spaceIndex < width / 2)
            spaceIndex = width;

        leftString = text.left(spaceIndex);
        returnList << rstrip(leftString);
        text = laterLineSpaces + text.mid(spaceIndex).trimmed();
    }

    returnList << text;
    return returnList;
}

//http://stackoverflow.com/questions/8215303/how-do-i-remove-trailing-whitespace-from-a-qstring
QString rstrip(const QString& str)
{
    int n = str.size() - 1;
    for (; n >= 0; --n)
    {
        if (!str.at(n).isSpace())
            return str.left(n + 1);
    }
    return "";
}

QString getDefaultColour(QColor colour)
{
    return "(default: " + getColourName(colour.name()) + ")";
}

QString getDefaultColorMap(ColorMap colorMap)
{
    return "(default: " + getColorMapName(colorMap) + ")";
}

std::string getBandageTitleAsciiArt()
{
    return "  ____                  _                  \n |  _ \\                | |                 \n | |_) | __ _ _ __   __| | __ _  __ _  ___ \n |  _ < / _` | '_ \\ / _` |/ _` |/ _` |/ _ \\\n | |_) | (_| | | | | (_| | (_| | (_| |  __/\n |____/ \\__,_|_| |_|\\__,_|\\__,_|\\__, |\\___|\n                                 __/ |     \n                                |___/      ";
}

bool isOption(QString text)
{
    bool option = (text.length() > 2 && text[0] == '-' && text[1] == '-' && text[2] != '-');

    QRegularExpression rx("^[\\w ]+:");
    return option || text.contains(rx);
}

bool isSectionHeader(const QString& text)
{
    //Make an exception:
    if (text.startsWith("Node widths are determined")) {
        return false;
    }

    QRegularExpression rx("^[\\w ]+:");
    return text.contains(rx);
}


bool isListItem(QString text)
{
    return (text.length() > 1 && text[0] == '*' && text[1] == ' ');
}

bool isCommand(const QString& text)
{
    return text.startsWith("load   ") ||
            text.startsWith("info   ") ||
            text.startsWith("image   ") ||
            text.startsWith("querypaths   ") ||
            text.startsWith("reduce   ");
}


bool isError(const QString& text)
{
    return text.startsWith("Bandage-NG error");
}

void getOnlineHelpMessage(QStringList * text)
{
    *text << "Online Bandage help: https://github.com/asl/BandageNG/wiki";
    *text << "";
}
