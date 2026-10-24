#include "HeroEditor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

const char* kMemoryNames[] = {"Memory of Origin", "Memory of Discipline", "Memory of Progress"};
const char* kMemorySlots[] = {"Origin", "Discipline", "Progress"};
const char* kPickLevels[] = {"45", "60", "75"};
const char* kRarities[] = {"Normal", "Magic", "Rare", "Epic", "Ultimate"};

// Per rarity (owner): Intelligence at level 1 and at the level cap, the cap, fixed affixes, random
// affixes (gained at memory levels 20 and 40), and the best affix tier that can roll.
constexpr double kStart[] = {10, 17, 23, 30, 37};
constexpr double kMax[] = {30, 50, 70, 90, 110};
constexpr int kCap[] = {10, 20, 30, 40, 50};
constexpr int kFixed[] = {0, 1, 1, 2, 2};
constexpr int kRandomMax[] = {0, 1, 1, 2, 2};
constexpr int kBestTier[] = {9, 3, 2, 1, 0};

int randomCount(int rarity, int level) {
    return std::min(kRandomMax[rarity], (level >= 20 ? 1 : 0) + (level >= 40 ? 1 : 0));
}

// "+(39–45)% Skill Area" -> "#% Skill Area": rows of one affix share this key across tiers.
QString familyKey(const QString& text) {
    static const QRegularExpression number(QStringLiteral("\\(\\s*-?[\\d.]+\\s*[–-]\\s*-?[\\d.]+\\s*\\)|-?[\\d.]+"));
    QString k = text;
    k.replace(number, "#");
    return k;
}

const nlohmann::json& pool(const nlohmann::json& crafting, int memory, const char* kind) {
    static const nlohmann::json empty = nlohmann::json::array();
    const auto& memories = crafting["hero_memories"];
    if (!memories.contains(kMemoryNames[memory]) || !memories[kMemoryNames[memory]].contains(kind)) return empty;
    return memories[kMemoryNames[memory]][kind];
}

} // namespace

// The attribute curve is a straight line from level 1 to the rarity's cap, rounded to the nearest
// whole number (checked in game at several levels). Other base stats scale from their Ultimate level 50
// value; that is tlidb's top value x 110/90, except Movement Speed (+30%, tlidb already lists the
// Ultimate value) and Damage (+132%; tlidb's +90% is wrong). Minion Damage is ASSUMED to match Damage.
double HeroEditor::baseStatValue(const std::string& statText, int rarity, int level) {
    rarity = std::clamp(rarity, 0, 4);
    level = std::clamp(level, 1, kCap[rarity]);
    const double curve = kStart[rarity] + (kMax[rarity] - kStart[rarity]) * (level - 1) / (kCap[rarity] - 1);
    static const QRegularExpression number(QStringLiteral("[\\d.]+"));
    const QString text = qs(statText);
    const double top = number.match(text).captured(0).toDouble();
    double ultimate50 = top * 110.0 / 90.0;
    if (text.contains("Movement Speed")) ultimate50 = top;
    else if (text.contains("damage", Qt::CaseInsensitive)) ultimate50 = 132.0;
    return std::round(ultimate50 * curve / 110.0);
}

