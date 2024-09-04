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

#pragma once

#include "io/cigar.h"

#include "llvm/Support/Error.h"

#include <QString>
#include <memory>
#include <vector>

class AssemblyGraph;
class DeBruijnEdge;

namespace io {
    class AssemblyGraphBuilder {
    public:
        virtual llvm::Error build(AssemblyGraph &graph) = 0;
        virtual ~AssemblyGraphBuilder() = default;

        static std::unique_ptr<AssemblyGraphBuilder> get(const QString &fullFileName);

        [[nodiscard]] bool hasCustomLabels() const { return hasCustomLabels_; }
        [[nodiscard]] bool hasCustomColours() const { return hasCustomColours_; }
        [[nodiscard]] bool hasComplexOverlaps() const { return hasComplexOverlaps_; }

        void treatJumpsAsLinks(bool val = true) { jumpsAsLinks_ = val; }

    protected:
        explicit AssemblyGraphBuilder(QString fileName)
                : fileName_(std::move(fileName)) {}

        QString fileName_;
        bool hasCustomLabels_ = false;
        bool hasCustomColours_ = false;
        bool hasComplexOverlaps_ = false;
        bool jumpsAsLinks_ = false;
    };

    bool handleStandardGFAEdgeTags(const DeBruijnEdge *edgePtr,
                                   const DeBruijnEdge *rcEdgePtr,
                                   const std::vector<cigar::tag> &tags,
                                   AssemblyGraph &graph);

    llvm::Error loadGFAPaths(AssemblyGraph &graph, QString fileName);
    llvm::Error loadGFALinks(AssemblyGraph &graph, QString fileName,
                             std::vector<DeBruijnEdge*> *newEdges = nullptr);
    llvm::Error loadLinks(AssemblyGraph &graph, QString fileName,
                          std::vector<DeBruijnEdge*> *newEdges = nullptr);
    llvm::Error loadGAFPaths(AssemblyGraph &graph, QString fileName);
    llvm::Error loadSPAlignerPaths(AssemblyGraph &graph, QString fileName);
    llvm::Error loadSPAdesPaths(AssemblyGraph &graph, QString fileName);
}
