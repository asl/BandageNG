// Copyright 2023 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage-NG is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage-NG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#include "settings.h"
#include "commoncommandlinefunctions.h"

#include "graph/graphscope.h"
#include "graph/nodecolorer.h"
#include "program/globals.h"
#include "program/settings.h"

#include <QString>
#include <QColor>
#include <string>
#include <sstream>

namespace CLI {
std::istringstream &operator>>(std::istringstream &in, QString &val) {
    std::string s;
    in >> s;
    val = QString::fromStdString(s);

    return in;
}

std::istringstream &operator>>(std::istringstream &in, QColor &val) {
    std::string s;
    in >> s;
    val = QColor::fromString(s);

    return in;
}

}

#include <CLI/CLI.hpp>

namespace CLI {
struct QColorValidator : public Validator {
    QColorValidator() {
        name_ = "QCOLOR";
        func_ = [](const std::string &str) {
            if (!QColor::isValidColorName(str))
                return std::string("This is not a valid color name: " + str);
            return std::string{};
        };
    }
};

namespace detail {

template <>
constexpr const char *type_name<IntSetting>() {
    return "INT";
}

template <>
constexpr const char *type_name<FloatSetting>() {
    return "FLOAT";
}

template <>
constexpr const char *type_name<SciNotSetting>() {
    return "SCINOT";
}

template <>
constexpr const char *type_name<GraphScope>() {
    return "SCOPE";
}

template <>
constexpr const char *type_name<QColor>() {
    return "COLOR";
}

template<>
struct classify_object<QString> {
    static constexpr object_category value{object_category::other};
};

}
}

bool lexical_cast(const std::string &input, IntSetting &val) {
    if (input == "off")
        val.on = false;
    else {
        val.on = true;
        return CLI::detail::lexical_cast(input, val.val);
    }

    return true;
}

bool lexical_cast(const std::string &input, FloatSetting &val) {
    if (input == "off")
        val.on = false;
    else {
        val.on = true;
        return CLI::detail::lexical_cast(input, val.val);
    }

    return true;
}

bool lexical_cast(const std::string &input, SciNot &val) {
    if (!SciNot::isValidSciNotString(input.c_str()))
        return false;

    val = SciNot(input.c_str());

    return true;
}

bool lexical_cast(const std::string &input, SciNotSetting &val) {
    if (input == "off")
        val.on = false;
    else {
        val.on = true;
        return CLI::detail::lexical_cast(input, val.val);
    }

    return true;
}

static CLI::App *addGraphScopeSettings(CLI::App &app) {
    auto *scope = app.add_option_group("Graph scope",
                                       "These settings control the graph scope. "
                                       "If the aroundnodes scope is used, then the --nodes option must also be used. "
                                       "If the aroundblast scope is used, a BLAST query must be given with the --query option.");

    scope->add_option("--scope", g_settings->graphScope, "Graph scope, from one of the following options: entire, aroundnodes, aroundblast, depthrange")
            ->transform(CLI::CheckedTransformer(
                std::vector<std::pair<std::string, GraphScope>>{
                    {"entire", GraphScope::WHOLE_GRAPH},
                    {"aroundnodes", GraphScope::AROUND_NODE},
                    // FIXME: paths!
                    {"aroundblast", GraphScope::AROUND_BLAST_HITS},
                    {"depthrange", GraphScope::DEPTH_RANGE}}))
            ->default_val("entire");
    scope->add_flag("--exact,!--partial", g_settings->startingNodesExactMatch, "Choose between exact or partial node name matching (default: exact)");
    scope->add_option("--distance", g_settings->nodeDistance, "The number of node steps away to draw for the aroundnodes and aroundblast scopes")
            ->default_str(getRangeAndDefault(g_settings->nodeDistance).toStdString()); // FIXME
    scope->add_option("--mindepth", g_settings->minDepthRange, "The minimum allowed depth for the depthrange scope")
            ->default_str(getRangeAndDefault(g_settings->minDepthRange).toStdString()); // FIXME
    scope->add_option("--maxdepth", g_settings->maxDepthRange, "The maximum allowed depth for the depthrange scope")
            ->default_str(getRangeAndDefault(g_settings->maxDepthRange).toStdString()); // FIXME
    scope->add_option("--nodes", g_settings->startingNodes, "A comma-separated list of starting nodes for the aroundnodes scope (default: none)");

    return scope;
}

