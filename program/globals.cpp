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



std::vector<QColor> getPresetColours()
{
    std::vector<QColor> presetColours;

    presetColours.emplace_back("#306FF8");
    presetColours.emplace_back("#86BB18");
    presetColours.emplace_back("#DF123A");
    presetColours.emplace_back("#181E2A");
    presetColours.emplace_back("#F91BBD");
    presetColours.emplace_back("#3CB2A4");
    presetColours.emplace_back("#D29AC1");
    presetColours.emplace_back("#E2922E");
    presetColours.emplace_back("#22501B");
    presetColours.emplace_back("#57297D");
    presetColours.emplace_back("#3FA0E6");
    presetColours.emplace_back("#770739");
    presetColours.emplace_back("#6A390C");
    presetColours.emplace_back("#25AB5D");
    presetColours.emplace_back("#ACAF61");
    presetColours.emplace_back("#F0826F");
    presetColours.emplace_back("#E94A80");
    presetColours.emplace_back("#C187F2");
    presetColours.emplace_back("#7E5764");
    presetColours.emplace_back("#037290");
    presetColours.emplace_back("#D65114");
    presetColours.emplace_back("#08396A");
    presetColours.emplace_back("#99ABBE");
    presetColours.emplace_back("#F270C0");
    presetColours.emplace_back("#F056F9");
    presetColours.emplace_back("#8E8D00");
    presetColours.emplace_back("#70010F");
    presetColours.emplace_back("#9C1E9A");
    presetColours.emplace_back("#471B1F");
    presetColours.emplace_back("#A00B6D");
    presetColours.emplace_back("#38C037");
    presetColours.emplace_back("#282C16");
    presetColours.emplace_back("#15604D");
    presetColours.emplace_back("#2E75D6");
    presetColours.emplace_back("#A09DEB");
    presetColours.emplace_back("#8454D7");
    presetColours.emplace_back("#301745");
    presetColours.emplace_back("#A45704");
    presetColours.emplace_back("#4D8C0E");
    presetColours.emplace_back("#C09860");
    presetColours.emplace_back("#009C73");
    presetColours.emplace_back("#FD6453");
    presetColours.emplace_back("#C11C4B");
    presetColours.emplace_back("#183B8B");
    presetColours.emplace_back("#5E6706");
    presetColours.emplace_back("#E42005");
    presetColours.emplace_back("#4873AF");
    presetColours.emplace_back("#6CA563");
    presetColours.emplace_back("#5E0F54");
    presetColours.emplace_back("#FE2065");
    presetColours.emplace_back("#5BB4D2");
    presetColours.emplace_back("#3F4204");
    presetColours.emplace_back("#521839");
    presetColours.emplace_back("#9A7706");
    presetColours.emplace_back("#77AB8C");
    presetColours.emplace_back("#105E04");
    presetColours.emplace_back("#98290F");
    presetColours.emplace_back("#B849D4");
    presetColours.emplace_back("#FC8426");
    presetColours.emplace_back("#341B03");
    presetColours.emplace_back("#E3278C");
    presetColours.emplace_back("#F28F93");
    presetColours.emplace_back("#D1A21F");
    presetColours.emplace_back("#277E46");
    presetColours.emplace_back("#285C60");
    presetColours.emplace_back("#76B945");
    presetColours.emplace_back("#E75D65");
    presetColours.emplace_back("#84ADDC");
    presetColours.emplace_back("#153C2B");
    presetColours.emplace_back("#FD10D9");
    presetColours.emplace_back("#C095D5");
    presetColours.emplace_back("#052B48");
    presetColours.emplace_back("#B365FC");
    presetColours.emplace_back("#97AA75");
    presetColours.emplace_back("#C78C9C");
    presetColours.emplace_back("#FD4838");
    presetColours.emplace_back("#F181E2");
    presetColours.emplace_back("#815A1A");
    presetColours.emplace_back("#BB2093");
    presetColours.emplace_back("#691822");
    presetColours.emplace_back("#C41A12");
    presetColours.emplace_back("#728A1F");
    presetColours.emplace_back("#375B73");
    presetColours.emplace_back("#97022C");
    presetColours.emplace_back("#95B44D");
    presetColours.emplace_back("#EB8DBB");
    presetColours.emplace_back("#83ACAB");
    presetColours.emplace_back("#E37D51");
    presetColours.emplace_back("#D78A68");
    presetColours.emplace_back("#4A41A2");
    presetColours.emplace_back("#8A0C79");
    presetColours.emplace_back("#133102");
    presetColours.emplace_back("#237A78");
    presetColours.emplace_back("#ADB03B");
    presetColours.emplace_back("#289E26");
    presetColours.emplace_back("#7683EC");
    presetColours.emplace_back("#4E1E04");
    presetColours.emplace_back("#BB17B2");
    presetColours.emplace_back("#EB6A81");
    presetColours.emplace_back("#47B4E8");
    presetColours.emplace_back("#0A6191");
    presetColours.emplace_back("#4EADB2");
    presetColours.emplace_back("#442965");
    presetColours.emplace_back("#FE784B");
    presetColours.emplace_back("#55BD8D");
    presetColours.emplace_back("#742B03");
    presetColours.emplace_back("#8C38AA");
    presetColours.emplace_back("#F758A6");
    presetColours.emplace_back("#A32526");
    presetColours.emplace_back("#442C2E");
    presetColours.emplace_back("#F06A97");
    presetColours.emplace_back("#3A1527");
    presetColours.emplace_back("#503509");
    presetColours.emplace_back("#2A67B4");
    presetColours.emplace_back("#243644");
    presetColours.emplace_back("#A74006");
    presetColours.emplace_back("#335900");
    presetColours.emplace_back("#A07484");
    presetColours.emplace_back("#490216");
    presetColours.emplace_back("#B19BCB");
    presetColours.emplace_back("#75B75A");
    presetColours.emplace_back("#BE71EB");
    presetColours.emplace_back("#024A2E");
    presetColours.emplace_back("#A097AB");
    presetColours.emplace_back("#7A287E");
    presetColours.emplace_back("#6A1444");
    presetColours.emplace_back("#212449");
    presetColours.emplace_back("#B07017");
    presetColours.emplace_back("#227D57");
    presetColours.emplace_back("#1B8CAF");
    presetColours.emplace_back("#016438");
    presetColours.emplace_back("#EA64CF");
    presetColours.emplace_back("#B5310E");
    presetColours.emplace_back("#B00765");
    presetColours.emplace_back("#5F42B3");
    presetColours.emplace_back("#EF9649");
    presetColours.emplace_back("#25717F");
    presetColours.emplace_back("#BCA309");
    presetColours.emplace_back("#FA35A6");
    presetColours.emplace_back("#F63D54");
    presetColours.emplace_back("#E83D6C");
    presetColours.emplace_back("#8362F2");
    presetColours.emplace_back("#33BC4A");
    presetColours.emplace_back("#194A85");
    presetColours.emplace_back("#E24215");
    presetColours.emplace_back("#6D71FE");
    presetColours.emplace_back("#3E52AF");
    presetColours.emplace_back("#1E9E89");
    presetColours.emplace_back("#740860");
    presetColours.emplace_back("#4B7BEE");
    presetColours.emplace_back("#8742C0");
    presetColours.emplace_back("#DD8EC6");
    presetColours.emplace_back("#CD202C");
    presetColours.emplace_back("#FD82C2");
    presetColours.emplace_back("#3C2874");
    presetColours.emplace_back("#F9742B");
    presetColours.emplace_back("#013B10");
    presetColours.emplace_back("#D12867");
    presetColours.emplace_back("#F743C3");
    presetColours.emplace_back("#B98EEC");
    presetColours.emplace_back("#D260EC");
    presetColours.emplace_back("#671C06");
    presetColours.emplace_back("#37A968");
    presetColours.emplace_back("#3B9529");
    presetColours.emplace_back("#2A0E33");
    presetColours.emplace_back("#51B237");
    presetColours.emplace_back("#95B61B");
    presetColours.emplace_back("#B195E2");
    presetColours.emplace_back("#68B49A");
    presetColours.emplace_back("#182339");
    presetColours.emplace_back("#FC4822");
    presetColours.emplace_back("#D79621");
    presetColours.emplace_back("#90761B");
    presetColours.emplace_back("#777315");
    presetColours.emplace_back("#E389E9");
    presetColours.emplace_back("#35BD64");
    presetColours.emplace_back("#C17910");
    presetColours.emplace_back("#3386ED");
    presetColours.emplace_back("#E82C2E");
    presetColours.emplace_back("#AC925F");
    presetColours.emplace_back("#F227C8");
    presetColours.emplace_back("#F43E67");
    presetColours.emplace_back("#55AEEB");
    presetColours.emplace_back("#F518E3");
    presetColours.emplace_back("#AB0643");
    presetColours.emplace_back("#8DA1F3");
    presetColours.emplace_back("#5C9C14");
    presetColours.emplace_back("#381F27");
    presetColours.emplace_back("#6BB7B5");
    presetColours.emplace_back("#9842BE");
    presetColours.emplace_back("#4897D6");
    presetColours.emplace_back("#8958E4");
    presetColours.emplace_back("#8F0065");
    presetColours.emplace_back("#A10A5E");
    presetColours.emplace_back("#076315");
    presetColours.emplace_back("#FA5EF9");
    presetColours.emplace_back("#A33402");
    presetColours.emplace_back("#A0ABC4");
    presetColours.emplace_back("#2B6EFE");
    presetColours.emplace_back("#9A9EE7");

    return presetColours;
}



