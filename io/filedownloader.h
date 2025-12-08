// Copyright 2025 Anton Korobeynikov

// This file is part of Bandage

// Bandage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

// Bandage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Bandage.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>

class FileDownloader : public QObject {
    Q_OBJECT

public:
    explicit FileDownloader(QObject* parent = nullptr);
    void download(const QString& url, const QString& outputPath = QString());

signals:
    void progressChanged(qint64 received, qint64 total);
    void finished(bool success, const QString& message);
    void error(const QString& message);

private:
    void processDataChunk(QNetworkReply* reply);
    void handleFinished(QNetworkReply* reply);

    QNetworkAccessManager* m_manager;
    QFile m_file;
    QString m_outputPath;
    qint64 m_bytesReceived = 0;
};