HeroEditor::HeroEditor(const nlohmann::json& traitData, const nlohmann::json& crafting, QWidget* parent)
    : QWidget(parent), traitData_(traitData), crafting_(crafting) {
    auto* layout = new QVBoxLayout(this);

    auto* heroBox = new QGroupBox("Hero and traits");
    auto* heroForm = new QFormLayout(heroBox);
    hero_ = new QComboBox;
    tree_ = new QComboBox;
    baseTrait_ = new QLabel;
    heroForm->addRow("Hero", hero_);
    heroForm->addRow("Trait tree", tree_);
    heroForm->addRow("Base trait", baseTrait_);
    for (int i = 0; i < 3; ++i) {
        auto* row = new QHBoxLayout;
        picks_[i] = new QComboBox;
        picksB_[i] = new QComboBox;   // second choice, for trees whose level groups split in two
        pickLevels_[i] = new QLabel;
        row->addWidget(picks_[i], 1);
        row->addWidget(picksB_[i], 1);
        row->addWidget(pickLevels_[i]);
        heroForm->addRow(QString("Level %1 (%2)").arg(kPickLevels[i], kMemorySlots[i]), row);
    }
    layout->addWidget(heroBox);

    std::set<std::string> heroes;
    for (const auto& t : traitData_) heroes.insert(t.value("hero", ""));
    // Shown as "Escapist | Bing"; tlidb's key ("Escapist|Pin") stays the item data. tlidb names Bing "Pin".
    for (const auto& h : heroes) {
        QString shown = qs(h);
        shown.replace("|Pin", "|Bing");
        shown.replace(QRegularExpression(R"(\s*\|\s*)"), " | ");
        hero_->addItem(shown, qs(h));
    }

    connect(hero_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_ || !build_) return;
        (*build_)["hero"] = hero_->currentData().toString().toStdString();
        refreshTrees();
        (*build_)["traits"]["tree"] = tree_->currentData().toString().toStdString();
        (*build_)["traits"]["picks"] = nlohmann::json::array({"", "", ""});
        (*build_)["traits"]["picks_b"] = nlohmann::json::array({"", "", ""});
        writeTraits();
        refreshPicks();
        emit changed();
    });
    connect(tree_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_ || !build_) return;
        (*build_)["traits"]["tree"] = tree_->currentData().toString().toStdString();
        (*build_)["traits"]["picks"] = nlohmann::json::array({"", "", ""});
        (*build_)["traits"]["picks_b"] = nlohmann::json::array({"", "", ""});
        writeTraits();
        refreshPicks();
        emit changed();
    });
    for (int i = 0; i < 3; ++i)
        connect(picks_[i], &QComboBox::currentIndexChanged, this, [this, i] {
            if (updating_ || !build_) return;
            nlohmann::json& picks = (*build_)["traits"]["picks"];
            picks[i] = picks_[i]->currentData().toString().toStdString();
            writeTraits();
            refreshPicks();   // later picks may no longer be reachable
            emit changed();
        });
    for (int i = 0; i < 3; ++i)
        connect(picksB_[i], &QComboBox::currentIndexChanged, this, [this, i] {
            if (updating_ || !build_) return;
            (*build_)["traits"]["picks_b"][i] = picksB_[i]->currentData().toString().toStdString();
            writeTraits();
            emit changed();
        });

    auto* memoryTabs = new QTabWidget;
    for (int i = 0; i < 3; ++i) {
        MemoryPanel& p = panels_[i];
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        p.kind = new QComboBox;
        p.kind->addItems({"Built from its pools", "Custom lines"});
        v->addWidget(p.kind);

        p.poolPage = new QWidget;
        auto* form = new QFormLayout(p.poolPage);
        p.rarity = new QComboBox;
        for (const char* r : kRarities) p.rarity->addItem(r);
        p.rarity->setCurrentIndex(4);
        p.level = new QSpinBox;
        p.level->setRange(1, 50);
        p.baseStat = new QComboBox;
        p.baseValue = new QLabel;
        form->addRow("Rarity", p.rarity);
        form->addRow("Level", p.level);
        form->addRow("Base stat", p.baseStat);
        form->addRow("", p.baseValue);
        // Listed by name ("Max Life"); the value comes from rarity and level. The tlidb text is the item data.
        for (const auto& b : pool(crafting_, i, "Base Stats")) {
            const QString text = qs(b.value("text", ""));
            QString name = text;
            name.remove(QRegularExpression(R"(^[+-]?[\d.]+%?\s*)"));
            if (!name.isEmpty()) name[0] = name[0].toUpper();
            p.baseStat->addItem(name, text);
        }
        for (int f = 0; f < 2; ++f) {
            AffixRow& r = p.fixed[f];
            r = {new QComboBox, new QComboBox, new RollEditor};
            auto* line = new QHBoxLayout;
            line->addWidget(r.family, 1);
            line->addWidget(r.tier);
            form->addRow(QString("Fixed affix %1").arg(f + 1), line);
            form->addRow("", r.roll);
        }
        for (int f = 0; f < 2; ++f) {
            AffixRow& r = p.random[f];
            r = {new QComboBox, new QComboBox, new RollEditor};
            auto* line = new QHBoxLayout;
            line->addWidget(r.family, 1);
            line->addWidget(r.tier);
            form->addRow(QString("Random affix %1").arg(f + 1), line);
            form->addRow("", r.roll);
        }
        v->addWidget(p.poolPage);

        p.customPage = new QWidget;
        auto* custom = new QFormLayout(p.customPage);
        p.customLines = new QPlainTextEdit;
        p.customLevel = new QSpinBox;
        p.customLevel->setRange(1, 5);
        custom->addRow("Lines", p.customLines);
        custom->addRow("Trait level it gives", p.customLevel);
        v->addWidget(p.customPage);
        p.summary = new QLabel;
        p.summary->setWordWrap(true);
        v->addWidget(p.summary);
        v->addStretch(1);
        memoryTabs->addTab(page, kMemorySlots[i]);

        auto edited = [this, i] {
            if (!updating_) memoryEdited(i);
        };
        connect(p.kind, &QComboBox::currentIndexChanged, this, [this, i] {
            if (updating_) return;
            memoryEdited(i);
            showMemory(i);
        });
        connect(p.rarity, &QComboBox::currentIndexChanged, this, [this, i] {
            if (updating_) return;
            panels_[i].level->setMaximum(kCap[panels_[i].rarity->currentIndex()]);
            refreshAffixRows(i);
            memoryEdited(i);
        });
        connect(p.level, &QSpinBox::valueChanged, this, [this, i] {
            if (updating_) return;
            refreshAffixRows(i);
            memoryEdited(i);
        });
        connect(p.baseStat, &QComboBox::currentIndexChanged, this, edited);
        for (AffixRow* r : {&p.fixed[0], &p.fixed[1], &p.random[0], &p.random[1]}) {
            connect(r->family, &QComboBox::currentIndexChanged, this, [this, i] {
                if (updating_) return;
                refreshAffixRows(i);
                memoryEdited(i);
            });
            connect(r->tier, &QComboBox::currentIndexChanged, this, [this, i] {
                if (updating_) return;
                refreshAffixRows(i);
                memoryEdited(i);
            });
            connect(r->roll, &RollEditor::changed, this, edited);
        }
        connect(p.customLines, &QPlainTextEdit::textChanged, this, edited);
        connect(p.customLevel, &QSpinBox::valueChanged, this, edited);
    }
    layout->addWidget(new QLabel("Hero Memories"));
    layout->addWidget(memoryTabs, 1);
}

