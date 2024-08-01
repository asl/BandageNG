//Copyright 2017 Ryan Wick

//This file is part of Bandage

//Bandage is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//Bandage is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with Bandage.  If not, see <http://www.gnu.org/licenses/>.


#include "settingsdialog.h"

#include "ui/widgets/colourbutton.h"
#include "program/scinot.h"
#include "program/settings.h"

#include "graph/assemblygraph.h"
#include "graph/nodecolorer.h"

#include <QMessageBox>
#include "ui_settingsdialog.h"

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    //On a PC and Linux, this window is called 'Settings', but on the Mac it is
    //called 'Preferences'.
    QString windowTitle = "Settings";
#ifdef Q_OS_MAC
    windowTitle = "Preferences";
#endif
    setWindowTitle(windowTitle);

    ui->edgeColourButton->m_name = "Edge colour";
    ui->outlineColourButton->m_name = "Outline colour";
    ui->selectionColourButton->m_name = "Selection colour";
    ui->textColourButton->m_name = "Text colour";
    ui->textOutlineColourButton->m_name = "Text outline colour";
    ui->uniformPositiveNodeColourButton->m_name = "Uniform positive node colour";
    ui->uniformNegativeNodeColourButton->m_name = "Uniform negative node colour";
    ui->uniformNodeSpecialColourButton->m_name = "Uniform special node colour";
    ui->noBlastHitsColourButton->m_name = "No BLAST hits colour";
    ui->contiguousStrandSpecificColourButton->m_name = "Contiguous (strand-specific) colour";
    ui->contiguousEitherStrandColourButton->m_name = "Contiguous (either strand) colour";
    ui->maybeContiguousColourButton->m_name = "Maybe contiguous colour";
    ui->notContiguousColourButton->m_name = "Not contiguous colour";
    ui->contiguityStartingColourButton->m_name = "Contiguity starting colour";

    connect(ui->restoreDefaultsButton, SIGNAL(clicked()), this, SLOT(restoreDefaults()));
    connect(ui->depthValueManualRadioButton, SIGNAL(toggled(bool)), this, SLOT(enableDisableDepthWidgets()));
    connect(ui->nodeLengthPerMegabaseManualRadioButton, SIGNAL(toggled(bool)), this, SLOT(nodeLengthPerMegabaseManualChanged()));
    connect(ui->depthPowerSpinBox, SIGNAL(valueChanged(double)), this, SLOT(updateNodeWidthVisualAid()));
    connect(ui->depthEffectOnWidthSpinBox, SIGNAL(valueChanged(double)), this, SLOT(updateNodeWidthVisualAid()));
    connect(ui->randomColourPositiveOpacitySpinBox, SIGNAL(valueChanged(int)), this, SLOT(colourSpinBoxChanged()));
    connect(ui->randomColourNegativeOpacitySpinBox, SIGNAL(valueChanged(int)), this, SLOT(colourSpinBoxChanged()));
    connect(ui->randomColourPositiveSaturationSpinBox, SIGNAL(valueChanged(int)), this, SLOT(colourSpinBoxChanged()));
    connect(ui->randomColourNegativeSaturationSpinBox, SIGNAL(valueChanged(int)), this, SLOT(colourSpinBoxChanged()));
    connect(ui->randomColourPositiveLightnessSpinBox, SIGNAL(valueChanged(int)), this, SLOT(colourSpinBoxChanged()));
    connect(ui->randomColourNegativeLightnessSpinBox, SIGNAL(valueChanged(int)), this, SLOT(colourSpinBoxChanged()));
    connect(ui->randomColourPositiveOpacitySlider, SIGNAL(valueChanged(int)), this, SLOT(colourSliderChanged()));
    connect(ui->randomColourNegativeOpacitySlider, SIGNAL(valueChanged(int)), this, SLOT(colourSliderChanged()));
    connect(ui->randomColourPositiveSaturationSlider, SIGNAL(valueChanged(int)), this, SLOT(colourSliderChanged()));
    connect(ui->randomColourNegativeSaturationSlider, SIGNAL(valueChanged(int)), this, SLOT(colourSliderChanged()));
    connect(ui->randomColourPositiveLightnessSlider, SIGNAL(valueChanged(int)), this, SLOT(colourSliderChanged()));
    connect(ui->randomColourNegativeLightnessSlider, SIGNAL(valueChanged(int)), this, SLOT(colourSliderChanged()));
    connect(ui->minQueryCoveredByHitsCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->minMeanHitIdentityCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->maxEValueProductCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->minLengthPercentageCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->maxLengthPercentageCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->minLengthBaseDiscrepancyCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->maxLengthBaseDiscrepancyCheckBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxesChanged()));
    connect(ui->minLengthBaseDiscrepancySpinBox, SIGNAL(valueChanged(int)), this, SLOT(lengthDiscrepancySpinBoxChanged()));
    connect(ui->maxLengthBaseDiscrepancySpinBox, SIGNAL(valueChanged(int)), this, SLOT(lengthDiscrepancySpinBoxChanged()));
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}


