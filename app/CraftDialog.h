#pragma once

#include <QDialog>
#include <QString>

#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGridLayout;
class QLabel;
class QVBoxLayout;

// Game data the crafter draws from (data/json): gear_types.json, vorax.json, crafting.json, legendaries.json.
struct CraftData {
    nlohmann::json gearTypes;
    nlohmann::json vorax;
    nlohmann::json crafting;
    nlohmann::json legendaries;
};

// One modifier text with rolled ranges, "+(154–220) Max Life", and a value chosen for each range.
class RollEditor : public QWidget {
    Q_OBJECT

public:
    explicit RollEditor(QWidget* parent = nullptr);
    void setText(const QString& text, const std::vector<double>& values = {});
    QString text() const { return text_; }
    std::vector<double> values() const { return values_; }
    QString resolved() const;   // the text with each range replaced by its value

signals:
    void changed();

private:
    QString text_;
    std::vector<double> values_;
    QGridLayout* grid_;
    QLabel* preview_;
};

// Builds one item the way the game does: a gear type and base, item level, up to 3 prefixes and 3
// suffixes from that type's pools (tier limited by item level, at most 2 Advanced and 2 Ultimate),
// base affixes, corrosion, Blending (belts), TOWER Sequence (weapons) and organ grafts (Vorax limbs);
// or a legendary with each rolled value set. The item keeps a "craft" block so it can be edited again,
// and "lines" with the resolved modifier text the engine reads.
class CraftDialog : public QDialog {
    Q_OBJECT

public:
    CraftDialog(const CraftData& data, const QString& slot, nlohmann::json item, QWidget* parent = nullptr);
    nlohmann::json item() const;

private:
    struct AffixRow {
        QComboBox* family;
        QComboBox* tier;
        QComboBox* upgrade;   // corrosion upgrade: 0, +1 or +2 tiers
        RollEditor* roll;
        QLabel* label;
    };

    const nlohmann::json* typeData() const;
    void fillCorrosion();
    void fillTypes();
    void typeChanged();
    void refreshAffixChoices();
    void refreshTiers(AffixRow& row);
    void updateRoll(AffixRow& row);
    void addBaseAffix(const QString& text, const std::vector<double>& values = {});
    void fillOrganLegendaries();
    void fillOrganLines();
    void fillOrganLine(int i);
    void fillLegendaryLines(const nlohmann::json& saved = {});
    void refreshLegendaryCorrosion();
    const nlohmann::json* legendaryData(const std::string& name) const;
    QString legendaryGearType(const std::string& name) const;
    QString organText(int i) const;
    void updateSummary();
    void loadCraft(const nlohmann::json& craft);
    nlohmann::json craftJson() const;
    QStringList resolvedLines() const;

    const CraftData& data_;
    QString slot_;
    nlohmann::json item_;
    bool loading_ = false;

    QComboBox* kind_;
    QComboBox* type_;
    QComboBox* base_;
    QComboBox* ilvl_;
    QComboBox* corrosion_;
    QComboBox* blending_;
    QComboBox* tower_;
    QComboBox* legendary_;
    QWidget* legendaryMutationBox_;
    QComboBox* legendaryMutation_;
    RollEditor* legendaryMutationRoll_;
    QVBoxLayout* body_;
    QWidget* affixPage_;
    QWidget* legendaryPage_;
    QVBoxLayout* legendaryLines_;
    QVBoxLayout* baseAffixBox_;
    QWidget* organPage_;
    QComboBox* organLegendary_;   // both grafted lines come from this one legendary
    QComboBox* organLine_[2];
    QCheckBox* organCorroded_[2];
    RollEditor* organRoll_[2];
    QLabel* summary_;
    std::vector<AffixRow> prefixes_;
    std::vector<AffixRow> suffixes_;
    std::vector<RollEditor*> baseAffixes_;
    std::vector<RollEditor*> legendaryImplicit_;
    std::vector<RollEditor*> legendaryRolls_;
    std::vector<QCheckBox*> legendaryCorroded_;
    nlohmann::json legendaryLineData_ = nlohmann::json::array();   // {text, corroded} per line
    RollEditor* blendingRoll_;
    RollEditor* towerRoll_;
};