void HeroEditor::setBuild(nlohmann::json* build) {
    build_ = build;
    if (!build_) return;
    migrate();
    updating_ = true;
    // The hero of the build's trait tree.
    const std::string slug = (*build_)["traits"].value("tree", "");
    for (const auto& t : traitData_)
        if (t.value("slug", "") == slug) hero_->setCurrentIndex(std::max(0, hero_->findData(qs(t.value("hero", "")))));
    updating_ = false;
    refreshTrees();
    refreshPicks();
    for (int i = 0; i < 3; ++i) showMemory(i);
}

const nlohmann::json* HeroEditor::tree() const {
    if (!build_) return nullptr;
    const std::string slug = (*build_)["traits"].value("tree", "");
    for (const auto& t : traitData_)
        if (t.value("slug", "") == slug) return &t;
    return nullptr;
}

nlohmann::json& HeroEditor::memory(int i) { return (*build_)["hero_memories"][i]; }

// Older build files: picks from the chosen nodes (in order, after the base trait), and one memory entry
// per slot in Origin, Discipline, Progress order, kept as custom lines with their trait level.
void HeroEditor::migrate() {
    nlohmann::json& traits = (*build_)["traits"];
    if (!traits.is_object()) traits = nlohmann::json::object();
    if (!traits.contains("picks")) {
        nlohmann::json picks = nlohmann::json::array();
        const auto& nodes = traits.value("nodes", nlohmann::json::array());
        for (std::size_t n = 1; n < nodes.size() && picks.size() < 3; ++n) picks.push_back(nodes[n].value("name", ""));
        while (picks.size() < 3) picks.push_back("");
        traits["picks"] = picks;
    }
    nlohmann::json old = build_->value("hero_memories", nlohmann::json::array());
    nlohmann::json ordered = nlohmann::json::array();
    for (int i = 0; i < 3; ++i) {
        nlohmann::json entry = {{"slot", kMemorySlots[i]}, {"name", kMemoryNames[i]}, {"lines", nlohmann::json::array()},
                                {"trait_level_bonus", 1}};
        for (const auto& m : old)
            if (m.value("slot", "") == kMemorySlots[i]) entry = m;
        ordered.push_back(entry);
    }
    (*build_)["hero_memories"] = ordered;
}

void HeroEditor::refreshTrees() {
    const bool wasUpdating = updating_;
    updating_ = true;
    const std::string current = build_ ? (*build_)["traits"].value("tree", "") : "";
    tree_->clear();
    for (const auto& t : traitData_)
        if (qs(t.value("hero", "")) == hero_->currentData().toString())
            tree_->addItem(qs(t.value("slug", "")).replace('_', ' '), qs(t.value("slug", "")));
    tree_->setCurrentIndex(std::max(0, tree_->findData(qs(current))));
    updating_ = wasUpdating;
}

