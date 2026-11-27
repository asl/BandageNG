// Copyright 2022 Anton Korobeynikov

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

#include "fastawriter.h"
#include "assemblygraph.h"

#include <QFile>
#include <QTextStream>

namespace utils {
    bool saveEntireGraphToFasta(const QString &filename,
                                const AssemblyGraph &graph) {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&file);
        for (OrientedNode positive : graph.canonical_nodes()) {
            out << positive.getFasta(true);
            // reverse() of a palindrome is that same orientation, so it is written twice.
            out << positive.reverse().getFasta(true);
        }

        return true;
    }

    bool saveEntireGraphToFastaOnlyPositiveNodes(const QString &filename,
                                                 const AssemblyGraph &graph) {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&file);
        for (OrientedNode positive : graph.canonical_nodes())
            out << positive.getFasta(false);

        return true;
    }

}