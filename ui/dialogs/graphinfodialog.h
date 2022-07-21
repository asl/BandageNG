#ifndef GRAPHINFODIALOG_H
#define GRAPHINFODIALOG_H

#include <QDialog>

namespace Ui {
class GraphInfoDialog;
}

class GraphInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GraphInfoDialog(QWidget *parent = nullptr);
    ~GraphInfoDialog() override;

private:
    Ui::GraphInfoDialog *ui;

    void setLabels();
    void setInfoTexts();
};

#endif // GRAPHINFODIALOG_H