// Allowed nodes for each pick. Linked trees: a child of the base trait or of an earlier pick (owner).
// Other trees: one node from the pick's level group (45, 60, 75). When a level group holds two node
// groups (tlidb ids sharing id / 10; only Creative Genius), one node is picked from each, both at the
// same memory's trait level (owner).
void HeroEditor::refreshPicks() {
    const bool wasUpdating = updating_;
    updating_ = true;
    const nlohmann::json* t = tree();
    nlohmann::json& picks = (*build_)["traits"]["picks"];
    nlohmann::json& second = (*build_)["traits"]["picks_b"];
    if (!second.is_array()) second = nlohmann::json::array({"", "", ""});
    std::string base;
    std::map<long long, std::string> nameById;
    std::map<std::string, long long> idByName;
    if (t)
        for (const auto& n : (*t)["nodes"]) {
            nameById[n.value("id", 0LL)] = n.value("name", "");
            idByName[n.value("name", "")] = n.value("id", 0LL);
            if (n.value("required_level", 0) <= 1) base = n.value("name", "");
        }
    baseTrait_->setText(t ? qs(base) + "  (level 1)" : QString());
    const bool linked = t && !t->value("tree_links", nlohmann::json::array()).empty();
    std::set<std::string> taken = {base};
    for (int i = 0; i < 3; ++i) {
        QStringList options, optionsB;
        if (t && linked) {
            for (const auto& link : (*t)["tree_links"]) {
                const std::string parent = nameById[link.value("parent", 0LL)];
                const std::string child = nameById[link.value("child", 0LL)];
                if (taken.contains(parent) && !taken.contains(child)) options << qs(child);
            }
        } else if (t) {
            std::map<long long, QStringList> groups;   // id / 10 -> nodes
            for (const auto& name : t->value("tiers", nlohmann::json::object()).value(kPickLevels[i], nlohmann::json::array()))
                groups[idByName[name.get<std::string>()] / 10] << qs(name.get<std::string>());
            auto g = groups.begin();
            if (g != groups.end()) options = (g++)->second;
            if (g != groups.end() && groups.size() == 2) optionsB = g->second;
            else
                for (; g != groups.end(); ++g) options << g->second;
        }
        options.removeDuplicates();
        picksB_[i]->clear();
        picksB_[i]->addItem("(none)", QString());
        for (const QString& o : optionsB) picksB_[i]->addItem(o, o);
        picksB_[i]->setVisible(!optionsB.isEmpty());
        const QString currentB = qs(second[i].is_string() ? second[i].get<std::string>() : "");
        const int indexB = picksB_[i]->findData(currentB);
        if (indexB < 0 && !currentB.isEmpty()) second[i] = "";
        picksB_[i]->setCurrentIndex(std::max(0, indexB));
        picks_[i]->clear();
        picks_[i]->addItem("(none)", QString());
        for (const QString& o : options) picks_[i]->addItem(o, o);
        const QString current = qs(picks[i].is_string() ? picks[i].get<std::string>() : "");
        int index = picks_[i]->findData(current);
        if (index < 0 && !current.isEmpty()) picks[i] = "";   // no longer reachable
        picks_[i]->setCurrentIndex(std::max(0, index));
        if (index > 0) taken.insert(current.toStdString());
        pickLevels_[i]->setText(index > 0 ? QString("level %1").arg(traitLevel(i)) : QString());
    }
    updating_ = wasUpdating;
    writeTraits();
}

// Trait level a memory gives its pick: +1 at memory levels 0, 30 and 50, +2 from "+2 to Hero Trait
// Level", at most 5 (owner). Custom memories keep the level written in the build file.
int HeroEditor::traitLevel(int i) {
    const nlohmann::json& m = memory(i);
    if (!m.contains("memory")) return std::clamp(m.value("trait_level_bonus", 1), 1, 5);
    const int level = m["memory"].value("level", 1);
    int traitLevel = 1 + (level >= 30 ? 1 : 0) + (level >= 50 ? 1 : 0);
    for (const auto& l : m.value("lines", nlohmann::json::array()))
        if (l.get<std::string>().find("to Hero Trait Level") != std::string::npos) traitLevel += 2;
    return std::min(traitLevel, 5);
}

