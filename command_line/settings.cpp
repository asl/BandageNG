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
#include "CLI/Error.hpp"
#include "commoncommandlinefunctions.h"

#include "graph/graphscope.h"
#include "graph/nodecolorer.h"
#include "program/colormap.h"
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
#if QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
    val.setNamedColor(s.c_str());
#else
    val = QColor::fromString(s);
#endif                

    return in;
}

}

#include <CLI/CLI.hpp>

namespace CLI {
struct QColorValidator : public Validator {
    QColorValidator() {
        name_ = "QCOLOR";
        func_ = [](const std::string &str) {
#if QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
            if (!QColor::isValidColor(str.c_str()))
#else
            if (!QColor::isValidColorName(str))
#endif                
                return std::string("This is not a valid color name: " + str);
            return std::string{};
        };
    }
};


class RangeOrOff : public Validator {
  public:
    template <typename T>
    RangeOrOff(T min_val, T max_val, bool allow_off,
               const std::string &validator_name = std::string{})
            : Validator(validator_name) {
        if (validator_name.empty()) {
            std::stringstream out;
            out << detail::type_name<T>() << " in [" << min_val << " - " << max_val << "]";
            if (allow_off)
                out << ", or \"off\"";
            description(out.str());
        }

        func_ = [min_val, max_val, allow_off](std::string &input) {
            if (input == "off")
                return allow_off ? std::string{} : std::string{"value cannot be off"};

            using CLI::detail::lexical_cast;
            T val;
            bool converted = lexical_cast(input, val);
            if ((!converted) || (val < min_val || val > max_val)) {
                std::stringstream out;
                out << "Value " << input << " not in range [";
                out << min_val << " - " << max_val << "]";
                return out.str();
            }
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
        if (!SciNot::isValidSciNotString(input.c_str()))
            return false;

        val.on = true;
        val.val = SciNot(input.c_str());
        return true;
    }

    return true;
}

template<class Setting>
static CLI::Option *add_setting(CLI::App &app,
                                std::string name,
                                Setting &setting,
                                std::string description, bool allowOff = false) {
    auto *opt = app.add_option(std::move(name), setting, std::move(description))
                ->check(CLI::RangeOrOff(setting.min, setting.max, allowOff));
    if (setting.on)
        opt->capture_default_str();
    else
        opt->default_str("off");

    return opt;
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
    add_setting(*scope, "--distance", g_settings->nodeDistance, "The number of node steps away to draw for the aroundnodes and aroundblast scopes");
    add_setting(*scope, "--mindepth", g_settings->minDepthRange, "The minimum allowed depth for the depthrange scope");
    add_setting(*scope, "--maxdepth", g_settings->maxDepthRange, "The maximum allowed depth for the depthrange scope");
    scope->add_option("--query", g_settings->blastQueryFilename, "A FASTA file of either nucleotide or protein sequences to be used as BLAST queries")
            ->check(CLI::ExistingFile);
    scope->add_option("--nodes", g_settings->startingNodes, "A comma-separated list of starting nodes for the aroundnodes scope (default: none)")
            ->capture_default_str();

    scope->callback([scope]() {
        switch (g_settings->graphScope) {
            default:
                break;
            case AROUND_NODE:
                if (g_settings->startingNodes.isEmpty())
                    throw CLI::ValidationError("Bandage-NG error",
                                               "A list of starting nodes must be given with the --nodes option\nwhen the aroundnodes scope is used.");
                break;
            case AROUND_BLAST_HITS:
                if (g_settings->blastQueryFilename.isEmpty())
                    throw CLI::ValidationError("Bandage-NG error",
                                               "A BLAST query must be given with the --query option when the\naroundblast scope is used.");
                break;
            case DEPTH_RANGE:
                if (!scope->count("--mindepth") || !scope->count("--maxdepth"))
                    throw CLI::ValidationError("Bandage-NG error",
                                               "A depth range must be given with the --mindepth and\n--maxdepth options when the depthrange scope is used.");
                break;
        }

        // Make sure that the min depth is less than or equal to the max read
        // depth.
        if (g_settings->minDepthRange > g_settings->maxDepthRange)
             throw CLI::ValidationError("Bandage-NG error",
                                        "the maximum depth (--maxdepth=" + std::to_string(g_settings->maxDepthRange) + ") "
                                        "must be greater than or equal to the minimum depth (--mindepth=" + std::to_string(g_settings->minDepthRange) + ").");
    });

    return scope;
}

static CLI::App *addGraphSizeSettings(CLI::App &app) {
    auto *size = app.add_option_group("Graph size");
    add_setting(*size, "--nodelen", g_settings->manualNodeLengthPerMegabase, "Node length per megabase")
            ->default_str("auto");
    add_setting(*size, "--minnodlen", g_settings->minimumNodeLength, "Minimum node length");
    add_setting(*size, "--edgelen", g_settings->edgeLength, "Edge length");
    add_setting(*size, "--edgewidth", g_settings->edgeWidth, "Edge width");
    add_setting(*size, "--doubsep", g_settings->doubleModeNodeSeparation, "Double mode node separation");
    size->callback([size]() {
        if (size->count("--nodelen"))
            g_settings->nodeLengthMode = MANUAL_NODE_LENGTH;
    });

    return size;
}

static CLI::App *addGraphLayoutSettings(CLI::App &app) {
    auto *layout = app.add_option_group("Graph layout");
    add_setting(*layout, "--nodseglen", g_settings->nodeSegmentLength, "Node segment length");
    add_setting(*layout, "--iter", g_settings->graphLayoutQuality, "Graph layout iterations");
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
    add_setting(*ga, "--outline", g_settings->outlineThickness, "Node outline thickness");
    ga->add_option("--selcol", g_settings->selectionColour, "Color for selections")
            ->check(CLI::QColorValidator())
            ->default_str(getDefaultColour(g_settings->selectionColour).toStdString());
    ga->add_flag("--aa,!--noaa", g_settings->antialiasing, "Enable / disable antialiasing")
            ->capture_default_str();
    ga->add_flag("--double,!--single", g_settings->doubleMode, "Draw graph in single / double mode")
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
    add_setting(*ta, "--toutline", g_settings->textOutlineThickness, "Surround text with an outline with this thickness");
    ta->add_flag("--centre", g_settings->positionTextNodeCentre, "Node labels appear at the centre of the node")
            ->capture_default_str();
    ta->callback([ta]() {
        g_settings->textOutline = (g_settings->textOutlineThickness == 0.0);
    });

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
    add_setting(*nw, "--nodewidth", g_settings->averageNodeWidth, "Average node width");
    add_setting(*nw, "--depwidth", g_settings->depthEffectOnWidth, "Depth effect on width");
    add_setting(*nw, "--deppower", g_settings->depthPower, "Power of depth effect on width");

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
    // Value here is dummy just to provide defaults and range
    static IntSetting fontSize(g_settings->labelFont.pointSize(), 1, 100);
    auto *fs = add_setting(*nl, "--fontsize", fontSize, "Font size for node labels");
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
    add_setting(*rc, "--ransatpos", g_settings->randomColourPositiveSaturation, "Positive node saturation");
    add_setting(*rc, "--ransatneg", g_settings->randomColourNegativeSaturation, "Negative node saturation");
    add_setting(*rc, "--ranligpos", g_settings->randomColourPositiveLightness, "Positive node lightness");
    add_setting(*rc, "--ranligneg", g_settings->randomColourNegativeLightness, "Negative node lightness");
    add_setting(*rc, "--ranopapos", g_settings->randomColourPositiveOpacity, "Positive node opacity");
    add_setting(*rc, "--ranopaneg", g_settings->randomColourNegativeOpacity, "Negative node opacity");

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

    dc->add_option("--colormap", g_settings->colorMap, "Color map to use")
            ->transform(CLI::CheckedTransformer(
                std::vector<std::pair<std::string, ColorMap>>{
                    {"viridis", Viridis},
                    {"parula",  Parula},
                    {"heat",    Heat},
                    {"jet",     Jet},
                    {"turbo",   Turbo},
                    {"hot",     Hot},
                    {"gray",    Gray},
                    {"magma",   Magma},
                    {"inferno", Inferno},
                    {"plasma",  Plasma},
                    {"cividis", Cividis},
                    {"github",  Github},
                    {"cubehelix", Cubehelix}
                }))
            ->default_str("viridis");
    add_setting(*dc, "--depvallow", g_settings->lowDepthValue, "Low depth value")
            ->default_str("auto");
    add_setting(*dc, "--depvalhi", g_settings->highDepthValue, "High depth value")
            ->default_str("auto");
    dc->callback([dc]() {
        if (dc->count("--depvallow") || dc->count("--depvalhi"))
            g_settings->autoDepthValue = false;

        if (g_settings->lowDepthValue > g_settings->highDepthValue)
            throw CLI::ValidationError("Bandage-NG error",
                                       "the maximum depth (--depvalhi=" + std::to_string(g_settings->highDepthValue) + ") "
                                       "must be greater than or equal to the minimum depth (--depvalhi=" + std::to_string(g_settings->lowDepthValue) + ").");
    });

    return dc;
}

static CLI::App *addBlastSearchSettings(CLI::App &app) {
    auto *bs = app.add_option_group("BLAST search");
    bs->add_option("--blastp", g_settings->blastSearchParameters,
                   "Parameters to be used by blastn and tblastn when conducting a BLAST search in Bandage-NG.\n"
                   "Format BLAST parameters exactly as they would be used for blastn/tblastn on the command line, and enclose them in quotes.");

    add_setting(*bs, "--alfilter", g_settings->blastAlignmentLengthFilter,
                "Alignment length filter for BLAST hits. Hits with shorter alignments will be excluded");
    add_setting(*bs, "--qcfilter", g_settings->blastQueryCoverageFilter,
                "Query coverage filter for BLAST hits. Hits with less coverage will be excluded");
    add_setting(*bs, "--ifilter", g_settings->blastIdentityFilter,
                "Identity filter for BLAST hits. Hits with less identity will be excluded");
    add_setting(*bs, "--evfilter", g_settings->blastEValueFilter,
                "E-value filter for BLAST hits. Hits with larger e-values will be excluded");
    add_setting(*bs, "--bsfilter", g_settings->blastBitScoreFilter,
                "Bit score filter for BLAST hits. Hits with lower bit scores will be excluded");

    return bs;
}

static CLI::App *addQueryPathsSettings(CLI::App &app) {
    auto *bqp = app.add_option_group("BLAST query paths",
                                     "These settings control how Bandage-NG searches for query paths after conducting a BLAST search");

    add_setting(*bqp, "--pathnodes", g_settings->maxQueryPathNodes,
                "The number of allowed nodes in a BLAST query path");
    add_setting(*bqp, "--minpatcov", g_settings->minQueryCoveredByPath,
                "Minimum fraction of a BLAST query which must be covered by a query path");
    add_setting(*bqp, "--minhitcov", g_settings->minQueryCoveredByHits,
                "Minimum fraction of a BLAST query which must be covered by BLAST hits in a query path", /* allow off */ true);
    add_setting(*bqp, "--minmeanid", g_settings->minMeanHitIdentity,
                "Minimum mean identity of BLAST hits in a query path", /* allow off */ true);
    add_setting(*bqp, "--maxevprod", g_settings->maxEValueProduct,
                "Maximum e-value product for all BLAST hits in a query path", /* allow off */ true);
    add_setting(*bqp, "--minpatlen", g_settings->minLengthPercentage,
                "Minimum allowed relative path length as compared to the query", /* allow off */ true);
    add_setting(*bqp, "--maxpatlen", g_settings->maxLengthPercentage,
                "Maximum allowed relative path length as compared to the query", /* allow off */ true);
    add_setting(*bqp, "--minlendis", g_settings->minLengthBaseDiscrepancy,
                "Minimum allowed length discrepancy (in bases) between a BLAST query and its path in the graph", /* allow off */ true);
    add_setting(*bqp, "--maxlendis", g_settings->maxLengthBaseDiscrepancy,
                "Maximum allowed length discrepancy (in bases) between a BLAST query and its path in the graph", /* allow off */ true);

    bqp->callback([bqp]() {
        // Make sure that the min path length is less than or equal to the max path
        // length.
        bool minLengthPercentageOn = g_settings->minLengthPercentage.on;
        bool maxLengthPercentageOn = g_settings->maxLengthPercentage.on;
        double minLengthPercentage = g_settings->minLengthPercentage;
        double maxLengthPercentage = g_settings->maxLengthPercentage;
        if (minLengthPercentageOn && maxLengthPercentageOn &&
            minLengthPercentage > maxLengthPercentage)
            throw CLI::ValidationError("Bandage-NG error",
                                       "the maximum BLAST query path length percent discrepancy must be greater than or equal to the minimum length discrepancy.");

        // Make sure that the min length discrepancy is less than or equal to the max
        // length discrepancy.
        bool minLengthBaseDiscrepancyOn = g_settings->minLengthBaseDiscrepancy.on;
        bool maxLengthBaseDiscrepancyOn = g_settings->maxLengthBaseDiscrepancy.on;
        int minLengthBaseDiscrepancy = g_settings->minLengthBaseDiscrepancy;
        int maxLengthBaseDiscrepancy = g_settings->maxLengthBaseDiscrepancy;
        if (minLengthBaseDiscrepancyOn && maxLengthBaseDiscrepancyOn &&
            minLengthBaseDiscrepancy > maxLengthBaseDiscrepancy)
            throw CLI::ValidationError("Bandage-NG error",
                                       "the maximum BLAST query path length base discrepancy must be greater than or equal to the minimum length discrepancy.");
    });

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
