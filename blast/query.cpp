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


#include "query.h"
#include "program/settings.h"
#include "graph/path.h"
#include "graph/debruijnnode.h"
#include <limits>
#include <utility>
#include <vector>

using namespace search;

Query::Query(QString name, QString sequence)
    : m_name(std::move(name)), m_sequence(std::move(sequence)), m_searchedFor(false), m_shown(true) {
    autoSetSequenceType();
}


//This function looks at the query sequence to decide if it is
//a nucleotide or protein sequence.
void Query::autoSetSequenceType() {
    //If the sequence contains a letter that's in the protein
    //alphabet but not in the extended DNA alphabet, then it's
    //a protein
    if (m_sequence.contains('e') || m_sequence.contains('E') ||
        m_sequence.contains('f') || m_sequence.contains('F') ||
        m_sequence.contains('i') || m_sequence.contains('I') ||
        m_sequence.contains('l') || m_sequence.contains('L') ||
        m_sequence.contains('p') || m_sequence.contains('P') ||
        m_sequence.contains('q') || m_sequence.contains('Q')) {
        m_sequenceType = PROTEIN;
        return;
    }

    // If the code got here, then it's a bit trickier.  It could
    // possibly be an extended alphabet DNA sequence or a protein
    // sequence without particular amino acids.

    // Look to see if A, C, G, T and N make up 75% or more of
    // the sequence.  If so, it's DNA.  If not, it's
    // protein.
    size_t length = m_sequence.length();
    size_t nuclLetters = m_sequence.count('a') + m_sequence.count('A') +
                         m_sequence.count('c') + m_sequence.count('C') +
                         m_sequence.count('g') + m_sequence.count('G') +
                         m_sequence.count('t') + m_sequence.count('T') +
                         m_sequence.count('n') + m_sequence.count('N');

    if (double(nuclLetters) / length >= 0.75)
        m_sequenceType = NUCLEOTIDE;
    else
        m_sequenceType = PROTEIN;
}


QString Query::getTypeString() const {
    return (m_sequenceType == NUCLEOTIDE ? "nucl" : "prot");
}

void Query::clearSearchResults() {
    m_searchedFor = false;
    m_hits.clear();
}

