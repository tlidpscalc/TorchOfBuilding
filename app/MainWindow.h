#pragma once

#include "tob/Build.h"
#include "tob/GameData.h"

#include <QMainWindow>
#include <QString>

#include <map>
#include <string>

#include <nlohmann/json.hpp>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTextBrowser;

// First usable UI: open a build file, see its DPS and breakdown, toggle the situational config
// (only the flags and values the build's own modifiers check), add custom modifier lines, and list
// every line with how the parser read it. Talent, trait and gear editors come later.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void openBuild();
    void loadBuildPath(const QString& path);
    void rebuildConfigPanel();
    void recalculate();
    void showLines();
    tob::BuildState loadState() const;

    tob::GameData data_;
    QString dataDir_;
    QString buildPath_;
    nlohmann::json build_;
    bool loaded_ = false;
    bool updating_ = false;

    QLabel* buildLabel_;
    QFormLayout* flagsForm_;
    QFormLayout* varsForm_;
    QDoubleSpinBox* enemyRes_;
    QDoubleSpinBox* armorPhys_;
    QDoubleSpinBox* armorNonPhys_;
    QPlainTextEdit* custom_;
    QTextBrowser* output_;
    QTableWidget* lines_;
    QComboBox* lineFilter_;
    std::map<std::string, QCheckBox*> flags_;
    std::map<std::string, QDoubleSpinBox*> vars_;
    tob::BuildState state_;
};
