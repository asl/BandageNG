// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <QString>
#include <QByteArray>
#include <QStringList>

#include "seq/sequence.hpp"

namespace utils {
    static inline QByteArray sequenceToQByteArray(const Sequence &sequence) {
        auto sequence_str = sequence.str();
        return {sequence_str.c_str(), static_cast<qsizetype>(sequence_str.size())};
    }

    // This function is used when making FASTA outputs - it breaks a sequence into
    // separate lines.  The default interval is 70, as that seems to be what NCBI
    // uses.
    // The returned string always ends in a newline.
    QByteArray addNewlinesToSequence(const QByteArray &sequence,
                                     int interval = 70);

    // Split a QString according to CSV rules
    // @param line  line of a csv
    // @param sep   field separator to use
    // @result      list of fields with escaping removed
    //
    // Known Bugs: CSV (as per RFC4180) allows multi-line fields (\r\n between "..."), which
    //             can't be parsed line-by line, hence isn't supported.
    QStringList splitCsv(const QString& line, const QString& sep=",");

    // This function will trim bases from the start of a sequence (in the case of
    // positive overlap) or add Ns to the start (in the case of negative overlap).
    QByteArray modifySequenceUsingOverlap(QByteArray sequence, int overlap);
}