//These functions either set a widget to a value or set the value to the widget.  Pointers to
//these functions will be passed to loadOrSaveSettingsToOrFromWidgets, so that one function can
//take care of both save and load functionality.
void setOneSettingFromWidget(FloatSetting * setting, QDoubleSpinBox * spinBox, bool percentage) {setting->val = spinBox->value() / (percentage ? 100.0 : 1.0);}
void setOneSettingFromWidget(IntSetting * setting, QSpinBox * spinBox) {setting->val = spinBox->value();}
void setOneSettingFromWidget(QColor * setting, ColourButton * button) {*setting = button->m_colour;}
void setOneSettingFromWidget(SciNotSetting * setting, QDoubleSpinBox * coefficientSpinBox, QSpinBox * exponentSpinBox) {setting->val = SciNot(coefficientSpinBox->value(), exponentSpinBox->value());}
void setOneSettingFromWidget(bool * setting, QCheckBox * checkBox) {*setting = checkBox->isChecked();}
void setOneWidgetFromSetting(QColor * setting, ColourButton * button) {button->setColour(*setting);}
void setOneWidgetFromSetting(bool * setting, QCheckBox * checkBox) {checkBox->setChecked(*setting);}
void setOneWidgetFromSetting(FloatSetting * setting, QDoubleSpinBox * spinBox, bool percentage)
{
    spinBox->setMinimum(setting->min * (percentage ? 100.0 : 1.0));
    spinBox->setMaximum(setting->max * (percentage ? 100.0 : 1.0));
    spinBox->setValue(setting->val * (percentage ? 100.0 : 1.0));
}
void setOneWidgetFromSetting(IntSetting * setting, QSpinBox * spinBox)
{
    spinBox->setMinimum(setting->min);
    spinBox->setMaximum(setting->max);
    spinBox->setValue(setting->val);
}
void setOneWidgetFromSetting(SciNotSetting * setting, QDoubleSpinBox * coefficientSpinBox, QSpinBox * exponentSpinBox)
{
    coefficientSpinBox->setMinimum(setting->min.getCoefficient());
    coefficientSpinBox->setMaximum(setting->max.getCoefficient());
    coefficientSpinBox->setValue(setting->val.getCoefficient());
    exponentSpinBox->setMinimum(setting->min.getExponent());
    exponentSpinBox->setMaximum(setting->max.getExponent());
    exponentSpinBox->setValue(setting->val.getExponent());
}


void SettingsDialog::setWidgetsFromSettings()
{
    loadOrSaveSettingsToOrFromWidgets(true, g_settings.data());

    enableDisableDepthWidgets();
    checkBoxesChanged();
    lengthDiscrepancySpinBoxChanged();
}


void SettingsDialog::setSettingsFromWidgets()
{
    loadOrSaveSettingsToOrFromWidgets(false, g_settings.data());

    //The highlighted outline thickness should always be a bit
    //bigger than the outlineThickness.
    g_settings->selectionThickness = g_settings->outlineThickness + 1.0;
}