static CLI::App *addGraphSizeSettings(CLI::App &app) {
    auto *size = app.add_option_group("Graph size");
    size->add_option("--nodelen", g_settings->manualNodeLengthPerMegabase, "Node length per megabase")
            ->default_str(getRangeAndDefault(g_settings->manualNodeLengthPerMegabase, "auto").toStdString());
    size->add_option("--minnodlen", g_settings->minimumNodeLength, "Minimum node length")
            ->default_str(getRangeAndDefault(g_settings->minimumNodeLength).toStdString());
    size->add_option("--edgelen", g_settings->edgeLength, "Edge length")
            ->default_str(getRangeAndDefault(g_settings->edgeLength).toStdString());
    size->add_option("--edgewidth", g_settings->edgeLength, "Edge width")
            ->default_str(getRangeAndDefault(g_settings->edgeWidth).toStdString());
    size->add_option("--doubsep", g_settings->doubleModeNodeSeparation, "Double mode node separation")
            ->default_str(getRangeAndDefault(g_settings->doubleModeNodeSeparation).toStdString());
    size->callback([size]() {
        if (size->count("--nodelen"))
            g_settings->nodeLengthMode = MANUAL_NODE_LENGTH;
    });

    return size;
}

static CLI::App *addGraphLayoutSettings(CLI::App &app) {
    auto *layout = app.add_option_group("Graph layout");
    layout->add_option("--nodseglen", g_settings->nodeSegmentLength, "Node segment length")
            ->default_str(getRangeAndDefault(g_settings->nodeSegmentLength).toStdString());
    layout->add_option("--iter", g_settings->graphLayoutQuality, "Graph layout iterations")
            ->default_str(getRangeAndDefault(g_settings->graphLayoutQuality).toStdString())
            ->check(CLI::Range(0, 4));
    layout->add_flag("--linear", g_settings->linearLayout, "Linear graph layout")
            ->capture_default_str();

    return layout;
}

static CLI::App *addGraphAppearanceSettings(CLI::App &app) {
    auto *ga = app.add_option_group("Graph appearance");
    ga->add_option("--edgecol", g_settings->edgeColour, "Color for edges")
            ->check(CLI::QColorValidator())
            ->default_str(getDefaultColour(g_settings->edgeColour).toStdString());
    ga->add_option("--outcol", g_settings->outlineColour, "Color for node outlines")
            ->check(CLI::QColorValidator())
            ->default_str(getDefaultColour(g_settings->outlineColour).toStdString());
    ga->add_option("--outline", g_settings->outlineThickness, "Node outline thickness")
            ->default_str(getRangeAndDefault(g_settings->outlineThickness).toStdString());
    ga->add_option("--selcol", g_settings->selectionColour, "Color for selections")
            ->check(CLI::QColorValidator())
            ->default_str(getDefaultColour(g_settings->selectionColour).toStdString());
    ga->add_flag("--aa,!--noaa", g_settings->antialiasing, "Enable / disable antialiasing")
            ->capture_default_str();
    ga->add_flag("--single,!--double", g_settings->doubleMode, "Draw graph in single / double mode")
            ->capture_default_str();
    ga->add_flag("--singlearr", g_settings->arrowheadsInSingleMode, "Show node arrowheads in single mode")
            ->capture_default_str();

    return ga;
}

