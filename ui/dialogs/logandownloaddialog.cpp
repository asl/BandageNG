#include "logandownloaddialog.h"
#include "ui_logandownloaddialog.h"
#include <QFileDialog>
#include <QMessageBox>

LoganDownloadDialog::LoganDownloadDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::LoganDownloadDialog) {
    ui->setupUi(this);

    // Create downloader
    m_downloader = new FileDownloader(this);
    connect(m_downloader, &FileDownloader::progressChanged, 
            this, &LoganDownloadDialog::updateProgress);
    connect(m_downloader, &FileDownloader::finished, 
            this, &LoganDownloadDialog::downloadFinished);
    connect(m_downloader, &FileDownloader::error, 
    this, &LoganDownloadDialog::downloadError);
}

LoganDownloadDialog::~LoganDownloadDialog() {
    delete ui;
}

LoganDownloadDialog::DownloadType LoganDownloadDialog::downloadType() const {
    if (ui->unitigsRadio->isChecked()) {
        return DownloadType::Unitigs;
    } else {
        return DownloadType::Contigs;
    }
}

QString LoganDownloadDialog::buildUrl(const QString& accession, DownloadType type) const {
    if (type == DownloadType::Unitigs) {
        return QString("https://s3.amazonaws.com/logan-pub/u/%1/%1.unitigs.fa.zst").arg(accession);
    } else {
        return QString("https://s3.amazonaws.com/logan-pub/c/%1/%1.contigs.fa.zst").arg(accession);
    }
}

QString LoganDownloadDialog::buildFileName(const QString& accession, DownloadType type) const {
    if (type == DownloadType::Unitigs) {
        return QString("%1.unitigs.fa.zst").arg(accession);
    } else {
        return QString("%1.contigs.fa.zst").arg(accession);
    }
}

void LoganDownloadDialog::on_saveToFileCheck_toggled(bool checked) {
    ui->filePathEdit->setEnabled(checked);
    ui->browseButton->setEnabled(checked);
}

void LoganDownloadDialog::on_browseButton_clicked() {
    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        "Select Directory",
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!dirPath.isEmpty()) {
        ui->filePathEdit->setText(dirPath);
    }
}

void LoganDownloadDialog::on_downloadButton_clicked() {
    QString accession = ui->accessionEdit->text().trimmed();
    
    if (accession.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please enter an accession number");
        return;
    }

    // Get download type and build URL
    DownloadType type = downloadType();
    QString url = buildUrl(accession, type);
    QString typeStr = (type == DownloadType::Unitigs) ? "unitigs" : "contigs";
    
    QString outputPath;
    if (ui->saveToFileCheck->isChecked()) {
        QString dirPath = ui->filePathEdit->text().trimmed();
        if (dirPath.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please choose a destination directory");
            return;
        }
        
        // Build full file path from directory and accession
        QString fileName = buildFileName(accession, type);
        outputPath = dirPath + "/" + fileName;
    }

    // Disable UI during download
    ui->downloadButton->setEnabled(false);
    ui->accessionEdit->setEnabled(false);
    ui->unitigsRadio->setEnabled(false);
    ui->contigsRadio->setEnabled(false);
    ui->saveToFileCheck->setEnabled(false);
    ui->filePathEdit->setEnabled(false);
    ui->browseButton->setEnabled(false);
    
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(QString("Starting download (%1)...\nURL: %2").arg(typeStr, url));

    m_outputPath = outputPath;
    m_downloader->download(url, outputPath);
}

void LoganDownloadDialog::updateProgress(qint64 received, qint64 total) {
    if (total > 0) {
        int percentage = static_cast<int>((received * 100) / total);
        ui->progressBar->setValue(percentage);
        
        QString status = QString("Downloading: %1 / %2 bytes (%3%)")
            .arg(received)
            .arg(total)
            .arg(percentage);
        ui->statusLabel->setText(status);
    } else {
        ui->statusLabel->setText(QString("Downloading: %1 bytes").arg(received));
    }
}

void LoganDownloadDialog::downloadFinished(bool success, const QString& message) {
    // Re-enable UI
    ui->downloadButton->setEnabled(true);
    ui->accessionEdit->setEnabled(true);
    ui->unitigsRadio->setEnabled(true);
    ui->contigsRadio->setEnabled(true);
    ui->saveToFileCheck->setEnabled(true);
    ui->filePathEdit->setEnabled(ui->saveToFileCheck->isChecked());
    ui->browseButton->setEnabled(ui->saveToFileCheck->isChecked());

    ui->statusLabel->setText(message);

    if (success) {
        ui->progressBar->setValue(100);
        QMessageBox::information(this, "Success", message);
        emit graphDownloaded(m_outputPath);
        close();
    } else {
        QMessageBox::critical(this, "Error", message);
        ui->progressBar->setValue(0);
    }
}

void LoganDownloadDialog::downloadError(const QString& message) {
    ui->statusLabel->setText("Error: " + message);
    QMessageBox::critical(this, "Error", message);
}
\