void SettingsDialog::loadOrSaveSettingsToOrFromWidgets(bool setWidgets, Settings * settings)
{
    void (*doubleFunctionPointer)(FloatSetting *, QDoubleSpinBox *, bool);
    void (*intFunctionPointer)(IntSetting *, QSpinBox *);
    void (*colourFunctionPointer)(QColor *, ColourButton *);
    void (*sciNotFunctionPointer)(SciNotSetting *, QDoubleSpinBox *, QSpinBox *);
    void (*checkBoxFunctionPointer)(bool *, QCheckBox *);

    if (setWidgets)
    {
        doubleFunctionPointer = setOneWidgetFromSetting;
        intFunctionPointer = setOneWidgetFromSetting;
        colourFunctionPointer = setOneWidgetFromSetting;
        sciNotFunctionPointer = setOneWidgetFromSetting;
        checkBoxFunctionPointer = setOneWidgetFromSetting;
    }
    else
    {
        doubleFunctionPointer = setOneSettingFromWidget;
        intFunctionPointer = setOneSettingFromWidget;
        colourFunctionPointer = setOneSettingFromWidget;
        sciNotFunctionPointer = setOneSettingFromWidget;
        checkBoxFunctionPointer = setOneSettingFromWidget;
    }

    doubleFunctionPointer(&settings->manualNodeLengthPerMegabase, ui->nodeLengthPerMegabaseManualSpinBox, false);
    doubleFunctionPointer(&settings->minimumNodeLength, ui->minimumNodeLengthSpinBox, false);
    doubleFunctionPointer(&settings->edgeLength, ui->edgeLengthSpinBox, false);
    doubleFunctionPointer(&settings->doubleModeNodeSeparation, ui->doubleModeNodeSeparationSpinBox, false);
    doubleFunctionPointer(&settings->nodeSegmentLength, ui->nodeSegmentLengthSpinBox, false);
    doubleFunctionPointer(&settings->componentSeparation, ui->componentSeparationSpinBox, false);
    doubleFunctionPointer(&settings->depthEffectOnWidth, ui->depthEffectOnWidthSpinBox, true);
    doubleFunctionPointer(&settings->depthPower, ui->depthPowerSpinBox, false);
    doubleFunctionPointer(&settings->edgeWidth, ui->edgeWidthSpinBox, false);
    doubleFunctionPointer(&settings->linkWidth, ui->linkWidthSpinBox, false);
    doubleFunctionPointer(&settings->outlineThickness, ui->outlineThicknessSpinBox, false);
    doubleFunctionPointer(&settings->textOutlineThickness, ui->textOutlineThicknessSpinBox, false);
    colourFunctionPointer(&settings->edgeColour, ui->edgeColourButton);
    colourFunctionPointer(&settings->outlineColour, ui->outlineColourButton);
    colourFunctionPointer(&settings->selectionColour, ui->selectionColourButton);
    colourFunctionPointer(&settings->textColour, ui->textColourButton);
    colourFunctionPointer(&settings->textOutlineColour, ui->textOutlineColourButton);
    intFunctionPointer(&settings->randomColourPositiveSaturation, ui->randomColourPositiveSaturationSpinBox);
    intFunctionPointer(&settings->randomColourNegativeSaturation, ui->randomColourNegativeSaturationSpinBox);
    intFunctionPointer(&settings->randomColourPositiveLightness, ui->randomColourPositiveLightnessSpinBox);
    intFunctionPointer(&settings->randomColourNegativeLightness, ui->randomColourNegativeLightnessSpinBox);
    intFunctionPointer(&settings->randomColourPositiveOpacity, ui->randomColourPositiveOpacitySpinBox);
    intFunctionPointer(&settings->randomColourNegativeOpacity, ui->randomColourNegativeOpacitySpinBox);
    colourFunctionPointer(&settings->uniformPositiveNodeColour, ui->uniformPositiveNodeColourButton);
    colourFunctionPointer(&settings->uniformNegativeNodeColour, ui->uniformNegativeNodeColourButton);
    colourFunctionPointer(&settings->uniformNodeSpecialColour, ui->uniformNodeSpecialColourButton);
    doubleFunctionPointer(&settings->lowDepthValue, ui->lowDepthValueSpinBox, false);
    doubleFunctionPointer(&settings->highDepthValue, ui->highDepthValueSpinBox, false);
    colourFunctionPointer(&settings->grayColor, ui->noBlastHitsColourButton);
    intFunctionPointer(&settings->contiguitySearchSteps, ui->contiguitySearchDepthSpinBox);
    colourFunctionPointer(&settings->contiguousStrandSpecificColour, ui->contiguousStrandSpecificColourButton);
    colourFunctionPointer(&settings->contiguousEitherStrandColour, ui->contiguousEitherStrandColourButton);
    colourFunctionPointer(&settings->maybeContiguousColour, ui->maybeContiguousColourButton);
    colourFunctionPointer(&settings->notContiguousColour, ui->notContiguousColourButton);
    colourFunctionPointer(&settings->contiguityStartingColour, ui->contiguityStartingColourButton);
    intFunctionPointer(&settings->maxHitsForQueryPath, ui->maxHitsForQueryPathSpinBox);
    intFunctionPointer(&settings->maxQueryPathNodes, ui->maxPathNodesSpinBox);
    doubleFunctionPointer(&settings->minQueryCoveredByPath, ui->minQueryCoveredByPathSpinBox, true);
    checkBoxFunctionPointer(&settings->minQueryCoveredByHits.on, ui->minQueryCoveredByHitsCheckBox);
    doubleFunctionPointer(&settings->minQueryCoveredByHits, ui->minQueryCoveredByHitsSpinBox, true);
    checkBoxFunctionPointer(&settings->minMeanHitIdentity.on, ui->minMeanHitIdentityCheckBox);
    doubleFunctionPointer(&settings->minMeanHitIdentity, ui->minMeanHitIdentitySpinBox, true);
    checkBoxFunctionPointer(&settings->maxEValueProduct.on, ui->maxEValueProductCheckBox);
    sciNotFunctionPointer(&settings->maxEValueProduct, ui->maxEValueCoefficientSpinBox, ui->maxEValueExponentSpinBox);
    checkBoxFunctionPointer(&settings->minLengthPercentage.on, ui->minLengthPercentageCheckBox);
    doubleFunctionPointer(&settings->minLengthPercentage, ui->minLengthPercentageSpinBox, true);
    checkBoxFunctionPointer(&settings->maxLengthPercentage.on, ui->maxLengthPercentageCheckBox);
    doubleFunctionPointer(&settings->maxLengthPercentage, ui->maxLengthPercentageSpinBox, true);
    checkBoxFunctionPointer(&settings->minLengthBaseDiscrepancy.on, ui->minLengthBaseDiscrepancyCheckBox);
    intFunctionPointer(&settings->minLengthBaseDiscrepancy, ui->minLengthBaseDiscrepancySpinBox);
    checkBoxFunctionPointer(&settings->maxLengthBaseDiscrepancy.on, ui->maxLengthBaseDiscrepancyCheckBox);
    intFunctionPointer(&settings->maxLengthBaseDiscrepancy, ui->maxLengthBaseDiscrepancySpinBox);

    //A couple of settings are not in a spin box, check box or colour button, so
    //they have to be done manually, not with those function pointers.
    if (setWidgets)
    {
        ui->graphLayoutQualitySlider->setValue(settings->graphLayoutQuality);
        ui->linearLayoutOffRadioButton->setChecked(!settings->linearLayout);
        ui->linearLayoutOnRadioButton->setChecked(settings->linearLayout);
        ui->antialiasingOffRadioButton->setChecked(!settings->antialiasing);
        ui->antialiasingOnRadioButton->setChecked(settings->antialiasing);
        ui->antialiasingOffRadioButton->setChecked(!settings->antialiasing);
        ui->singleNodeArrowHeadsOnRadioButton->setChecked(settings->arrowheadsInSingleMode);
        ui->singleNodeArrowHeadsOffRadioButton->setChecked(!settings->arrowheadsInSingleMode);
        ui->depthValueAutoRadioButton->setChecked(settings->autoDepthValue);
        ui->depthValueManualRadioButton->setChecked(!settings->autoDepthValue);
        nodeLengthPerMegabaseManualChanged();
        ui->nodeLengthPerMegabaseAutoLabel->setText(formatDoubleForDisplay(settings->autoNodeLengthPerMegabase, 1));
        ui->lowDepthAutoValueLabel2->setText(formatDoubleForDisplay(g_assemblyGraph->m_firstQuartileDepth, 2));
        ui->highDepthAutoValueLabel2->setText(formatDoubleForDisplay(g_assemblyGraph->m_thirdQuartileDepth, 2));
        ui->nodeLengthPerMegabaseAutoRadioButton->setChecked(settings->nodeLengthMode == AUTO_NODE_LENGTH);
        ui->nodeLengthPerMegabaseManualRadioButton->setChecked(settings->nodeLengthMode != AUTO_NODE_LENGTH);
        ui->positionVisibleRadioButton->setChecked(!settings->positionTextNodeCentre);
        ui->positionCentreRadioButton->setChecked(settings->positionTextNodeCentre);
        ui->colorMapCombo->setCurrentIndex(int(settings->colorMap));
    }
    else
    {
        settings->graphLayoutQuality = ui->graphLayoutQualitySlider->value();
        settings->linearLayout = ui->linearLayoutOnRadioButton->isChecked();
        settings->antialiasing = ui->antialiasingOnRadioButton->isChecked();
        settings->arrowheadsInSingleMode = ui->singleNodeArrowHeadsOnRadioButton->isChecked();
        settings->autoDepthValue = ui->depthValueAutoRadioButton->isChecked();
        if (ui->nodeLengthPerMegabaseAutoRadioButton->isChecked())
            settings->nodeLengthMode = AUTO_NODE_LENGTH;
        else
            settings->nodeLengthMode = MANUAL_NODE_LENGTH;
        settings->positionTextNodeCentre = ui->positionCentreRadioButton->isChecked();
        settings->colorMap = ColorMap(ui->colorMapCombo->currentIndex());
        settings->nodeColorer->reset();
    }
}


