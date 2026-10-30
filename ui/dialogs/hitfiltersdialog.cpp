// Copyright 2022 Anton Korobeynikov

// This file is part of Bandage-NG

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

#include "hitfiltersdialog.h"
#include "ui_hitfiltersdialog.h"

#include "program/globals.h"
#include "program/settings.h"

HitFiltersDialog::HitFiltersDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::HitFiltersDialog)
{
    ui->setupUi(this);

    connect(ui->alignmentLengthCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->queryCoverageCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->identityCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->eValueCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->bitScoreCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
}

HitFiltersDialog::~HitFiltersDialog()
{
    delete ui;
}


void HitFiltersDialog::checkBoxesChanged()
{
    ui->alignmentLengthSpinBox->setEnabled(ui->alignmentLengthCheckBox->isChecked());

    ui->queryCoverageSpinBox->setEnabled(ui->queryCoverageCheckBox->isChecked());

    ui->identitySpinBox->setEnabled(ui->identityCheckBox->isChecked());

    ui->eValueCoefficientSpinBox->setEnabled(ui->eValueCheckBox->isChecked());
    ui->eValueExponentSpinBox->setEnabled(ui->eValueCheckBox->isChecked());

    ui->bitScoreSpinBox->setEnabled(ui->bitScoreCheckBox->isChecked());
}


void setSpinBoxValMinAndMax(QSpinBox * spinBox, IntSetting setting)
{
    spinBox->setValue(setting.val);
    spinBox->setMinimum(setting.min);
    spinBox->setMaximum(setting.max);
}

void setSpinBoxValMinAndMax(QDoubleSpinBox * spinBox, FloatSetting setting)
{
    spinBox->setValue(setting.val);
    spinBox->setMinimum(setting.min);
    spinBox->setMaximum(setting.max);
}

void HitFiltersDialog::setWidgetsFromSettings()
{
    ui->alignmentLengthCheckBox->setChecked(g_settings->alignmentLengthFilter.on);
    ui->queryCoverageCheckBox->setChecked(g_settings->queryCoverageFilter.on);
    ui->identityCheckBox->setChecked(g_settings->identityFilter.on);
    ui->eValueCheckBox->setChecked(g_settings->evalueFilter.on);
    ui->bitScoreCheckBox->setChecked(g_settings->bitScoreFilter.on);

    setSpinBoxValMinAndMax(ui->alignmentLengthSpinBox, g_settings->alignmentLengthFilter);
    setSpinBoxValMinAndMax(ui->queryCoverageSpinBox, g_settings->queryCoverageFilter);
    setSpinBoxValMinAndMax(ui->identitySpinBox, g_settings->identityFilter);
    setSpinBoxValMinAndMax(ui->bitScoreSpinBox, g_settings->bitScoreFilter);

    ui->eValueCoefficientSpinBox->setValue(g_settings->evalueFilter.val.getCoefficient());
    ui->eValueExponentSpinBox->setValue(g_settings->evalueFilter.val.getExponent());

    checkBoxesChanged();
}


void HitFiltersDialog::setSettingsFromWidgets()
{
    g_settings->alignmentLengthFilter.on = ui->alignmentLengthCheckBox->isChecked();
    g_settings->queryCoverageFilter.on = ui->queryCoverageCheckBox->isChecked();
    g_settings->identityFilter.on = ui->identityCheckBox->isChecked();
    g_settings->evalueFilter.on = ui->eValueCheckBox->isChecked();
    g_settings->bitScoreFilter.on = ui->bitScoreCheckBox->isChecked();

    g_settings->alignmentLengthFilter.val = ui->alignmentLengthSpinBox->value();
    g_settings->queryCoverageFilter.val = ui->queryCoverageSpinBox->value();
    g_settings->identityFilter.val = ui->identitySpinBox->value();
    g_settings->evalueFilter.val = SciNot(ui->eValueCoefficientSpinBox->value(), ui->eValueExponentSpinBox->value());
    g_settings->bitScoreFilter.val = ui->bitScoreSpinBox->value();
}


QString HitFiltersDialog::getFilterText()
{
    QString filterText;
    if (g_settings->alignmentLengthFilter.on)
    {
        if (filterText.length() > 0)
            filterText += ", ";
        filterText += "alignment length: " + QString::number(g_settings->alignmentLengthFilter);
    }
    if (g_settings->queryCoverageFilter.on)
    {
        if (filterText.length() > 0)
            filterText += ", ";
        filterText += "query coverage: " + QString::number(g_settings->queryCoverageFilter) + "%";
    }
    if (g_settings->identityFilter.on)
    {
        if (filterText.length() > 0)
            filterText += ", ";
        filterText += "identity: " + QString::number(g_settings->identityFilter) + "%";
    }
    if (g_settings->evalueFilter.on)
    {
        if (filterText.length() > 0)
            filterText += ", ";
        filterText += "e-value: " + g_settings->evalueFilter.val.asString(true);
    }
    if (g_settings->bitScoreFilter.on)
    {
        if (filterText.length() > 0)
            filterText += ", ";
        filterText += "bit score: " + QString::number(g_settings->bitScoreFilter);
    }

    if (filterText == "")
        filterText = "none";

    return filterText;
}