// "nodes" for the engine: the base trait at level 1, then each pick at its memory's trait level.
void HeroEditor::writeTraits() {
    const nlohmann::json* t = tree();
    nlohmann::json& traits = (*build_)["traits"];
    nlohmann::json nodes = nlohmann::json::array();
    if (t) {
        for (const auto& n : (*t)["nodes"])
            if (n.value("required_level", 0) <= 1) nodes.push_back({{"name", n.value("name", "")}, {"level", 1}});
        const auto& picks = traits["picks"];
        const auto& second = traits.value("picks_b", nlohmann::json::array());
        for (int i = 0; i < 3 && i < static_cast<int>(second.size()); ++i)
            if (second[i].is_string() && !second[i].get<std::string>().empty())
                nodes.push_back({{"name", second[i]}, {"level", traitLevel(i)}});
        for (int i = 0; i < 3 && i < static_cast<int>(picks.size()); ++i)
            if (picks[i].is_string() && !picks[i].get<std::string>().empty())
                nodes.push_back({{"name", picks[i]}, {"level", traitLevel(i)}});
    }
    traits["nodes"] = nodes;
}

void HeroEditor::showMemory(int i) {
    const bool wasUpdating = updating_;
    updating_ = true;
    MemoryPanel& p = panels_[i];
    const nlohmann::json& m = memory(i);
    const bool built = m.contains("memory");
    p.kind->setCurrentIndex(built ? 0 : 1);
    p.poolPage->setVisible(built);
    p.customPage->setVisible(!built);
    QStringList lines;
    for (const auto& l : m.value("lines", nlohmann::json::array())) lines << qs(l.get<std::string>());
    p.customLines->setPlainText(lines.join('\n'));
    p.customLevel->setValue(std::clamp(m.value("trait_level_bonus", 1), 1, 5));
    if (built) {
        const auto& mem = m["memory"];
        p.rarity->setCurrentIndex(std::clamp(mem.value("rarity", 4), 0, 4));
        p.level->setRange(1, kCap[p.rarity->currentIndex()]);
        p.level->setValue(mem.value("level", 1));
        p.baseStat->setCurrentIndex(std::clamp(mem.value("base", 0), 0, std::max(0, p.baseStat->count() - 1)));
        // Affix rows: restore families and tiers, then values.
        for (auto [rows, key] : {std::pair{p.fixed, "fixed"}, std::pair{p.random, "random"}}) {
            const auto& saved = mem.value(key, nlohmann::json::array());
            for (int r = 0; r < 2; ++r) {
                rows[r].family->clear();
                if (r < static_cast<int>(saved.size())) {
                    rows[r].family->addItem("saved", qs(saved[r].value("family", "")));
                    rows[r].tier->clear();
                    rows[r].tier->addItem("saved", qs(saved[r].value("tier", "")));
                }
            }
        }
        refreshAffixRows(i);
        for (auto [rows, key] : {std::pair{p.fixed, "fixed"}, std::pair{p.random, "random"}}) {
            const auto& saved = mem.value(key, nlohmann::json::array());
            for (int r = 0; r < 2 && r < static_cast<int>(saved.size()); ++r) {
                std::vector<double> values;
                for (const auto& v : saved[r].value("values", nlohmann::json::array())) values.push_back(v.get<double>());
                rows[r].roll->setText(rows[r].roll->text(), values);
            }
        }
    }
    updating_ = wasUpdating;
    p.summary->setText(QString("Gives its trait level %1.").arg(traitLevel(i)));
}

