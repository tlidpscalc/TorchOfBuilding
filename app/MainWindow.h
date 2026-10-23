#pragma once

#include <QMainWindow>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QTextBrowser;

// First UI milestone: a single-skill sandbox. Enter weapon/skill numbers and custom modifier
// lines, get DPS with a full breakdown. Talent tree, traits and gear panels come later.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void recalculate();

    QLineEdit* skillTags_;
    QDoubleSpinBox* weaponMin_;
    QDoubleSpinBox* weaponMax_;
    QDoubleSpinBox* weaponAps_;
    QDoubleSpinBox* weaponCrit_;
    QDoubleSpinBox* weaponPct_;
    QDoubleSpinBox* ade_;
    QComboBox* spellType_;
    QDoubleSpinBox* spellMin_;
    QDoubleSpinBox* spellMax_;
    QDoubleSpinBox* spellCastSpeed_;
    QDoubleSpinBox* spellCrit_;
    QDoubleSpinBox* hitsPerUse_;
    QComboBox* convertTo_;
    QDoubleSpinBox* convertPct_;
    QDoubleSpinBox* enemyRes_;
    QDoubleSpinBox* armorPhys_;
    QDoubleSpinBox* armorNonPhys_;
    QPlainTextEdit* mods_;
    QPlainTextEdit* config_;
    QTextBrowser* output_;
};
