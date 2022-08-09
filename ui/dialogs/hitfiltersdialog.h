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

#pragma once

#include <QDialog>

namespace Ui {
class HitFiltersDialog;
}

class HitFiltersDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HitFiltersDialog(QWidget *parent = nullptr);
    ~HitFiltersDialog() override;

    void setWidgetsFromSettings();
    void setSettingsFromWidgets();

    static QString getFilterText();

private:
    Ui::HitFiltersDialog *ui;

private slots:
    void checkBoxesChanged();
};