static CLI::App *addTextAppearanceSettings(CLI::App &app) {
    auto *ta = app.add_option_group("Text appearance");
    ta->add_option("--textcol", g_settings->textColour, "Color for label text")
            ->default_str(getDefaultColour(g_settings->textColour).toStdString());
    ta->add_option("--toutcol", g_settings->textOutlineColour, "Color for text outline")
            ->default_str(getDefaultColour(g_settings->textOutlineColour).toStdString());
    ta->add_option("--toutline", g_settings->textOutlineThickness, "Surround text with an outline with this thickness")
            ->default_str( getRangeAndDefault(g_settings->textOutlineThickness).toStdString());
    ta->add_flag("--centre", g_settings->positionTextNodeCentre, "Node labels appear at the centre of the node")
            ->capture_default_str();

    return ta;
}

static CLI::App *addNodeWidthsSettings(CLI::App &app) {
    auto *nw = app.add_option_group("Node width",
                                    "Node widths are determined using the following formula:\n"
                                    "a*b*((c/d)^e-1)+1\n"
                                    "  a = average node width\n"
                                    "  b = depth effect on width\n"
                                    "  c = node dept\n"
                                    "  d = mean depth\n"
                                    "  e = power of depth effect on width\n"
                                    );
    nw->add_option("--nodewidth", g_settings->averageNodeWidth, "Average node width")
            ->default_str(getRangeAndDefault(g_settings->averageNodeWidth).toStdString());
    nw->add_option("--depwidth", g_settings->depthEffectOnWidth, "Depth effect on width")
            ->default_str(getRangeAndDefault(g_settings->depthEffectOnWidth).toStdString());
    nw->add_option("--deppower", g_settings->depthPower, "Power of depth effect on width")
            ->default_str(getRangeAndDefault(g_settings->depthPower).toStdString());

    return nw;
}

static CLI::App *addNodeLabelsSettings(CLI::App &app) {
    auto *nl = app.add_option_group("Node labels");
    nl->add_option("--csv", g_settings->csvFilename, "A CSV file with additional info")
            ->check(CLI::ExistingFile);
    nl->add_flag("--names", g_settings->displayNodeNames, "Label nodes with name");
    nl->add_flag("--lengths", g_settings->displayNodeLengths, "Label nodes with length");
    nl->add_flag("--depth", g_settings->displayNodeDepth, "Label nodes with depth");
    // For backward compatibility we allow enabling blast annotation text by "--blasthits" option.
    // To be removed when we can set up annotations from CLI.
    nl->add_flag("--blasthits", g_settings->defaultBlastAnnotationSetting.showText, "Label BLAST hits");
    auto *fs = nl->add_option("--fontsize", "Font size for node labels")
               ->default_str(getRangeAndDefault(1, 100, g_settings->labelFont.pointSize()).toStdString());
    nl->callback([fs](){
        if (fs->count()) {
            QFont font = g_settings->labelFont;
            font.setPointSize(fs->as<int>());
            g_settings->labelFont = font;
        }
    });

    return nl;
}

static CLI::App *addNodeColorsSettings(CLI::App &app) {
    auto *nc = app.add_option_group("Node colors");
    auto *co =
            nc->add_option("--colour", "Node colouring scheme")
            ->transform(CLI::CheckedTransformer(
                std::vector<std::pair<std::string, NodeColorScheme>>{
                    {"random",  RANDOM_COLOURS},
                    {"uniform", UNIFORM_COLOURS},
                    {"depth",   DEPTH_COLOUR},
                    {"custom",  CUSTOM_COLOURS},
                    {"gc",      GC_CONTENT},
                    {"gfa",     TAG_VALUE},
                    {"csv",     CSV_COLUMN},
                }))
            ->default_val("random");

    nc->callback([co]() {
        if (co->count())
            g_settings->initializeColorer(co->as<NodeColorScheme>());
    });

    return nc;
}