// This function tries to find the paths through the graph which cover the query.
void Query::findQueryPaths() {
    m_paths.clear();
    if (m_hits.size() > g_settings->maxHitsForQueryPath)
        return;

    int queryLength = m_sequence.length();
    if (m_sequenceType == PROTEIN)
        queryLength *= 3;

    //Find all possible path starts within an acceptable distance from the query
    //start.
    std::vector<Hit *> possibleStarts;
    double acceptableStartFraction = 1.0 - g_settings->minQueryCoveredByPath;
    for (auto &hit : m_hits) {
        if (hit.queryStartFraction() <= acceptableStartFraction)
            possibleStarts.push_back(&hit);
    }

    //Find all possible path ends.
    std::vector<Hit *> possibleEnds;
    double acceptableEndFraction = g_settings->minQueryCoveredByPath;
    for (auto &hit : m_hits) {
        if (hit.queryEndFraction() >= acceptableEndFraction)
            possibleEnds.push_back(&hit);
    }

    //For each possible start, find paths to each possible end.
    QList<Path> possiblePaths;
    for (auto start : possibleStarts) {
        GraphLocation startLocation = start->getHitStart();

        for (auto end : possibleEnds) {
            GraphLocation endLocation = end->getHitEnd();

            //Assuming there is a path from the start hit to the end hit,
            //determine the ideal length.  This is the query length minus the
            //parts of the query not covered by the start and end.
            int partialQueryLength = queryLength;
            int pathStart = start->m_queryStart - 1;
            int pathEnd = end->m_queryEnd;
            if (m_sequenceType == PROTEIN) {
                pathStart *= 3;
                pathEnd *= 3;
            }
            partialQueryLength -= pathStart;
            partialQueryLength -= queryLength - pathEnd;

            //Determine the minimum and maximum lengths allowed for the path.
            int minLength;
            if (g_settings->minLengthPercentage.on && g_settings->minLengthBaseDiscrepancy.on) //both on
                minLength = std::max(int(partialQueryLength * g_settings->minLengthPercentage + 0.5), partialQueryLength + g_settings->minLengthBaseDiscrepancy);
            else if (g_settings->minLengthPercentage.on && !g_settings->minLengthBaseDiscrepancy.on) //just relative
                minLength = int(partialQueryLength * g_settings->minLengthPercentage + 0.5);
            else if (!g_settings->minLengthPercentage.on && g_settings->minLengthBaseDiscrepancy.on) //just absolute
                minLength = partialQueryLength + g_settings->minLengthBaseDiscrepancy;
            else //neither are on
                minLength = 1;

            int maxLength;
            if (g_settings->maxLengthPercentage.on && g_settings->maxLengthBaseDiscrepancy.on) //both on
                maxLength = std::min(int(partialQueryLength * g_settings->maxLengthPercentage + 0.5), partialQueryLength + g_settings->maxLengthBaseDiscrepancy);
            else if (g_settings->maxLengthPercentage.on && !g_settings->maxLengthBaseDiscrepancy.on) //just relative
                maxLength = int(partialQueryLength * g_settings->maxLengthPercentage + 0.5);
            else if (!g_settings->maxLengthPercentage.on && g_settings->maxLengthBaseDiscrepancy.on) //just absolute
                maxLength = partialQueryLength + g_settings->maxLengthBaseDiscrepancy;
            else //neither are on
                maxLength = std::numeric_limits<int>::max();

            possiblePaths.append(Path::getAllPossiblePaths(startLocation,
                                                           endLocation,
                                                           g_settings->maxQueryPathNodes - 1,
                                                           minLength,
                                                           maxLength));
        }
    }

    //Now we use the Path objects to make QueryPath objects.  These contain
    //BLAST-specific information that the Path class doesn't.
    QList<QueryPath> blastQueryPaths;
    for (auto &possiblePath : possiblePaths)
        blastQueryPaths.push_back(QueryPath(possiblePath, this));

    //We now want to throw out any paths for which the hits fail to meet the
    //thresholds in settings.
    QList<QueryPath> sufficientCoveragePaths;
    for (auto & blastQueryPath : blastQueryPaths) {
        if (blastQueryPath.getPathQueryCoverage() < g_settings->minQueryCoveredByPath)
            continue;
        if (g_settings->minQueryCoveredByHits.on && blastQueryPath.getHitsQueryCoverage() < g_settings->minQueryCoveredByHits)
            continue;
        if (g_settings->maxEValueProduct.on && blastQueryPath.getEvalueProduct() > g_settings->maxEValueProduct)
            continue;
        if (g_settings->minMeanHitIdentity.on && blastQueryPath.getMeanHitPercIdentity() < 100.0 * g_settings->minMeanHitIdentity)
            continue;
        if (g_settings->minLengthPercentage.on && blastQueryPath.getRelativePathLength() < g_settings->minLengthPercentage)
            continue;
        if (g_settings->maxLengthPercentage.on && blastQueryPath.getRelativePathLength() > g_settings->maxLengthPercentage)
            continue;
        if (g_settings->minLengthBaseDiscrepancy.on && blastQueryPath.getAbsolutePathLengthDifference() < g_settings->minLengthBaseDiscrepancy)
            continue;
        if (g_settings->maxLengthBaseDiscrepancy.on && blastQueryPath.getAbsolutePathLengthDifference() > g_settings->maxLengthBaseDiscrepancy)
            continue;

        sufficientCoveragePaths.push_back(blastQueryPath);
    }

    //We now want to throw out any paths which are sub-paths of other, larger
    //paths.
    for (int i = 0; i < sufficientCoveragePaths.size(); ++i) {
        bool throwOut = false;
        for (int j = 0; j < sufficientCoveragePaths.size(); ++j) {
            //No need to compare a path with itself.
            if (i == j)
                continue;

            if (sufficientCoveragePaths[i].getPath().hasNodeSubset(sufficientCoveragePaths[j].getPath())) {
                throwOut = true;
                break;
            }
        }
        if (!throwOut)
            m_paths.push_back(sufficientCoveragePaths[i]);
    }

    //Now we sort the paths from best to worst.
    std::sort(m_paths.begin(), m_paths.end());
}


//This function returns the fraction of the query that is covered by BLAST hits.
//If a list of BLAST hits is passed to the function, it only looks in those
//hits.  If no such list is passed, it looks in all hits for this query.
// http://stackoverflow.com/questions/5276686/merging-ranges-in-c
double Query::fractionCoveredByHits(const std::vector<const Hit *> &hitsToCheck) const {
    int hitBases = 0;
    int queryLength = getLength();
    if (queryLength == 0)
        return 0.0;

    std::vector<std::pair<int, int> > ranges;
    if (hitsToCheck.empty()) {
        for (const auto &hit : m_hits)
            ranges.emplace_back(hit.m_queryStart - 1, hit.m_queryEnd);
    } else {
        for (auto *hit : hitsToCheck) {
            ranges.emplace_back(hit->m_queryStart - 1, hit->m_queryEnd);
        }
    }

    if (ranges.empty())
        return 0.0;

    std::sort(ranges.begin(), ranges.end());

    std::vector<std::pair<int, int> > mergedRanges;
    auto it = ranges.begin();
    std::pair<int,int> current = *(it)++;
    while (it != ranges.end()) {
        if (current.second >= it->first) {
            current.second = std::max(current.second, it->second);
        } else {
            mergedRanges.push_back(current);
            current = *(it);
        }
        it++;
    }
    mergedRanges.push_back(current);

    for (auto &mergedRange : mergedRanges)
        hitBases += mergedRange.second - mergedRange.first;

    return double(hitBases) / queryLength;
}
