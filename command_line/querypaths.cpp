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

#include "querypaths.h"
#include "commoncommandlinefunctions.h"

#include "graph/assemblygraph.h"
#include "graph/sequenceutils.h"
#include "graphsearch/blast/blastsearch.h"
#include "program/settings.h"

#include <CLI/CLI.hpp>

#include <QDateTime>
#include <type_traits>

CLI::App *addQueryPathsSubcommand(CLI::App &app,
                                  QueryPathsCmd &cmd) {
    auto *qp = app.add_subcommand("querypaths", "Output graph paths for BLAST queries");
    qp->add_option("<graph>", cmd.m_graph, "A graph file of any type supported by Bandage")
            ->required()->check(CLI::ExistingFile);
    qp->add_option("<queries>", cmd.m_queries, "A FASTA file of one or more BLAST queries")
            ->required()->check(CLI::ExistingFile);
    qp->add_option("<output_prefix>", cmd.m_prefix, "The output file prefix (used to create the '.tsv' output file, and possibly FASTA files as well, depending on options)")
            ->required();
    qp->add_flag("--pathfasta", cmd.m_pathFasta, "Put all query path sequences in a multi-FASTA file, not in the TSV file");
    qp->add_flag("--hitsfasta", cmd.m_hitsFasta, "Produce a multi-FASTA file of all BLAST hits in the query paths");
    qp->add_flag("--gfapaths", cmd.m_gfaPaths, "Align to GFA path sequences in addition to nodes");

    qp->footer("Bandage querypaths searches for queries in the graph using BLAST and outputs the results to a tab-delimited file.");

    return qp;

}