void SettingsDialog::restoreDefaults()
{
    Settings defaultSettings;

    //The auto base pairs per segment is the only setting we don't want to
    //restore, as it is calculated from the graph.
    defaultSettings.autoNodeLengthPerMegabase = g_settings->autoNodeLengthPerMegabase;

    loadOrSaveSettingsToOrFromWidgets(true, &defaultSettings);
}


void SettingsDialog::enableDisableDepthWidgets()
{
    bool manual = ui->depthValueManualRadioButton->isChecked();

    ui->depthManualWidget->setEnabled(manual);
    ui->depthAutoWidget->setEnabled(!manual);
}


void SettingsDialog::accept()
{
    if (ui->lowDepthValueSpinBox->value() > ui->highDepthValueSpinBox->value())
        QMessageBox::warning(this, "Depth value error", "The low depth value cannot be greater than the high depth value.");

    else if (ui->minLengthPercentageCheckBox->isChecked() &&
             ui->maxLengthPercentageCheckBox->isChecked() &&
             ui->minLengthPercentageSpinBox->value() > ui->maxLengthPercentageSpinBox->value())
        QMessageBox::warning(this, "BLAST query path length value error", "In the 'BLAST query paths' section, the minimum "
                                                                          "path length value cannot be larger than "
                                                                           "the maximum path length value.");

    else if (ui->minLengthBaseDiscrepancyCheckBox->isChecked() &&
             ui->maxLengthBaseDiscrepancyCheckBox->isChecked() &&
             ui->minLengthBaseDiscrepancySpinBox->value() > ui->maxLengthBaseDiscrepancySpinBox->value())
        QMessageBox::warning(this, "BLAST query length discrepancy value error", "In the 'BLAST query paths' section, the minimum "
                                                                                 "length discrepancy value cannot be larger than "
                                                                                 "the maximum length discrepancy value.");

    else
        QDialog::accept();
}


