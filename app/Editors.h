#pragma once

#include "CraftDialog.h"

#include <QStringList>
#include <QWidget>

#include <nlohmann/json.hpp>

class QButtonGroup;
class QCheckBox;
class QGroupBox;
class QSpinBox;
class QLabel;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

// The editors change the build JSON in place and emit changed(); the window recalculates from it,
// so saving writes back exactly what is shown.

// Skills as the game slots them: 5 active skill slots (slot 1 is the main skill), passive skills, and 5
// support slots per skill. Slot-bound supports only appear in their slot: Activation Mediums in 1, the
// skill's Magnificent canvas supports in 3, its Noble ones in 5. Mediums pick tier, bonus and rolls;
// canvas supports pick stars and rolls.
class SkillsEditor : public QWidget {
    Q_OBJECT

public:
    SkillsEditor(const nlohmann::json& skillData, const nlohmann::json& traitData, QWidget* parent = nullptr);
    void setBuild(nlohmann::json* build);
    // Supports a hero trait puts in a fixed slot, e.g. "Tanglable Main Active Spell Skills are supported
    // by Clockwork Core, replacing the support skill in the second support skill slot". Returns true if
    // the build changed.
    static bool applyTraitSupports(nlohmann::json& build, const nlohmann::json& skillData, const nlohmann::json& traitData);
    // Moves a build's skills and supports into the slots (used on load; keeps every entry).
    static void normalize(nlohmann::json& build, const nlohmann::json& skillData);

signals:
    void changed();

private:
    void selectSkill(const std::string& kind, int index);
    nlohmann::json* selectedSkill();
    nlohmann::json* selectedSupport();
    void refresh();
    void showSupports();
    void showSupportDetails();
    QStringList supportsForSlot(int k, const QString& skill) const;
    void initSupport(nlohmann::json& sup);
    void storeCanvasLines(nlohmann::json& sup);
    void showMedium();
    void mediumEdited();
    QString mediumText(const nlohmann::json& card, const QString& tier, const QString& bonus) const;

    bool traitLocked(int supportSlot) const;

    const nlohmann::json& skillData_;
    const nlohmann::json& traitData_;
    nlohmann::json* build_ = nullptr;
    QStringList skillNames_;
    QStringList supportNames_;
    std::string selKind_ = "active";
    int selIndex_ = 0;
    int selSupport_ = 0;
    QButtonGroup* skillGroup_;
    QComboBox* activeSlots_[5];
    QComboBox* passiveSlots_[4];
    QCheckBox* passiveEnabled_[4];
    QGroupBox* supportBox_;
    QButtonGroup* supportGroup_;
    QComboBox* supportSlots_[5];
    QGroupBox* mediumBox_;
    QComboBox* mediumTier_;
    QComboBox* mediumBonus_;
    RollEditor* mediumRoll_;
    QGroupBox* canvasBox_;
    QSpinBox* canvasStars_;
    RollEditor* canvasRoll_;
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
