// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage

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

#include "graphsearch.h"

#include "blast/blastsearch.h"
#include "minimap2/minimap2search.h"
#include "hmmer/hmmersearch.h"

#include <memory>

using namespace search;

std::unique_ptr<GraphSearch> GraphSearch::get(GraphSearchKind kind,
                                              const QDir &workDir, QObject *parent) {
    std::unique_ptr<GraphSearch> res;

    switch (kind) {
        case BLAST:
            res = std::make_unique<BlastSearch>(workDir, parent);
            break;
        case Minimap2:
            res = std::make_unique<Minimap2Search>(workDir, parent);
            break;
        case NHMMER:
            res = std::make_unique<HmmerSearch>(workDir, parent);
            break;
    }

    return res;
}
