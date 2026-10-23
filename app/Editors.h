#pragma once

#include "CraftDialog.h"

#include <QStringList>
#include <QWidget>

#include <nlohmann/json.hpp>

class QCheckBox;
class QLabel;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

// The editors change the build JSON in place and emit changed(); the window recalculates from it,
// so saving writes back exactly what is shown.

// Main skill, active and passive skills, and each skill's supports.
class SkillsEditor : public QWidget {
    Q_OBJECT

public:
    SkillsEditor(const nlohmann::json& skillData, QWidget* parent = nullptr);
    void setBuild(nlohmann::json* build);

signals:
    void changed();

private:
    nlohmann::json* selectedSkill();
    nlohmann::json* selectedSupport();
    void refresh();
    void showSupports();
    void showMedium();
    void mediumEdited();
    QString mediumText(const nlohmann::json& card, const QString& tier, const QString& bonus) const;

    const nlohmann::json& skillData_;
    nlohmann::json* build_ = nullptr;
    QStringList skillNames_;
    QStringList supportNames_;
    QComboBox* main_;
    QListWidget* skills_;
    QCheckBox* enabled_;
    QListWidget* supports_;
    QWidget* mediumBox_;      // Activation Medium: tier, bonus and rolls
    QComboBox* mediumTier_;
    QComboBox* mediumBonus_;
    RollEditor* mediumRoll_;
    bool updating_ = false;
};

// Gear: an inventory of items (crafted, Vorax, legendary or custom lines) and a dropdown per equipment
// slot choosing which inventory item is worn, so gear can be swapped on the fly. The build keeps
// "inventory" (items with an "id") and "equipped" (slot -> id); the worn items are also written to
// "items", which is what the engine reads.
class GearEditor : public QWidget {
    Q_OBJECT

public:
    GearEditor(const CraftData& data, QWidget* parent = nullptr);
    void setBuild(nlohmann::json* build);

signals:
    void changed();

private:
    nlohmann::json& inventory();
    nlohmann::json* selectedItem();
    nlohmann::json* itemById(long long id);
    long long nextId();
    void migrate();
    void syncEquipped();
    void refresh();
    void showItem();
    void newItem();
    void editItem();
    void duplicateItem();
    void deleteItem();
    bool fits(const QString& slot, const nlohmann::json& item) const;
    bool isTwoHanded(const nlohmann::json& item) const;

    nlohmann::json* build_ = nullptr;
    const CraftData& data_;
    std::vector<std::pair<QString, QComboBox*>> equip_;   // slot name -> its dropdown
    QListWidget* items_;
    QLineEdit* name_;
    QPlainTextEdit* lines_;
    QLabel* warning_;
    bool updating_ = false;
};