void SettingsDialog::nodeLengthPerMegabaseManualChanged()
{
    bool manual = ui->nodeLengthPerMegabaseManualRadioButton->isChecked();
    ui->nodeLengthPerMegabaseManualSpinBox->setEnabled(manual);
    ui->nodeLengthPerMegabaseAutoLabel->setEnabled(!manual);
}


void SettingsDialog::updateNodeWidthVisualAid()
{
    ui->nodeWidthVisualAid->m_depthEffectOnWidth = ui->depthEffectOnWidthSpinBox->value() / 100.0;
    ui->nodeWidthVisualAid->m_depthPower = ui->depthPowerSpinBox->value();

    ui->nodeWidthVisualAid->update();
}


void SettingsDialog::colourSliderChanged()
{
    ui->randomColourPositiveOpacitySpinBox->setValue(ui->randomColourPositiveOpacitySlider->value());
    ui->randomColourNegativeOpacitySpinBox->setValue(ui->randomColourNegativeOpacitySlider->value());
    ui->randomColourPositiveSaturationSpinBox->setValue(ui->randomColourPositiveSaturationSlider->value());
    ui->randomColourNegativeSaturationSpinBox->setValue(ui->randomColourNegativeSaturationSlider->value());
    ui->randomColourPositiveLightnessSpinBox->setValue(ui->randomColourPositiveLightnessSlider->value());
    ui->randomColourNegativeLightnessSpinBox->setValue(ui->randomColourNegativeLightnessSlider->value());
}