//This function will convert a colour to its SVG name, if one exists, or the hex value otherwise.
QString getColourName(QColor colour)
{
    if (colour == QColor(240, 248, 255)) return "aliceblue";
    if (colour == QColor(250, 235, 215)) return "antiquewhite";
    if (colour == QColor(  0, 255, 255)) return "aqua";
    if (colour == QColor(127, 255, 212)) return "aquamarine";
    if (colour == QColor(240, 255, 255)) return "azure";
    if (colour == QColor(245, 245, 220)) return "beige";
    if (colour == QColor(255, 228, 196)) return "bisque";
    if (colour == QColor(  0,   0,   0)) return "black";
    if (colour == QColor(255, 235, 205)) return "blanchedalmond";
    if (colour == QColor(  0,   0, 255)) return "blue";
    if (colour == QColor(138,  43, 226)) return "blueviolet";
    if (colour == QColor(165,  42,  42)) return "brown";
    if (colour == QColor(222, 184, 135)) return "burlywood";
    if (colour == QColor( 95, 158, 160)) return "cadetblue";
    if (colour == QColor(127, 255,   0)) return "chartreuse";
    if (colour == QColor(210, 105,  30)) return "chocolate";
    if (colour == QColor(255, 127,  80)) return "coral";
    if (colour == QColor(100, 149, 237)) return "cornflowerblue";
    if (colour == QColor(255, 248, 220)) return "cornsilk";
    if (colour == QColor(220,  20,  60)) return "crimson";
    if (colour == QColor(  0, 255, 255)) return "cyan";
    if (colour == QColor(  0,   0, 139)) return "darkblue";
    if (colour == QColor(  0, 139, 139)) return "darkcyan";
    if (colour == QColor(184, 134,  11)) return "darkgoldenrod";
    if (colour == QColor(  0, 100,   0)) return "darkgreen";
    if (colour == QColor(169, 169, 169)) return "darkgrey";
    if (colour == QColor(189, 183, 107)) return "darkkhaki";
    if (colour == QColor(139,   0, 139)) return "darkmagenta";
    if (colour == QColor( 85, 107,  47)) return "darkolivegreen";
    if (colour == QColor(255, 140,   0)) return "darkorange";
    if (colour == QColor(153,  50, 204)) return "darkorchid";
    if (colour == QColor(139,   0,   0)) return "darkred";
    if (colour == QColor(233, 150, 122)) return "darksalmon";
    if (colour == QColor(143, 188, 143)) return "darkseagreen";
    if (colour == QColor( 72,  61, 139)) return "darkslateblue";
    if (colour == QColor( 47,  79,  79)) return "darkslategrey";
    if (colour == QColor(  0, 206, 209)) return "darkturquoise";
    if (colour == QColor(148,   0, 211)) return "darkviolet";
    if (colour == QColor(255,  20, 147)) return "deeppink";
    if (colour == QColor(  0, 191, 255)) return "deepskyblue";
    if (colour == QColor(105, 105, 105)) return "dimgrey";
    if (colour == QColor( 30, 144, 255)) return "dodgerblue";
    if (colour == QColor(178,  34,  34)) return "firebrick";
    if (colour == QColor(255, 250, 240)) return "floralwhite";
    if (colour == QColor( 34, 139,  34)) return "forestgreen";
    if (colour == QColor(255,   0, 255)) return "fuchsia";
    if (colour == QColor(220, 220, 220)) return "gainsboro";
    if (colour == QColor(248, 248, 255)) return "ghostwhite";
    if (colour == QColor(255, 215,   0)) return "gold";
    if (colour == QColor(218, 165,  32)) return "goldenrod";
    if (colour == QColor(128, 128, 128)) return "grey";
    if (colour == QColor(  0, 128,   0)) return "green";
    if (colour == QColor(173, 255,  47)) return "greenyellow";
    if (colour == QColor(240, 255, 240)) return "honeydew";
    if (colour == QColor(255, 105, 180)) return "hotpink";
    if (colour == QColor(205,  92,  92)) return "indianred";
    if (colour == QColor( 75,   0, 130)) return "indigo";
    if (colour == QColor(255, 255, 240)) return "ivory";
    if (colour == QColor(240, 230, 140)) return "khaki";
    if (colour == QColor(230, 230, 250)) return "lavender";
    if (colour == QColor(255, 240, 245)) return "lavenderblush";
    if (colour == QColor(124, 252,   0)) return "lawngreen";
    if (colour == QColor(255, 250, 205)) return "lemonchiffon";
    if (colour == QColor(173, 216, 230)) return "lightblue";
    if (colour == QColor(240, 128, 128)) return "lightcoral";
    if (colour == QColor(224, 255, 255)) return "lightcyan";
    if (colour == QColor(250, 250, 210)) return "lightgoldenrodyellow";
    if (colour == QColor(144, 238, 144)) return "lightgreen";
    if (colour == QColor(211, 211, 211)) return "lightgrey";
    if (colour == QColor(255, 182, 193)) return "lightpink";
    if (colour == QColor(255, 160, 122)) return "lightsalmon";
    if (colour == QColor( 32, 178, 170)) return "lightseagreen";
    if (colour == QColor(135, 206, 250)) return "lightskyblue";
    if (colour == QColor(119, 136, 153)) return "lightslategrey";
    if (colour == QColor(176, 196, 222)) return "lightsteelblue";
    if (colour == QColor(255, 255, 224)) return "lightyellow";
    if (colour == QColor(  0, 255,   0)) return "lime";
    if (colour == QColor( 50, 205,  50)) return "limegreen";
    if (colour == QColor(250, 240, 230)) return "linen";
    if (colour == QColor(255,   0, 255)) return "magenta";
    if (colour == QColor(128,   0,   0)) return "maroon";
    if (colour == QColor(102, 205, 170)) return "mediumaquamarine";
    if (colour == QColor(  0,   0, 205)) return "mediumblue";
    if (colour == QColor(186,  85, 211)) return "mediumorchid";
    if (colour == QColor(147, 112, 219)) return "mediumpurple";
    if (colour == QColor( 60, 179, 113)) return "mediumseagreen";
    if (colour == QColor(123, 104, 238)) return "mediumslateblue";
    if (colour == QColor(  0, 250, 154)) return "mediumspringgreen";
    if (colour == QColor( 72, 209, 204)) return "mediumturquoise";
    if (colour == QColor(199,  21, 133)) return "mediumvioletred";
    if (colour == QColor( 25,  25, 112)) return "midnightblue";
    if (colour == QColor(245, 255, 250)) return "mintcream";
    if (colour == QColor(255, 228, 225)) return "mistyrose";
    if (colour == QColor(255, 228, 181)) return "moccasin";
    if (colour == QColor(255, 222, 173)) return "navajowhite";
    if (colour == QColor(  0,   0, 128)) return "navy";
    if (colour == QColor(253, 245, 230)) return "oldlace";
    if (colour == QColor(128, 128,   0)) return "olive";
    if (colour == QColor(107, 142,  35)) return "olivedrab";
    if (colour == QColor(255, 165,   0)) return "orange";
    if (colour == QColor(255,  69,   0)) return "orangered";
    if (colour == QColor(218, 112, 214)) return "orchid";
    if (colour == QColor(238, 232, 170)) return "palegoldenrod";
    if (colour == QColor(152, 251, 152)) return "palegreen";
    if (colour == QColor(175, 238, 238)) return "paleturquoise";
    if (colour == QColor(219, 112, 147)) return "palevioletred";
    if (colour == QColor(255, 239, 213)) return "papayawhip";
    if (colour == QColor(255, 218, 185)) return "peachpuff";
    if (colour == QColor(205, 133,  63)) return "peru";
    if (colour == QColor(255, 192, 203)) return "pink";
    if (colour == QColor(221, 160, 221)) return "plum";
    if (colour == QColor(176, 224, 230)) return "powderblue";
    if (colour == QColor(128,   0, 128)) return "purple";
    if (colour == QColor(255,   0,   0)) return "red";
    if (colour == QColor(188, 143, 143)) return "rosybrown";
    if (colour == QColor( 65, 105, 225)) return "royalblue";
    if (colour == QColor(139,  69,  19)) return "saddlebrown";
    if (colour == QColor(250, 128, 114)) return "salmon";
    if (colour == QColor(244, 164,  96)) return "sandybrown";
    if (colour == QColor( 46, 139,  87)) return "seagreen";
    if (colour == QColor(255, 245, 238)) return "seashell";
    if (colour == QColor(160,  82,  45)) return "sienna";
    if (colour == QColor(192, 192, 192)) return "silver";
    if (colour == QColor(135, 206, 235)) return "skyblue";
    if (colour == QColor(106,  90, 205)) return "slateblue";
    if (colour == QColor(112, 128, 144)) return "slategrey";
    if (colour == QColor(255, 250, 250)) return "snow";
    if (colour == QColor(  0, 255, 127)) return "springgreen";
    if (colour == QColor( 70, 130, 180)) return "steelblue";
    if (colour == QColor(210, 180, 140)) return "tan";
    if (colour == QColor(  0, 128, 128)) return "teal";
    if (colour == QColor(216, 191, 216)) return "thistle";
    if (colour == QColor(255,  99,  71)) return "tomato";
    if (colour == QColor( 64, 224, 208)) return "turquoise";
    if (colour == QColor(238, 130, 238)) return "violet";
    if (colour == QColor(245, 222, 179)) return "wheat";
    if (colour == QColor(255, 255, 255)) return "white";
    if (colour == QColor(245, 245, 245)) return "whitesmoke";
    if (colour == QColor(255, 255,   0)) return "yellow";
    if (colour == QColor(154, 205,  50)) return "yellowgreen";

    return colour.name();
}

