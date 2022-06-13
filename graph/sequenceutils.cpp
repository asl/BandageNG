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

#include "sequenceutils.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QStringList>

namespace utils {
    QByteArray addNewlinesToSequence(const QByteArray &sequence, int interval) {
        QByteArray output;

        int charactersRemaining = sequence.length();
        int currentIndex = 0;
        while (charactersRemaining > interval) {
            output += sequence.mid(currentIndex, interval);
            output += "\n";
            charactersRemaining -= interval;
            currentIndex += interval;
        }
        output += sequence.mid(currentIndex);
        output += "\n";

        return output;
    }

    QStringList splitCsv(const QString &line, const QString &sep) {
        // TODO: use libraries for parsing CSV
        QRegularExpression rx(R"(("(?:[^"]|"")*"|[^)" + sep + "]*)(?:" + sep + "|$)");
        QStringList list;

        auto it = rx.globalMatch(line);
        while (it.hasNext()) {
            auto match = it.next();
            QString field = match.captured().replace("\"\"", "\"");
            if (field.endsWith(sep)) {
                field.chop(sep.length());
            }
            if (!field.isEmpty() && field[0] == '"' && field[field.length() - 1] == '"') {
                field = field.mid(1, field.length() - 2);
            }
            list << field;
        }

        // regexp always matches empty string at the end
        // if string ends with separator then we need to store it
        if (!line.endsWith(sep)) {
            list.pop_back();
        }

        return list;
    }
} // namespace utils