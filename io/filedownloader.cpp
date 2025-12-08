#include "filedownloader.h"
#include <QNetworkRequest>
#include <QUrl>

FileDownloader::FileDownloader(QObject* parent) 
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {}

void FileDownloader::download(const QString& url, const QString& outputPath) {
    m_outputPath = outputPath;
    m_bytesReceived = 0;

    QNetworkRequest request(url);
    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        processDataChunk(reply);
    });

    connect(reply, &QNetworkReply::downloadProgress, this, 
            [this](qint64 received, qint64 total) {
        emit progressChanged(received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleFinished(reply);
    });

    connect(reply, &QNetworkReply::errorOccurred, this, 
            [this](QNetworkReply::NetworkError) {
        emit error("Network error occurred");
    });
}

void FileDownloader::processDataChunk(QNetworkReply* reply) {
    QByteArray chunk = reply->readAll();
    
    if (chunk.isEmpty()) {
        return;
    }

    m_bytesReceived += chunk.size();

    if (!m_outputPath.isEmpty()) {
        if (!m_file.isOpen()) {
            m_file.setFileName(m_outputPath);
            if (!m_file.open(QIODevice::WriteOnly)) {
                emit error("Failed to open file for writing: " + m_file.errorString());
                reply->abort();
                return;
            }
        }

        qint64 written = m_file.write(chunk);
        if (written != chunk.size()) {
            emit error("Failed to write complete chunk to file");
            reply->abort();
        }
    }
}

void FileDownloader::handleFinished(QNetworkReply* reply) {
    processDataChunk(reply);

    if (m_file.isOpen()) {
        m_file.close();
    }

    if (reply->error() == QNetworkReply::NoError) {
        QString msg = QString("Download completed! Total: %1 bytes").arg(m_bytesReceived);
        if (!m_outputPath.isEmpty()) {
            msg += "\nSaved to: " + m_outputPath;
        }
        emit finished(true, msg);
    } else {
        if (!m_outputPath.isEmpty() && QFile::exists(m_outputPath)) {
            QFile::remove(m_outputPath);
        }
        emit finished(false, "Download failed: " + reply->errorString());
    }

    reply->deleteLater();
}