static CLI::App *addRandomColorsSettings(CLI::App &app) {
    auto *rc = app.add_option_group("Random color scheme", "These settings only apply when the random colour scheme is used.");
    rc->add_option("--ransatpos", g_settings->randomColourPositiveSaturation, "Positive node saturation")
            ->default_str(getRangeAndDefault(g_settings->randomColourPositiveSaturation).toStdString());
    rc->add_option("--ransatneg", g_settings->randomColourNegativeSaturation, "Negative node saturation")
            ->default_str( getRangeAndDefault(g_settings->randomColourNegativeSaturation).toStdString());
    rc->add_option("--ranligpos", g_settings->randomColourPositiveLightness, "Positive node lightness")
            ->default_str( getRangeAndDefault(g_settings->randomColourPositiveLightness).toStdString());
    rc->add_option("--ranligneg", g_settings->randomColourNegativeLightness, "Negative node lightness")
            ->default_str(getRangeAndDefault(g_settings->randomColourNegativeLightness).toStdString());
    rc->add_option("--ranopapos", g_settings->randomColourPositiveOpacity, "Positive node opacity")
            ->default_str(getRangeAndDefault(g_settings->randomColourPositiveOpacity).toStdString());
    rc->add_option("--ranopaneg", g_settings->randomColourNegativeOpacity, "Negative node opacity")
            ->default_str(getRangeAndDefault(g_settings->randomColourNegativeOpacity).toStdString());

    return rc;
}

static CLI::App *addUniformColorsSettings(CLI::App &app) {
    auto *uc = app.add_option_group("Uniform color scheme", "These settings only apply when the uniform colour scheme is used.");
    uc->add_option("--unicolpos", g_settings->uniformPositiveNodeColour, "Positive node colour")
            ->default_str(getDefaultColour(g_settings->uniformPositiveNodeColour).toStdString());
    uc->add_option("--unicolneg", g_settings->uniformNegativeNodeColour, "Negative node colour")
            ->default_str(getDefaultColour(g_settings->uniformNegativeNodeColour).toStdString());
    uc->add_option("--unicolspe", g_settings->uniformNodeSpecialColour, "Special node colour ")
            ->default_str(getDefaultColour(g_settings->uniformNodeSpecialColour).toStdString());

    return uc;
}

static CLI::App *addDepthColorsSettings(CLI::App &app) {
    auto *dc = app.add_option_group("Depth / GC color scheme", "These settings only apply when the depth / GC colour scheme is used.");

    //dc->add_option("--colormap <mapname>  Color map to use "->default_str(getDefaultColorMap(g_settings->colorMap);
    dc->add_option("--depvallow", g_settings->lowDepthValue, "Low depth value ")
            ->default_str(getRangeAndDefault(g_settings->lowDepthValue, "auto").toStdString());
    dc->add_option("--depvalhi", g_settings->highDepthValue, "High depth value ")
            ->default_str(getRangeAndDefault(g_settings->highDepthValue, "auto").toStdString());
    dc->callback([dc]() {
        if (dc->count("--depvallow") || dc->count("--depvalhi"))
            g_settings->autoDepthValue = false;
    });

    return dc;
}

static CLI::App *addBlastSearchSettings(CLI::App &app) {
    auto *bs = app.add_option_group("BLAST search");

    bs->add_option("--query", g_settings->blastQueryFilename, "A FASTA file of either nucleotide or protein sequences to be used as BLAST queries")
            ->check(CLI::ExistingFile);

    bs->add_option("--blastp", g_settings->blastSearchParameters,
                   "Parameters to be used by blastn and tblastn when conducting a BLAST search in Bandage-NG.\n"
                   "Format BLAST parameters exactly as they would be used for blastn/tblastn on the command line, and enclose them in quotes.");

    bs->add_option("--alfilter", g_settings->blastAlignmentLengthFilter,
                   "Alignment length filter for BLAST hits. Hits with shorter alignments will be excluded ")
            ->default_str(getRangeAndDefault(g_settings->blastAlignmentLengthFilter).toStdString());
    bs->add_option("--qcfilter", g_settings->blastQueryCoverageFilter,
                   "Query coverage filter for BLAST hits. Hits with less coverage will be excluded ")
            ->default_str(getRangeAndDefault(g_settings->blastQueryCoverageFilter).toStdString());
    bs->add_option("--ifilter", g_settings->blastIdentityFilter,
                   "Identity filter for BLAST hits. Hits with less identity will be excluded ")
            ->default_str(getRangeAndDefault(g_settings->blastIdentityFilter).toStdString());
    bs->add_option("--evfilter", g_settings->blastEValueFilter,
                   "E-value filter for BLAST hits. Hits with larger e-values will be excluded ")
            ->default_str(getRangeAndDefault(g_settings->blastEValueFilter).toStdString());
    bs->add_option("--bsfilter", g_settings->blastBitScoreFilter,
                   "Bit score filter for BLAST hits. Hits with lower bit scores will be excluded ")
            ->default_str(getRangeAndDefault(g_settings->blastBitScoreFilter).toStdString());

    return bs;
}