// Fills each affix row with the families its pool allows at this rarity (tier limit) and that are not
// used by the other row, keeping the current choice; shows only as many rows as the rarity and level give.
void HeroEditor::refreshAffixRows(int i) {
    const bool wasUpdating = updating_;
    updating_ = true;
    MemoryPanel& p = panels_[i];
    const int rarity = p.rarity->currentIndex();
    const int level = p.level->value();
    p.baseValue->setText(QString("= %1").arg(
        qs(p.baseStat->currentData().toString().toStdString())
            .replace(QRegularExpression("[\\d.]+"),
                     QString::number(baseStatValue(p.baseStat->currentData().toString().toStdString(), rarity, level))),
        0));
    for (auto [rows, kind, count] : {std::tuple{p.fixed, "Fixed Affix", kFixed[rarity]},
                                     std::tuple{p.random, "Random Affix", randomCount(rarity, level)}}) {
        const auto& rowsData = pool(crafting_, i, kind);
        for (int r = 0; r < 2; ++r) {
            AffixRow& row = rows[r];
            const bool active = r < count;
            row.family->setVisible(active);
            row.tier->setVisible(active);
            row.roll->setVisible(active);
            const QString currentFamily = row.family->currentData().toString();
            const QString currentTier = row.tier->currentData().toString();
            const QString otherFamily = rows[1 - r].family->currentData().toString();
            row.family->clear();
            row.family->addItem("(empty)", QString());
            std::set<QString> seen;
            for (const auto& a : rowsData) {
                const int tier = std::atoi(a.value("tier", "9").c_str());
                if (tier < kBestTier[rarity]) continue;
                const QString key = familyKey(qs(a.value("text", "")));
                if (seen.contains(key) || (key == otherFamily && key != currentFamily)) continue;
                seen.insert(key);
                QString label = key;
                label.replace("#", "X");
                row.family->addItem(label, key);
            }
            row.family->setCurrentIndex(std::max(0, row.family->findData(currentFamily)));
            // Tiers of the chosen family allowed at this rarity, best first.
            row.tier->clear();
            const QString family = row.family->currentData().toString();
            std::vector<std::pair<int, QString>> tiers;
            for (const auto& a : rowsData)
                if (familyKey(qs(a.value("text", ""))) == family) {
                    const int tier = std::atoi(a.value("tier", "9").c_str());
                    if (tier >= kBestTier[rarity]) tiers.emplace_back(tier, qs(a.value("text", "")));
                }
            std::sort(tiers.begin(), tiers.end());
            for (const auto& [tier, text] : tiers) row.tier->addItem("T" + QString::number(tier), QString::number(tier));
            row.tier->setCurrentIndex(std::max(0, row.tier->findData(currentTier)));
            QString text;
            for (const auto& [tier, t] : tiers)
                if (QString::number(tier) == row.tier->currentData().toString()) text = t;
            if (!active || family.isEmpty()) text.clear();
            if (text != row.roll->text()) row.roll->setText(text);
        }
    }
    updating_ = wasUpdating;
}

QStringList HeroEditor::memoryLines(int i) const {
    const MemoryPanel& p = panels_[i];
    QStringList out;
    if (p.kind->currentIndex() == 1) {
        for (const QString& l : p.customLines->toPlainText().split('\n'))
            if (!l.trimmed().isEmpty()) out << l.trimmed();
        return out;
    }
    const QString base = p.baseStat->currentData().toString();
    if (!base.isEmpty())
        out << QString(base).replace(QRegularExpression("[\\d.]+"),
                                     QString::number(baseStatValue(base.toStdString(), p.rarity->currentIndex(), p.level->value())));
    const int rarity = p.rarity->currentIndex();
    for (auto [rows, count] : {std::pair{p.fixed, kFixed[rarity]}, std::pair{p.random, randomCount(rarity, p.level->value())}})
        for (int r = 0; r < count; ++r)
            if (!rows[r].roll->text().isEmpty()) out << rows[r].roll->resolved();
    return out;
}

void HeroEditor::memoryEdited(int i) {
    if (!build_) return;
    MemoryPanel& p = panels_[i];
    nlohmann::json& m = memory(i);
    nlohmann::json lines = nlohmann::json::array();
    for (const QString& l : memoryLines(i)) lines.push_back(l.toStdString());
    m["lines"] = lines;
    if (p.kind->currentIndex() == 1) {
        m.erase("memory");
        m["trait_level_bonus"] = p.customLevel->value();
    } else {
        nlohmann::json mem = {{"rarity", p.rarity->currentIndex()}, {"level", p.level->value()},
                              {"base", p.baseStat->currentIndex()}};
        const int rarity = p.rarity->currentIndex();
        for (auto [rows, key, count] : {std::tuple{p.fixed, "fixed", kFixed[rarity]},
                                        std::tuple{p.random, "random", randomCount(rarity, p.level->value())}}) {
            mem[key] = nlohmann::json::array();
            for (int r = 0; r < count; ++r)
                if (!rows[r].family->currentData().toString().isEmpty())
                    mem[key].push_back({{"family", rows[r].family->currentData().toString().toStdString()},
                                        {"tier", rows[r].tier->currentData().toString().toStdString()},
                                        {"values", rows[r].roll->values()}});
        }
        m["memory"] = mem;
        m["trait_level_bonus"] = traitLevel(i);
    }
    p.summary->setText(QString("Gives its trait level %1.").arg(traitLevel(i)));
    refreshPicks();
    emit changed();
}