void SettingsDialog::colourSpinBoxChanged()
{
    ui->randomColourPositiveOpacitySlider->setValue(ui->randomColourPositiveOpacitySpinBox->value());
    ui->randomColourNegativeOpacitySlider->setValue(ui->randomColourNegativeOpacitySpinBox->value());
    ui->randomColourPositiveSaturationSlider->setValue(ui->randomColourPositiveSaturationSpinBox->value());
    ui->randomColourNegativeSaturationSlider->setValue(ui->randomColourNegativeSaturationSpinBox->value());
    ui->randomColourPositiveLightnessSlider->setValue(ui->randomColourPositiveLightnessSpinBox->value());
    ui->randomColourNegativeLightnessSlider->setValue(ui->randomColourNegativeLightnessSpinBox->value());
}

void SettingsDialog::checkBoxesChanged()
{
    ui->minQueryCoveredByHitsSpinBox->setEnabled(ui->minQueryCoveredByHitsCheckBox->isChecked());
    ui->minMeanHitIdentitySpinBox->setEnabled(ui->minMeanHitIdentityCheckBox->isChecked());
    ui->maxEValueCoefficientSpinBox->setEnabled(ui->maxEValueProductCheckBox->isChecked());
    ui->maxEValueExponentSpinBox->setEnabled(ui->maxEValueProductCheckBox->isChecked());
    ui->minLengthPercentageSpinBox->setEnabled(ui->minLengthPercentageCheckBox->isChecked());
    ui->maxLengthPercentageSpinBox->setEnabled(ui->maxLengthPercentageCheckBox->isChecked());
    ui->minLengthBaseDiscrepancySpinBox->setEnabled(ui->minLengthBaseDiscrepancyCheckBox->isChecked());
    ui->maxLengthBaseDiscrepancySpinBox->setEnabled(ui->maxLengthBaseDiscrepancyCheckBox->isChecked());
}

//This function adds or removes the '+' prefix from the length discrepancy
//spin boxes, based on whether or not they hold a positive value.
void SettingsDialog::lengthDiscrepancySpinBoxChanged()
{
    if (ui->minLengthBaseDiscrepancySpinBox->value() > 0)
        ui->minLengthBaseDiscrepancySpinBox->setPrefix("+");
    else
        ui->minLengthBaseDiscrepancySpinBox->setPrefix("");

    if (ui->maxLengthBaseDiscrepancySpinBox->value() > 0)
        ui->maxLengthBaseDiscrepancySpinBox->setPrefix("+");
    else
        ui->maxLengthBaseDiscrepancySpinBox->setPrefix("");
}
