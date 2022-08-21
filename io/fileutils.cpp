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

#include "fileutils.h"

#include <QTextStream>
#include <QFile>
#include <QChar>
#include <QApplication>
#include <QRegularExpression>

namespace utils {
    bool readFastxFile(const QString &filename, std::vector<QString> &names,
                       std::vector<QByteArray> &sequences) {
        QChar firstChar = QChar(0);
        QFile inputFile(filename);
        if (!inputFile.open(QIODevice::ReadOnly))
            return false;

        QTextStream in(&inputFile);
        QString firstLine = in.readLine();
        firstChar = firstLine.at(0);
        inputFile.close();

        if (firstChar == '>')
            return readFastaFile(filename, names, sequences);
        else if (firstChar == '@')
            return readFastqFile(filename, names, sequences);

        return false;
    }


    bool readFastaFile(const QString &filename, std::vector<QString> &names,
                       std::vector<QByteArray> &sequences) {
        QFile inputFile(filename);
        if (!inputFile.open(QIODevice::ReadOnly))
            return false;

        QString name = "";
        QByteArray sequence = "";

        QTextStream in(&inputFile);
        while (!in.atEnd()) {
            QApplication::processEvents();

            QString line = in.readLine();

            if (line.length() == 0)
                continue;

            if (line.at(0) == '>') {
                //If there is a current sequence, add it to the vectors now.
                if (name.length() > 0) {
                    names.push_back(name);
                    sequences.push_back(sequence);
                }

                line.remove(0, 1); //Remove '>' from start
                name = line;
                sequence = "";
            } else //It's a sequence line
                sequence += line.simplified().toLatin1();
        }

        //Add the last target to the results now.
        if (name.length() > 0) {
            names.push_back(name);
            sequences.push_back(sequence);
        }

            return true;
    }


    bool readFastqFile(const QString &filename, std::vector<QString> &names,
                       std::vector<QByteArray> &sequences) {
        QFile inputFile(filename);
        if (!inputFile.open(QIODevice::ReadOnly))
            return false;

        QTextStream in(&inputFile);
        while (!in.atEnd()) {
            QApplication::processEvents();

            QString name = in.readLine().simplified();
            QByteArray sequence = in.readLine().simplified().toLocal8Bit();
            in.readLine();  // separator
            in.readLine();  // qualities

            if (name.length() == 0)
                continue;
            if (sequence.length() == 0)
                continue;
            if (name.at(0) != '@')
                continue;
            name.remove(0, 1); //Remove '@' from start
            names.push_back(name);
            sequences.push_back(sequence);
        }

        return true;
    }

    bool readHmmFile(const QString &filename,
                     std::vector<QString> &names, std::vector<unsigned> &lengths,
                     std::vector<QByteArray> &hmms, std::vector<bool> &protHmms) {
        QFile inputFile(filename);
        if (!inputFile.open(QIODevice::ReadOnly))
            return false;

        QTextStream in(&inputFile);
        // FIXME: This is a very rudimentary and inefficient parser, but we
        // should be ok for small models
        QString all = in.readAll();
        for (auto part : all.split(QRegularExpression("//[\r\n]"), Qt::SkipEmptyParts)) {
            QString name;
            unsigned length = 0;
            std::optional<bool> protHmm;

            // Sanity check
            if (!part.startsWith("HMMER3"))
                continue;

            // Find name and length
            for (auto line : part.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts)) {
                auto tv = line.split(" ", Qt::SkipEmptyParts);
                if (tv.length() < 2)
                    continue;
                if (tv[0] == "NAME")
                    name = tv[1];
                else if (tv[0] == "LENG")
                    length = tv[1].toInt();
                else if (tv[0] == "ALPH")
                    protHmm = tv[1] == "amino";

                if (!name.isEmpty() && length > 0 && protHmm.has_value())
                    break;
            }

            if (name.isEmpty() || length == 0)
                continue;

            names.push_back(name);
            lengths.push_back(length);
            hmms.push_back(part.toLatin1());
            protHmms.push_back(*protHmm);
        }

        return true;
    }

}
