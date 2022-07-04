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

namespace utils {
    void readFastaOrFastqFile(const QString &filename, std::vector<QString> *names,
                              std::vector<QByteArray> *sequences) {
        QChar firstChar = QChar(0);
        QFile inputFile(filename);
        if (inputFile.open(QIODevice::ReadOnly)) {
            QTextStream in(&inputFile);
            QString firstLine = in.readLine();
            firstChar = firstLine.at(0);
            inputFile.close();
        }
        if (firstChar == '>')
            readFastaFile(filename, names, sequences);
        else if (firstChar == '@')
            readFastqFile(filename, names, sequences);
    }


    void readFastaFile(const QString &filename, std::vector<QString> *names,
                       std::vector<QByteArray> *sequences) {
        QFile inputFile(filename);
        if (inputFile.open(QIODevice::ReadOnly)) {
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
                        names->push_back(name);
                        sequences->push_back(sequence);
                    }

                    line.remove(0, 1); //Remove '>' from start
                    name = line;
                    sequence = "";
                } else //It's a sequence line
                    sequence += line.simplified().toLatin1();
            }

            //Add the last target to the results now.
            if (name.length() > 0) {
                names->push_back(name);
                sequences->push_back(sequence);
            }

            inputFile.close();
        }
    }


    void readFastqFile(const QString &filename, std::vector<QString> *names,
                       std::vector<QByteArray> *sequences) {
        QFile inputFile(filename);
        if (inputFile.open(QIODevice::ReadOnly)) {
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
                names->push_back(name);
                sequences->push_back(sequence);
            }
            inputFile.close();
        }
    }
}