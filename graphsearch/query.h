// Copyright 2017 Ryan Wick
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

#pragma once

#include "querypath.h"
#include "graphsearch/hit.h"

#include <QString>
#include <QColor>
#include <utility>

namespace search {
    enum QuerySequenceType {
        NUCLEOTIDE,
        PROTEIN
    };

    class Query {
    public:
        //CREATORS
        Query(QString name, QString sequence, QByteArray aux = QByteArray());

        //ACCESSORS
        QString getName() const { return m_name; }
        QString getSequence() const { return m_sequence; }
        size_t getLength() const { return m_sequence.length(); }
        QByteArray getAuxData() const { return m_aux; }

        bool hasHits() const { return !m_hits.empty(); }
        size_t hitCount() const { return m_hits.size(); }
        const auto &getHits() const { return m_hits; }

        bool wasSearchedFor() const { return m_searchedFor; }
        QColor getColour() const { return m_colour; }
        QuerySequenceType getSequenceType() const { return m_sequenceType; }

        const auto &getPaths() const { return m_paths; }
        size_t getPathCount() const { return m_paths.size(); }

        QString getTypeString() const;

        double fractionCoveredByHits(const std::vector<const Hit *> &hitsToCheck = {}) const;

        bool isShown() const { return m_shown; }
        bool isHidden() const { return !m_shown; }

        // MODIFIERS
        void setName(QString newName) { m_name = std::move(newName); }
        void addHit(Hit newHit) { m_hits.emplace_back(newHit); }

        void clearSearchResults();
        void setAsSearchedFor() { m_searchedFor = true; }

        void findQueryPaths();

        void setColour(QColor newColour) { m_colour = newColour; }
        void setShown(bool newShown) { m_shown = newShown; }
    private:
        QString m_name;
        QString m_sequence;
        QByteArray m_aux;
        std::vector<Hit> m_hits;
        QuerySequenceType m_sequenceType;
        bool m_searchedFor = false;
        bool m_shown = true;
        QColor m_colour;
        std::vector<QueryPath> m_paths;

        void autoSetSequenceType();
    };
}
