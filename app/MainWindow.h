#pragma once

#include "CraftDialog.h"
#include "tob/Build.h"
#include "tob/GameData.h"

#include <QMainWindow>
#include <QString>

#include <map>
#include <string>

#include <nlohmann/json.hpp>

class GearEditor;
class HeroEditor;
class TalentEditor;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QTableWidget;
class QTextBrowser;
class QTimer;
class SkillsEditor;

// Build planner window. The build JSON is the single source of truth: the editors (skills, gear,
// config) change it in place, the engine recalculates from it, and Save writes it back.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void newBuild();
    void openBuild();
    bool saveBuild();
    bool saveBuildAs();
    bool confirmDiscard();
    void loadBuildPath(const QString& path);
    void setBuild(nlohmann::json build);
    void buildEdited(bool linesChanged);
    void setDirty(bool dirty);
    void rebuildConfigPanel();
    void setConfigFlag(const std::string& name, bool on);
    void setConfigVar(const std::string& name, double value);
    void setEnemy();
    void recalculate();
    void showLines();

    tob::GameData data_;
    CraftData craftData_;
    QString dataDir_;
    QString buildPath_;
    nlohmann::json build_;
    bool loaded_ = false;
    bool dirty_ = false;
    bool updating_ = false;
    bool rebuildPending_ = false;
    bool linesStale_ = true;

    QLabel* buildLabel_;
    SkillsEditor* skillsEditor_ = nullptr;
    GearEditor* gearEditor_ = nullptr;
    HeroEditor* heroEditor_ = nullptr;
    TalentEditor* talentEditor_ = nullptr;
    QFormLayout* flagsForm_;
    QFormLayout* varsForm_;
    QDoubleSpinBox* enemyRes_;
    QDoubleSpinBox* armorPhys_;
    QDoubleSpinBox* armorNonPhys_;
    QTextBrowser* output_;
    QTableWidget* lines_;
    QComboBox* lineFilter_;
    QTimer* recalcTimer_;
    tob::BuildState state_;
};
