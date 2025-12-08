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

#include <QDialog>
#include "io/filedownloader.h"

QT_BEGIN_NAMESPACE
namespace Ui { class LoganDownloadDialog; }
QT_END_NAMESPACE

class LoganDownloadDialog : public QDialog {
    Q_OBJECT

public:
    enum class DownloadType {
        Unitigs,
        Contigs
    };

    explicit LoganDownloadDialog(QWidget* parent = nullptr);
    ~LoganDownloadDialog();

    DownloadType downloadType() const;
    QString buildUrl(const QString& accession, DownloadType type) const;
    QString buildFileName(const QString& accession, DownloadType type) const;

private slots:
    void on_saveToFileCheck_toggled(bool checked);
    void on_browseButton_clicked();
    void on_downloadButton_clicked();
    void updateProgress(qint64 received, qint64 total);
    void downloadFinished(bool success, const QString& message);
    void downloadError(const QString& message);

signals:
    void graphDownloaded(QString fullFileName);
    
private:
    QString m_outputPath;
    Ui::LoganDownloadDialog* ui;
    FileDownloader* m_downloader;
};
