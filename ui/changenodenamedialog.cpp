#include "changenodenamedialog.h"
#include "ui_changenodenamedialog.h"

#include "graph/assemblygraph.h"
#include "program/globals.h"

#include <QPushButton>
#include <utility>

ChangeNodeNameDialog::ChangeNodeNameDialog(QWidget * parent, QString oldName) :
    QDialog(parent),
    ui(new Ui::ChangeNodeNameDialog),
    m_oldName(std::move(oldName))
{
    ui->setupUi(this);

    ui->currentNodeNameLabel2->setText(m_oldName);
    checkNodeNameValidity();
    ui->newNodeNameLineEdit->setFocus();

    connect(ui->newNodeNameLineEdit, SIGNAL(textChanged(QString)), this, SLOT(checkNodeNameValidity()));
}

ChangeNodeNameDialog::~ChangeNodeNameDialog()
{
    delete ui;
}


QString ChangeNodeNameDialog::getNewName() const
{
    return ui->newNodeNameLineEdit->text();
}


void ChangeNodeNameDialog::checkNodeNameValidity()
{
    QString potentialName = ui->newNodeNameLineEdit->text();

    //If the new name is blank, don't allow an accept but no need to show an
    //error.
    if (potentialName.length() == 0)
    {
        ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        ui->errorLabel->setText("");
        return;
    }

    if (potentialName == m_oldName)
    {
        ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        ui->errorLabel->setText("Same as current");
        return;
    }

    NodeNameStatus nodeNameStatus = g_assemblyGraph->checkNodeNameValidity(potentialName);
    switch (nodeNameStatus) {
        case NodeNameStatus::NODE_NAME_OKAY:
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
            ui->errorLabel->setText("");
            break;
        case NodeNameStatus::NODE_NAME_TAKEN:
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            ui->errorLabel->setText("Name already used");
            break;
        case NodeNameStatus::NODE_NAME_CONTAINS_TAB:
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            ui->errorLabel->setText("Tab not allowed");
            break;
        case NodeNameStatus::NODE_NAME_CONTAINS_NEWLINE:
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            ui->errorLabel->setText("Newline not allowed");
            break;
        case NodeNameStatus::NODE_NAME_CONTAINS_COMMA:
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            ui->errorLabel->setText("Comma not allowed");
            break;
        case NodeNameStatus::NODE_NAME_CONTAINS_SPACE:
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            ui->errorLabel->setText("Space not allowed");
            break;
        default:
            //Catch any other error cases (shouldn't happen, but just in case)
            ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            ui->errorLabel->setText("");
            break;
    }
}