static CLI::App *addQueryPathsSettings(CLI::App &app) {
    auto *bqp = app.add_option_group("BLAST query paths",
                                     "These settings control how Bandage-NG searches for query paths after conducting a BLAST search");

    bqp->add_option("--pathnodes", g_settings->maxQueryPathNodes,
                    "The number of allowed nodes in a BLAST query path ")
            ->default_str(getRangeAndDefault(g_settings->maxQueryPathNodes).toStdString());
    bqp->add_option("--minpatcov", g_settings->minQueryCoveredByPath,
                    "Minimum fraction of a BLAST query which must be covered by a query path ")
            ->default_str(getRangeAndDefault(g_settings->minQueryCoveredByPath).toStdString());
    bqp->add_option("--minhitcov", g_settings->minQueryCoveredByHits,
                    "Minimum fraction of a BLAST query which must be covered by BLAST hits in a query path ")
            ->default_str(getRangeAndDefault(g_settings->minQueryCoveredByHits).toStdString());
    bqp->add_option("--minmeanid", g_settings->minMeanHitIdentity,
                    "Minimum mean identity of BLAST hits in a query path ")
            ->default_str(getRangeAndDefault(g_settings->minMeanHitIdentity).toStdString());
    bqp->add_option("--maxevprod", g_settings->maxEValueProduct,
                    "Maximum e-value product for all BLAST hits in a query path ")
            ->default_str(getRangeAndDefault(g_settings->maxEValueProduct).toStdString());
    bqp->add_option("--minpatlen", g_settings->minLengthPercentage,
                    "Minimum allowed relative path length as compared to the query ")
            ->default_str(getRangeAndDefault(g_settings->minLengthPercentage).toStdString());
    bqp->add_option("--maxpatlen", g_settings->maxLengthPercentage,
                    "Maximum allowed relative path length as compared to the query ")
            ->default_str(getRangeAndDefault(g_settings->maxLengthPercentage).toStdString());
    bqp->add_option("--minlendis", g_settings->minLengthBaseDiscrepancy,
                    "Minimum allowed length discrepancy (in bases) between a BLAST query and its path in the graph ")
            ->default_str(getRangeAndDefault(g_settings->minLengthBaseDiscrepancy).toStdString());
    bqp->add_option("--maxlendis", g_settings->maxLengthBaseDiscrepancy,
                    "Maximum allowed length discrepancy (in bases) between a BLAST query and its path in the graph ")
            ->default_str(getRangeAndDefault(g_settings->maxLengthBaseDiscrepancy).toStdString());

    return bqp;
}

CLI::App *addSettings(CLI::App &app) {
    auto *scope = addGraphScopeSettings(app);
    auto *size = addGraphSizeSettings(app);
    auto *layout = addGraphLayoutSettings(app);
    auto *ga = addGraphAppearanceSettings(app);
    auto *ta = addTextAppearanceSettings(app);
    auto *nw = addNodeWidthsSettings(app);
    auto *nl = addNodeLabelsSettings(app);
    auto *nc = addNodeColorsSettings(app);
    auto *rc = addRandomColorsSettings(app);
    auto *uc = addUniformColorsSettings(app);
    auto *dc = addDepthColorsSettings(app);
    auto *bs = addBlastSearchSettings(app);
    auto *bqp = addQueryPathsSettings(app);

    return &app;
}
