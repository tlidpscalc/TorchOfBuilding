#pragma once

#include "CraftDialog.h"

#include <QWidget>

#include <nlohmann/json.hpp>

class QComboBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QVBoxLayout;

// Hero, trait tree, the three trait picks and the three Hero Memories that activate them (Origin ->
// Discipline -> Progress, unlocked at 45/60/75). Each pick's trait level comes from its memory:
// +1 at memory levels 0, 30 and 50, +2 from a "+2 to Hero Trait Level" fixed affix (owner).
// A memory is either built from its pools (rarity, level, base stat, fixed and random affixes) or kept as
// custom lines with a set trait level, as older build files have it.
//
// Build file: "hero", "traits" {"tree", "picks", "nodes"}, "hero_memories" [{slot, name, lines,
// trait_level_bonus, "memory": {...}}]. "nodes" (name + level) is what the engine reads.
class HeroEditor : public QWidget {
    Q_OBJECT

public:
    HeroEditor(const nlohmann::json& traitData, const nlohmann::json& crafting, QWidget* parent = nullptr);
    void setBuild(nlohmann::json* build);

    // Base stat value of a memory: the attribute curve for its rarity and level, scaled to the stat.
    static double baseStatValue(const std::string& statText, int rarity, int level);

signals:
    void changed();

private:
    struct AffixRow {
        QComboBox* family;
        QComboBox* tier;
        RollEditor* roll;
    };
    struct MemoryPanel {
        QComboBox* kind;          // Custom lines / Built from pools
        QWidget* customPage;
        QPlainTextEdit* customLines;
        QSpinBox* customLevel;
        QWidget* poolPage;
        QComboBox* rarity;
        QSpinBox* level;
        QComboBox* baseStat;
        QLabel* baseValue;
        AffixRow fixed[2];
        AffixRow random[2];
        QLabel* summary;
    };

    const nlohmann::json* tree() const;
    nlohmann::json& memory(int i);
    void migrate();
    void refreshTrees();
    void refreshPicks();
    void showMemory(int i);
    void refreshAffixRows(int i);
    void memoryEdited(int i);
    void writeTraits();
    int traitLevel(int i);
    QStringList memoryLines(int i) const;

    const nlohmann::json& traitData_;
    const nlohmann::json& crafting_;
    nlohmann::json* build_ = nullptr;
    bool updating_ = false;

    QComboBox* hero_;
    QComboBox* tree_;
    QLabel* baseTrait_;
    QComboBox* picks_[3];
    QComboBox* picksB_[3];
    QLabel* pickLevels_[3];
    MemoryPanel panels_[3];
};
