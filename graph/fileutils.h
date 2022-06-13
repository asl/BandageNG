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

#include <QString>
#include <QByteArray>
#include <vector>

namespace utils {
    void readFastaOrFastqFile(const QString &filename, std::vector<QString> *names,
                              std::vector<QByteArray> *sequences);

    void readFastaFile(const QString &filename, std::vector<QString> *names,
                       std::vector<QByteArray> *sequences);

    void readFastqFile(const QString &filename, std::vector<QString> *names,
                       std::vector<QByteArray> *sequences);
}