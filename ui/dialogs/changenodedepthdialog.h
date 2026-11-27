#ifndef CHANGENODEDEPTHDIALOG_H
#define CHANGENODEDEPTHDIALOG_H

#include <QDialog>

#include "graph/debruijnnode.h"

namespace Ui {
class ChangeNodeDepthDialog;
}

class ChangeNodeDepthDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChangeNodeDepthDialog(QWidget *parent,
                                       std::vector<OrientedNode> *nodes,
                                       double oldDepth);
    ~ChangeNodeDepthDialog() override;

    double getNewDepth() const;

private:
    Ui::ChangeNodeDepthDialog *ui;
};

#endif // CHANGENODEDEPTHDIALOG_H