int handleQueryPathsCmd(QApplication *app,
                        const CLI::App &cli,
                        const QueryPathsCmd &cmd) {
    QTextStream out(stdout);
    QTextStream err(stderr);

    g_settings->blastQueryFilename = cmd.m_queries.c_str();

    // Ensure that the --query option isn't used, as that would overwrite the
    // queries file that is a positional argument.
    if (cli.count("--query")) {
        err << "Bandage-NG error: the --query option cannot be used with Bandage querypaths." << Qt::endl;
        return 1;
    }

    QString outputPrefix = cmd.m_prefix.c_str();
    QString tableFilename = outputPrefix + ".tsv";
    QString pathFastaFilename = outputPrefix + "_paths.fasta";
    QString hitsFastaFilename = outputPrefix + "_hits.fasta";

    // Check to make sure the output files don't already exist.
    QFile tableFile(tableFilename);
    QFile pathsFile(pathFastaFilename);
    QFile hitsFile(hitsFastaFilename);
    if (tableFile.exists()) {
        outputText("Bandage-NG error: " + tableFilename + " already exists.", &err);
        return 1;
    }
    if (cmd.m_pathFasta && pathsFile.exists()) {
        outputText("Bandage-NG error: " + pathFastaFilename + " already exists.", &err);
        return 1;
    }
    if (cmd.m_hitsFasta && hitsFile.exists()) {
        outputText("Bandage-NG error: " + hitsFastaFilename + " already exists.", &err);
        return 1;
    }

    QDateTime startTime = QDateTime::currentDateTime();

    auto log = [&out](const char * msg) {
        out << "(" << QDateTime::currentDateTime().toString("dd MMM yyyy hh:mm:ss") << ") " << msg  << Qt::flush;
    };

    log("Loading graph...        ");

    if (!g_assemblyGraph->loadGraphFromFile(cmd.m_graph.c_str())) {
        err << "Bandage-NG error: could not load " << cmd.m_graph.c_str() << Qt::endl;
        return 1;
    }

    if (!g_blastSearch->ready()) {
        err << g_blastSearch->lastError() << Qt::endl;
        return 1;
    }
    out << "done" << Qt::endl;

    log("Running BLAST search... ");
    QString blastError = g_blastSearch->doAutoGraphSearch(*g_assemblyGraph,
                                                          g_settings->blastQueryFilename,
                                                          cmd.m_gfaPaths,
                                                          g_settings->blastSearchParameters);
    if (!blastError.isEmpty()) {
        err << Qt::endl << blastError << Qt::endl;
        return 1;
    }
    out << "done" << Qt::endl;
    log("Saving results...       ");

    // Create the table file.
    tableFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream tableOut(&tableFile);

    // Write the TSV header line.
    tableOut << "Query\t"
                "Path\t"
                "Length\t"
                "Query covered by path\t"
                "Query covered by hits\t"
                "Mean hit identity\t"
                "Total hit mismatches\t"
                "Total hit gap opens\t"
                "Relative length\t"
                "Length discrepancy\t"
                "E-value product\t";

    // If the user asked for a separate path sequence file, then the last column
    // will be a reference to that sequence ID.  If not, the sequence will go in
    // the table.
    if (cmd.m_pathFasta)
        tableOut << "Sequence ID\n";
    else
        tableOut << "Sequence\n";

    // If a path sequence FASTA file is used, these will store the sequences
    // that will go there.
    QList<QString> pathSequenceIDs;
    QList<QByteArray> pathSequences;

    // If a hits sequence FASTA file is used, these will store the sequences
    // that will go there.
    QList<QString> hitSequenceIDs;
    QList<QByteArray> hitSequences;

    auto maybeNA = [](auto val) -> QString {
        using ValT = typeof(val);
        if constexpr (std::is_same_v<ValT, double>) {
            if (isnan(val))
            return "N/A";
        } else if constexpr (std::is_same_v<ValT, SciNot>) {
            if (isnan(val.toDouble()))
                return "N/A";
            return val.asString(false);
        } else {
            if (val < 0)
                return "N/A";

            return QString::number(val);
        }

        return "N/A";
    };

    for (const auto *query : g_blastSearch->queries()) {
        unsigned num = 0;
        for (const auto & queryPath : query->getPaths()) {
            Path path = queryPath.getPath();

            tableOut << query->getName() << "\t"
                     << path.getString(true) << "\t"
                     << QString::number(path.getLength()) << "\t"
                     << QString::number(queryPath.getPathQueryCoverage()) << "\t"
                     << QString::number(queryPath.getHitsQueryCoverage()) << "\t"
                     << maybeNA(queryPath.getMeanHitPercIdentity()) << "\t"
                     << maybeNA(queryPath.getTotalHitMismatches()) << "\t"
                     << maybeNA(queryPath.getTotalHitGapOpens()) << "\t"
                     << QString::number(queryPath.getRelativePathLength()) << "\t"
                     << queryPath.getAbsolutePathLengthDifferenceString(false) << "\t"
                     << maybeNA(queryPath.getEvalueProduct()) << "\t";

            // If we are using a separate file for the path sequences, save the
            // sequence along with its ID to save later, and store the ID here.
            // Otherwise, just include the sequence in this table.
            QByteArray sequence = path.getPathSequence();
            QString pathSequenceID = query->getName() + "_" + QString::number(++num);
            if (cmd.m_pathFasta){
                pathSequenceIDs.push_back(pathSequenceID);
                pathSequences.push_back(sequence);
                tableOut << pathSequenceID << "\n";
            } else
                tableOut << sequence << "\n";

            // If we are also saving the hit sequences, save the hit sequence
            // along with its ID to save later.
            if (cmd.m_hitsFasta) {
                const auto &hits = queryPath.getHits();
                for (unsigned k = 0; k < hits.size(); ++k) {
                    const auto *hit = hits[k];
                    QString hitSequenceID = pathSequenceID + "_" + QString::number(k+1);
                    QByteArray hitSequence = hit->getNodeSequence();
                    hitSequenceIDs.push_back(hitSequenceID);
                    hitSequences.push_back(hitSequence);
                }
            }
        }
    }

    // Write the path sequence FASTA file, if appropriate.
    if (cmd.m_pathFasta) {
        pathsFile.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream pathsOut(&pathsFile);

        for (unsigned i = 0; i < pathSequenceIDs.size(); ++i) {
            pathsOut << ">" + pathSequenceIDs[i] + "\n";
            pathsOut << utils::addNewlinesToSequence(pathSequences[i]);
        }
    }

    // Write the hits sequence FASTA file, if appropriate.
    if (cmd.m_hitsFasta) {
        hitsFile.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream hitsOut(&hitsFile);

        for (unsigned i = 0; i < hitSequenceIDs.size(); ++i) {
            hitsOut << ">" << hitSequenceIDs[i] << "\n";
            hitsOut << utils::addNewlinesToSequence(hitSequences[i]);
        }
    }

    out << "done" << Qt::endl;

    out << Qt::endl << "Results: " + tableFilename << Qt::endl;
    if (cmd.m_pathFasta)
        out << "              " + pathFastaFilename << Qt::endl;
    if (cmd.m_hitsFasta)
        out << "              " + hitsFastaFilename << Qt::endl;

    out << Qt::endl << "Summary: Total BLAST queries:           " << g_blastSearch->getQueryCount() << Qt::endl;
    out << "         Total hits:                    " << g_blastSearch->getNumHits() << Qt::endl;
    out << "         Queries with found paths:      " << g_blastSearch->getQueryCountWithAtLeastOnePath() << Qt::endl;
    out << "         Total query paths:             " << g_blastSearch->getQueryPathCount() << Qt::endl;

    out << Qt::endl << "Elapsed time: " << getElapsedTime(startTime, QDateTime::currentDateTime()) << Qt::endl;

    return 0;
}