ColorMap colorMapFromName(const QString& name) {
    if (name == "viridis")
        return Viridis;
    if (name == "parula")
        return Parula;
    if (name == "heat")
        return Heat;
    if (name == "jet")
        return Jet;
    if (name == "turbo")
        return Turbo;
    if (name == "hot")
        return Hot;
    if (name == "gray")
        return Gray;
    if (name == "magma")
        return Magma;
    if (name == "inferno")
        return Inferno;
    if (name == "plasma")
        return Plasma;
    if (name == "cividis")
        return Cividis;
    if (name == "github")
        return Github;
    if (name == "cubehelix")
        return Cubehelix;

    return Viridis;
}

tinycolormap::ColormapType colorMap(ColorMap colorMap) {
    switch (colorMap) {
        default:
        case Viridis:
            return tinycolormap::ColormapType::Viridis;
        case Parula:
            return tinycolormap::ColormapType::Parula;
        case Heat:
            return tinycolormap::ColormapType::Heat;
        case Jet:
            return tinycolormap::ColormapType::Jet;
        case Turbo:
            return tinycolormap::ColormapType::Turbo;
        case Hot:
            return tinycolormap::ColormapType::Hot;
        case Gray:
            return tinycolormap::ColormapType::Gray;
        case Magma:
            return tinycolormap::ColormapType::Magma;
        case Inferno:
            return tinycolormap::ColormapType::Inferno;
        case Plasma:
            return tinycolormap::ColormapType::Plasma;
        case Cividis:
            return tinycolormap::ColormapType::Cividis;
        case Github:
            return tinycolormap::ColormapType::Github;
        case Cubehelix:
            return tinycolormap::ColormapType::Cubehelix;
    }

    return tinycolormap::ColormapType::Viridis;
}
QString getColorMapName(ColorMap colorMap) {
    switch (colorMap) {
        default:
        case Viridis:
            return "viridis";
        case Parula:
            return "parula";
        case Heat:
            return "heat";
        case Jet:
            return "jet";
        case Turbo:
            return "turbo";
        case Hot:
            return "hot";
        case Gray:
            return "gray";
        case Magma:
            return "magma";
        case Inferno:
            return "inferno";
        case Plasma:
            return "plasma";
        case Cividis:
            return "cividis";
        case Github:
            return "github";
        case Cubehelix:
            return "cubehelix";
    }

    return "viridis";
}