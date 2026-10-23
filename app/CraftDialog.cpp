#include "CraftDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }

// "(154–220)" or "(-30–-6)": one rolled range.
const QRegularExpression& rangePattern() {
    static const QRegularExpression re(QStringLiteral("\\(\\s*(-?[\\d.]+)\\s*[–-]\\s*(-?[\\d.]+)\\s*\\)"));
    return re;
}

int decimalsOf(const QString& number) {
    const int dot = number.indexOf('.');
    return dot < 0 ? 0 : static_cast<int>(number.size()) - dot - 1;
}

QString formatValue(double v, int decimals) {
    QString s = QString::number(v, 'f', decimals);
    return s;
}

QString looseKey(const QString& s) { return s.toLower().remove(QRegularExpression("[^a-z0-9]")); }

// Tier order from best to worst: "0+" (corroded T0), "0", "1", ...
int tierRank(const std::string& tier) { return tier == "0+" ? -1 : std::atoi(tier.c_str()); }

QString tierName(const std::string& tier) { return "T" + qs(tier); }

// Equipment slot -> gear types that can go there, and the Vorax limb for it.
bool typeFitsSlot(const QString& slot, const QString& type, const nlohmann::json& typeData) {
    const QString s = looseKey(slot);
    auto ends = [&](const char* suffix) { return type.endsWith(suffix); };
    const bool shield = ends("Shield");
    const bool armor = ends("Helmet") || ends("Chest Armor") || ends("Gloves") || ends("Boots") || ends("Belt") ||
                       type == "Necklace" || ends("Ring") || shield;
    bool twoHanded = false;
    for (const auto& b : typeData.value("bases", nlohmann::json::array()))
        if (qs(b.value("category", "")).contains("two", Qt::CaseInsensitive)) twoHanded = true;
    if (s.startsWith("helm")) return ends("Helmet");
    if (s.startsWith("chest")) return ends("Chest Armor");
    if (s.startsWith("glove")) return ends("Gloves");
    if (s.startsWith("boot")) return ends("Boots");
    if (s.startsWith("belt")) return ends("Belt");
    if (s.startsWith("amulet") || s.startsWith("neck")) return type == "Necklace";
    if (s.startsWith("ring")) return ends("Ring");
    if (s.startsWith("main")) return !armor;
    if (s.startsWith("off")) return shield || (!armor && !twoHanded);
    return true;
}

QString voraxSuffixForSlot(const QString& slot) {
    const QString s = looseKey(slot);
    if (s.startsWith("helm")) return "Head";
    if (s.startsWith("chest")) return "Chest";
    if (s.startsWith("glove")) return "Hands";
    if (s.startsWith("boot")) return "Legs";
    if (s.startsWith("belt")) return "Waist";
    if (s.startsWith("amulet") || s.startsWith("neck")) return "Neck";
    if (s.startsWith("ring")) return "Digits";
    return {};
}

std::vector<double> toValues(const nlohmann::json& j) {
    std::vector<double> v;
    if (j.is_array())
        for (const auto& x : j) v.push_back(x.get<double>());
    return v;
}

nlohmann::json rollJson(const RollEditor* r) {
    return {{"text", ss(r->text())}, {"values", r->values()}};
}

} // namespace

// --- RollEditor ----------------------------------------------------------------------------------

RollEditor::RollEditor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    preview_ = new QLabel;
    preview_->setWordWrap(true);
    layout->addWidget(preview_);
    grid_ = new QGridLayout;
    layout->addLayout(grid_);
}

QString RollEditor::resolved() const {
    QString out;
    qsizetype last = 0;
    std::size_t i = 0;
    auto it = rangePattern().globalMatch(text_);
    while (it.hasNext()) {
        const auto m = it.next();
        const int decimals = std::max(decimalsOf(m.captured(1)), decimalsOf(m.captured(2)));
        out += text_.mid(last, m.capturedStart() - last);
        out += formatValue(i < values_.size() ? values_[i] : m.captured(2).toDouble(), decimals);
        last = m.capturedEnd();
        ++i;
    }
    return out + text_.mid(last);
}

// One slider per range, defaulting to the top of the range.
void RollEditor::setText(const QString& text, const std::vector<double>& values) {
    text_ = text;
    values_.clear();
    while (QLayoutItem* child = grid_->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    int row = 0;
    auto it = rangePattern().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        double lo = m.captured(1).toDouble(), hi = m.captured(2).toDouble();
        if (lo > hi) std::swap(lo, hi);
        const int decimals = std::max(decimalsOf(m.captured(1)), decimalsOf(m.captured(2)));
        const double step = std::pow(10.0, -decimals);
        const std::size_t index = values_.size();
        const double value = std::clamp(index < values.size() ? values[index] : hi, lo, hi);
        values_.push_back(value);

        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, static_cast<int>(std::lround((hi - lo) / step)));
        slider->setValue(static_cast<int>(std::lround((value - lo) / step)));
        auto* label = new QLabel(QString("%1  (%2 to %3)").arg(formatValue(value, decimals), formatValue(lo, decimals),
                                                                formatValue(hi, decimals)));
        grid_->addWidget(slider, row, 0);
        grid_->addWidget(label, row, 1);
        connect(slider, &QSlider::valueChanged, this, [this, index, lo, hi, step, decimals, label](int pos) {
            values_[index] = lo + pos * step;
            label->setText(QString("%1  (%2 to %3)").arg(formatValue(values_[index], decimals),
                                                          formatValue(lo, decimals), formatValue(hi, decimals)));
            preview_->setText(resolved());
            emit changed();
        });
        ++row;
    }
    preview_->setText(text.isEmpty() ? QString() : resolved());
}

// --- CraftDialog ---------------------------------------------------------------------------------

CraftDialog::CraftDialog(const CraftData& data, const QString& slot, nlohmann::json item, QWidget* parent)
    : QDialog(parent), data_(data), slot_(slot), item_(std::move(item)) {
    setWindowTitle("Craft item: " + slot);
    resize(900, 800);
    auto* outer = new QVBoxLayout(this);

    auto* head = new QFormLayout;
    kind_ = new QComboBox;
    kind_->addItems({"Crafted", "Vorax limb", "Legendary"});
    head->addRow("Item", kind_);
    type_ = new QComboBox;
    head->addRow("Gear type", type_);
    base_ = new QComboBox;
    head->addRow("Base", base_);
    ilvl_ = new QComboBox;
    ilvl_->addItem("82 (up to T2)", 82);
    ilvl_->addItem("86+ (up to T1)", 86);
    ilvl_->addItem("100, Priceless (T0, Ultimate affixes)", 100);
    ilvl_->setCurrentIndex(2);
    head->addRow("Item level", ilvl_);
    corrosion_ = new QComboBox;
    head->addRow("Corrosion", corrosion_);
    outer->addLayout(head);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* content = new QWidget;
    body_ = new QVBoxLayout(content);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    // Crafted / Vorax page.
    affixPage_ = new QWidget;
    auto* affixLayout = new QVBoxLayout(affixPage_);
    auto* baseBox = new QGroupBox("Base affixes");
    baseAffixBox_ = new QVBoxLayout(baseBox);
    auto* addBase = new QPushButton("Add base affix");
    baseAffixBox_->addWidget(addBase);
    affixLayout->addWidget(baseBox);
    for (auto [title, rows] : {std::pair{"Prefixes", &prefixes_}, std::pair{"Suffixes", &suffixes_}}) {
        auto* box = new QGroupBox(title);
        auto* grid = new QVBoxLayout(box);
        for (int i = 0; i < 3; ++i) {
            AffixRow row{new QComboBox, new QComboBox, new QComboBox, new RollEditor, new QLabel};
            row.family->setMinimumWidth(420);
            auto* line = new QHBoxLayout;
            line->addWidget(row.family, 1);
            line->addWidget(row.tier);
            line->addWidget(row.upgrade);
            line->addWidget(row.label);
            grid->addLayout(line);
            grid->addWidget(row.roll);
            rows->push_back(row);
        }
        affixLayout->addWidget(box);
    }
    auto* extraBox = new QGroupBox("Blending (belts) and TOWER Sequence (weapons)");
    auto* extra = new QFormLayout(extraBox);
    blending_ = new QComboBox;
    blendingRoll_ = new RollEditor;
    extra->addRow("Blending Ritual", blending_);
    extra->addRow("", blendingRoll_);
    tower_ = new QComboBox;
    towerRoll_ = new RollEditor;
    extra->addRow("TOWER Sequence", tower_);
    extra->addRow("", towerRoll_);
    affixLayout->addWidget(extraBox);

    organPage_ = new QGroupBox("Organ grafts (up to 2 lines from one legendary)");
    auto* organForm = new QFormLayout(organPage_);
    organLegendary_ = new QComboBox;
    organForm->addRow("Legendary", organLegendary_);
    for (int i = 0; i < 2; ++i) {
        organLine_[i] = new QComboBox;
        organRoll_[i] = new RollEditor;
        organCorroded_[i] = new QCheckBox("Corroded by the limb's corrosion");
        organForm->addRow(QString("Line %1").arg(i + 1), organLine_[i]);
        organForm->addRow("", organCorroded_[i]);
        organForm->addRow("", organRoll_[i]);
    }
    affixLayout->addWidget(organPage_);
    body_->addWidget(affixPage_);

    // Legendary page.
    legendaryPage_ = new QWidget;
    auto* legendaryForm = new QFormLayout(legendaryPage_);
    legendary_ = new QComboBox;
    legendaryForm->addRow("Legendary", legendary_);
    legendaryLines_ = new QVBoxLayout;
    legendaryForm->addRow(legendaryLines_);
    legendaryMutationBox_ = new QWidget;
    auto* mutationForm = new QFormLayout(legendaryMutationBox_);
    legendaryMutation_ = new QComboBox;
    legendaryMutationRoll_ = new RollEditor;
    mutationForm->addRow("Corrosion Base (Mutation)", legendaryMutation_);
    mutationForm->addRow("", legendaryMutationRoll_);
    legendaryForm->addRow(legendaryMutationBox_);
    body_->addWidget(legendaryPage_);
    body_->addStretch(1);

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    outer->addWidget(summary_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) fillTypes();
    });
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) typeChanged();
    });
    connect(ilvl_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_) return;
        // Below item level 100 there are no base affixes (a Mutation's Corrosion Base stays).
        const bool mutation = corrosion_->currentData().toString() == "mutation";
        while (ilvl_->currentData().toInt() < 100 && !mutation && !baseAffixes_.empty()) {
            baseAffixes_.back()->parentWidget()->deleteLater();
            baseAffixes_.pop_back();
        }
        refreshAffixChoices();
    });
    connect(corrosion_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_) return;
        // Mutation replaces the base affixes with a single Corrosion Base affix.
        for (RollEditor* r : baseAffixes_) r->parentWidget()->deleteLater();
        baseAffixes_.clear();
        refreshAffixChoices();
        refreshLegendaryCorrosion();
    });
    connect(addBase, &QPushButton::clicked, this, [this] {
        const nlohmann::json* t = typeData();
        if (!t) return;
        // Priceless (item level 100) items have up to 2 base affixes and lower item levels none (owner).
        // Mutation leaves a single Corrosion Base affix.
        const bool mutation = corrosion_->currentData().toString() == "mutation";
        const std::size_t maxBase = mutation ? 1 : ilvl_->currentData().toInt() >= 100 ? 2 : 0;
        if (baseAffixes_.size() >= maxBase) {
            QMessageBox::information(this, "Base affixes",
                                     maxBase == 0 ? QString("Only Priceless (item level 100) items have base affixes.")
                                                  : QString("This item can have at most %1 base affix%2.").arg(maxBase).arg(maxBase == 1 ? "" : "es"));
            return;
        }
        QStringList choices;
        for (const auto& a : (*t)[mutation ? "corrosion_base" : "base_affixes"]) choices << qs(a.get<std::string>());
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, mutation ? "Corrosion Base affix" : "Base affix", "Affix:",
                                                   choices, 0, false, &ok);
        if (ok) addBaseAffix(pick);
    });
    for (std::vector<AffixRow>* rows : {&prefixes_, &suffixes_}) {
        for (std::size_t i = 0; i < rows->size(); ++i) {
            AffixRow* row = &(*rows)[i];
            connect(row->family, &QComboBox::currentIndexChanged, this, [this, row] {
                if (loading_) return;
                refreshTiers(*row);
                refreshAffixChoices();
            });
            connect(row->tier, &QComboBox::currentIndexChanged, this, [this, row] {
                if (!loading_) updateRoll(*row);
            });
            connect(row->upgrade, &QComboBox::currentIndexChanged, this, [this, row] {
                if (loading_) return;
                updateRoll(*row);
                refreshAffixChoices();
            });
            connect(row->roll, &RollEditor::changed, this, &CraftDialog::updateSummary);
        }
    }
    connect(blending_, &QComboBox::currentIndexChanged, this, [this] {
        blendingRoll_->setText(blending_->currentData().toString());
        updateSummary();
    });
    connect(tower_, &QComboBox::currentIndexChanged, this, [this] {
        towerRoll_->setText(tower_->currentData().toString());
        updateSummary();
    });
    connect(organLegendary_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_) return;
        fillOrganLines();
        refreshAffixChoices();
    });
    for (int i = 0; i < 2; ++i)
        connect(organLine_[i], &QComboBox::currentIndexChanged, this, [this, i] {
            organRoll_[i]->setText(organText(i));
            updateSummary();
        });
    for (int i = 0; i < 2; ++i)
        connect(organCorroded_[i], &QCheckBox::toggled, this, [this, i] {
            organRoll_[i]->setText(organText(i));
            if (!loading_) refreshAffixChoices();
        });
    connect(legendary_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) fillLegendaryLines();
    });
    connect(legendaryMutation_, &QComboBox::currentIndexChanged, this, [this] {
        legendaryMutationRoll_->setText(legendaryMutation_->currentData().toString());
        updateSummary();
    });

    const nlohmann::json craft = item_.value("craft", nlohmann::json::object());
    if (!craft.empty()) loadCraft(craft);
    else fillTypes();
}

const nlohmann::json* CraftDialog::typeData() const {
    const std::string type = ss(type_->currentText());
    const nlohmann::json& pool = kind_->currentIndex() == 1 ? data_.vorax : data_.gearTypes;
    return pool.contains(type) ? &pool[type] : nullptr;
}

// Corrosion outcomes differ between crafted gear and Vorax limbs (owner).
void CraftDialog::fillCorrosion() {
    const QString current = corrosion_->currentData().toString();
    corrosion_->clear();
    corrosion_->addItem("Not corroded", "none");
    if (kind_->currentIndex() == 1) {
        corrosion_->addItem("Festered (corroded only)", "festered");
        corrosion_->addItem("Hyperplastic (1 affix +1-2 tiers)", "hyperplastic");
        corrosion_->addItem("Limb-Stitched (2 affixes +1-2 tiers)", "stitched");
        corrosion_->addItem("Metamorphosized (all prefixes +1-2 tiers)", "metamorphosized");
        corrosion_->addItem("Derived (all suffixes +1-2 tiers)", "derived");
    } else {
        corrosion_->addItem("Void or Chaos (corroded, values only)", "void");
        corrosion_->addItem("Arrogance (1 affix +1 tier)", "arrogance");
        corrosion_->addItem("Desecration (2 affixes +1 tier)", "desecration");
        corrosion_->addItem("Mutation (base affixes become 1 Corrosion Base)", "mutation");
    }
    corrosion_->setCurrentIndex(std::max(0, corrosion_->findData(current)));
}

// Gear types for this slot; legendaries are listed from those types' legendaries.
void CraftDialog::fillTypes() {
    const bool wasLoading = loading_;
    loading_ = true;
    const int kind = kind_->currentIndex();
    fillCorrosion();
    affixPage_->setVisible(kind != 2);
    legendaryPage_->setVisible(kind == 2);
    organPage_->setVisible(kind == 1);
    type_->clear();
    legendary_->clear();
    if (kind == 1) {
        const QString limb = voraxSuffixForSlot(slot_);
        for (auto it = data_.vorax.begin(); it != data_.vorax.end(); ++it)
            if (!limb.isEmpty() && qs(it.key()).endsWith(": " + limb)) type_->addItem(qs(it.key()));
    } else {
        std::set<std::string> legendarySlugs;
        for (auto it = data_.gearTypes.begin(); it != data_.gearTypes.end(); ++it) {
            if (!typeFitsSlot(slot_, qs(it.key()), *it)) continue;
            type_->addItem(qs(it.key()));
            for (const auto& slug : it->value("legendaries", nlohmann::json::array())) legendarySlugs.insert(slug);
        }
        QStringList names;
        for (const auto& l : data_.legendaries)
            if (legendarySlugs.contains(l.value("slug", "")) && !l.value("variants", nlohmann::json::object()).empty())
                names << qs(l.value("name", ""));
        names.sort();
        legendary_->addItems(names);
    }
    loading_ = wasLoading;
    if (!loading_) {
        typeChanged();
        fillLegendaryLines();
    }
}

void CraftDialog::typeChanged() {
    const bool wasLoading = loading_;
    loading_ = true;
    base_->clear();
    blending_->clear();
    tower_->clear();
    for (RollEditor* r : baseAffixes_) r->parentWidget()->deleteLater();
    baseAffixes_.clear();
    const nlohmann::json* t = typeData();
    const QString type = type_->currentText();
    if (t) {
        for (const auto& b : t->value("bases", nlohmann::json::array()))
            base_->addItem(qs(b.value("name", "")) + QString("  (lv %1)").arg(b.value("level", 1)), qs(b.value("name", "")));
        base_->setCurrentIndex(base_->count() - 1);   // the highest base is usually the one wanted
    }
    blending_->addItem("(none)", QString());
    if (looseKey(slot_).startsWith("belt"))
        for (const auto& b : data_.crafting["blending"]) blending_->addItem(qs(b.get<std::string>()), qs(b.get<std::string>()));
    tower_->addItem("(none)", QString());
    for (const auto& s : data_.crafting["tower_sequence"]) {
        const auto& gear = s["gear"];
        if (std::find(gear.begin(), gear.end(), ss(type)) == gear.end()) continue;
        tower_->addItem(qs(s.value("sequence", "")) + ": " + qs(s.value("text", "")), qs(s.value("text", "")));
    }
    blending_->setEnabled(blending_->count() > 1);
    tower_->setEnabled(tower_->count() > 1);
    blendingRoll_->setText({});
    towerRoll_->setText({});
    for (std::vector<AffixRow>* rows : {&prefixes_, &suffixes_})
        for (AffixRow& row : *rows) {
            row.family->clear();
            row.upgrade->setCurrentIndex(0);
        }
    loading_ = wasLoading;
    fillOrganLegendaries();
    refreshAffixChoices();
}

// Repopulates every affix row's choices: families of this side within the item level, not already used,
// and within the Advanced/Ultimate limits counting the other rows. Keeps each row's current choice.
void CraftDialog::refreshAffixChoices() {
    const bool wasLoading = loading_;
    loading_ = true;
    const nlohmann::json* t = typeData();
    const int ilvl = ilvl_->currentData().toInt();
    const std::string corrosion = ss(corrosion_->currentData().toString());

    for (auto [rows, key] : {std::pair{&prefixes_, "prefixes"}, std::pair{&suffixes_, "suffixes"}}) {
        // Families and their library, from the type's rows.
        std::map<long long, std::pair<std::string, std::string>> families;   // id -> (label, library)
        if (t) {
            for (const auto& r : (*t)[key]) {
                if (r.value("level", 0) > ilvl || r.value("tier", "") == "0+") continue;
                const long long id = r.value("family", 0LL);
                if (!families.contains(id) || r.value("tier", "") == "1")
                    families[id] = {r.value("text", ""), r.value("library", "")};
            }
        }
        for (std::size_t i = 0; i < rows->size(); ++i) {
            AffixRow& row = (*rows)[i];
            const long long current = row.family->currentData().toLongLong();
            // Counts of the other rows (both sides).
            int advanced = 0, ultimate = 0;
            std::set<long long> used;
            for (std::vector<AffixRow>* all : {&prefixes_, &suffixes_})
                for (AffixRow& other : *all) {
                    if (&other == &row || other.family->currentIndex() <= 0) continue;
                    const long long id = other.family->currentData().toLongLong();
                    used.insert(id);
                    const std::string lib = ss(other.family->currentData(Qt::UserRole + 1).toString());
                    advanced += lib == "Advanced";
                    ultimate += lib == "Ultimate";
                }
            row.family->clear();
            row.family->addItem("(empty)", 0LL);
            for (const auto& [id, info] : families) {
                const auto& [label, library] = info;
                if (used.contains(id) && id != current) continue;
                if (library == "Advanced" && advanced >= 2 && id != current) continue;
                if (library == "Ultimate" && ultimate >= 2 && id != current) continue;
                QString text = qs(label);
                text.remove(rangePattern());
                row.family->addItem(QString("[%1] %2").arg(qs(library), text.simplified()), id);
                row.family->setItemData(row.family->count() - 1, qs(library), Qt::UserRole + 1);
            }
            const int index = row.family->findData(current);
            row.family->setCurrentIndex(index < 0 ? 0 : index);
            refreshTiers(row);
        }
    }

    // Corrosion upgrades (owner). Crafted gear: Arrogance 1 affix, Desecration 2, by 1 tier. Vorax limbs:
    // Hyperplastic 1 random affix, Limb-Stitched 2, Metamorphosized every prefix, Derived every suffix, by
    // 1-2 tiers. The planner lets the user pick which affixes and by how much.
    struct Rule { int rows; int maxStep; bool prefixes; bool suffixes; };
    const Rule rule = corrosion == "arrogance"         ? Rule{1, 1, true, true}
                      : corrosion == "desecration"     ? Rule{2, 1, true, true}
                      : corrosion == "hyperplastic"    ? Rule{1, 2, true, true}
                      : corrosion == "stitched"        ? Rule{2, 2, true, true}
                      : corrosion == "metamorphosized" ? Rule{3, 2, true, false}
                      : corrosion == "derived"         ? Rule{3, 2, false, true}
                                                       : Rule{0, 0, false, false};
    int upgraded = 0;
    for (std::vector<AffixRow>* rows : {&prefixes_, &suffixes_})
        for (AffixRow& row : *rows) {
            const bool sideAllowed = rows == &prefixes_ ? rule.prefixes : rule.suffixes;
            const int step = row.upgrade->currentIndex();
            if (step > 0 && (!sideAllowed || upgraded >= rule.rows || step > rule.maxStep || row.family->currentIndex() <= 0))
                row.upgrade->setCurrentIndex(0);
            upgraded += row.upgrade->currentIndex() > 0;
        }
    for (std::vector<AffixRow>* rows : {&prefixes_, &suffixes_})
        for (AffixRow& row : *rows) {
            const bool sideAllowed = rows == &prefixes_ ? rule.prefixes : rule.suffixes;
            const int keep = row.upgrade->currentIndex();
            row.upgrade->clear();
            row.upgrade->addItem("No upgrade");
            for (int s = 1; s <= rule.maxStep; ++s) row.upgrade->addItem(QString("Corrosion +%1 tier%2").arg(s).arg(s > 1 ? "s" : ""));
            row.upgrade->setCurrentIndex(std::min(keep, row.upgrade->count() - 1));
            row.upgrade->setEnabled(row.family->currentIndex() > 0 && sideAllowed &&
                                    (keep > 0 || upgraded < rule.rows));
            updateRoll(row);
        }

    // A grafted legendary line picked by Hyperplastic or Limb-Stitched turns into its corroded version
    // (tier steps do nothing to it); it uses one of the outcome's random picks (owner).
    const bool organsCanCorrode = corrosion == "hyperplastic" || corrosion == "stitched";
    for (int i = 0; i < 2; ++i) {
        QCheckBox* box = organCorroded_[i];
        if (box->isChecked() && (!organsCanCorrode || upgraded >= rule.rows)) box->setChecked(false);
        upgraded += box->isChecked();
    }
    for (int i = 0; i < 2; ++i)
        organCorroded_[i]->setEnabled(organsCanCorrode && !organLegendary_->currentData().toString().isEmpty() &&
                                      (organCorroded_[i]->isChecked() || upgraded < rule.rows));
    loading_ = wasLoading;
    updateSummary();
}

void CraftDialog::refreshTiers(AffixRow& row) {
    const bool wasLoading = loading_;
    loading_ = true;
    const QString current = row.tier->currentData().toString();
    row.tier->clear();
    const nlohmann::json* t = typeData();
    const long long id = row.family->currentData().toLongLong();
    if (t && id != 0) {
        const char* key = std::any_of(prefixes_.begin(), prefixes_.end(), [&](const AffixRow& r) { return &r == &row; })
                              ? "prefixes" : "suffixes";
        std::vector<std::string> tiers;
        for (const auto& r : (*t)[key])
            if (r.value("family", 0LL) == id && r.value("tier", "") != "0+" && r.value("level", 0) <= ilvl_->currentData().toInt())
                tiers.push_back(r.value("tier", ""));
        std::sort(tiers.begin(), tiers.end(), [](const auto& a, const auto& b) { return tierRank(a) < tierRank(b); });
        for (const auto& tier : tiers) row.tier->addItem(tierName(tier), qs(tier));
    }
    const int index = row.tier->findData(current);
    row.tier->setCurrentIndex(index < 0 ? 0 : index);
    loading_ = wasLoading;
    updateRoll(row);
}

// Shows the chosen tier's text (1-2 tiers better when upgraded by corrosion) with its sliders.
void CraftDialog::updateRoll(AffixRow& row) {
    const nlohmann::json* t = typeData();
    const long long id = row.family->currentData().toLongLong();
    if (!t || id == 0 || row.tier->count() == 0) {
        row.roll->setText({});
        row.label->clear();
        updateSummary();
        return;
    }
    const char* key = std::any_of(prefixes_.begin(), prefixes_.end(), [&](const AffixRow& r) { return &r == &row; })
                          ? "prefixes" : "suffixes";
    std::vector<const nlohmann::json*> family;
    for (const auto& r : (*t)[key])
        if (r.value("family", 0LL) == id) family.push_back(&r);
    std::sort(family.begin(), family.end(),
              [](const auto* a, const auto* b) { return tierRank(a->value("tier", "")) < tierRank(b->value("tier", "")); });
    const std::string chosen = ss(row.tier->currentData().toString());
    auto it = std::find_if(family.begin(), family.end(), [&](const auto* r) { return r->value("tier", "") == chosen; });
    if (it == family.end()) return;
    for (int s = row.upgrade->currentIndex(); s > 0 && it != family.begin(); --s) --it;   // better tiers
    const nlohmann::json& r = **it;
    const QString text = qs(r.value("text", ""));
    if (text != row.roll->text()) row.roll->setText(text);
    row.label->setText(QString("<b>%1</b> %2").arg(tierName(r.value("tier", "")), qs(r.value("library", ""))));
    updateSummary();
}

void CraftDialog::addBaseAffix(const QString& text, const std::vector<double>& values) {
    auto* holder = new QWidget;
    auto* line = new QHBoxLayout(holder);
    line->setContentsMargins(0, 0, 0, 0);
    auto* roll = new RollEditor;
    roll->setText(text, values);
    auto* remove = new QPushButton("Remove");
    line->addWidget(roll, 1);
    line->addWidget(remove);
    baseAffixBox_->insertWidget(baseAffixBox_->count() - 1, holder);
    baseAffixes_.push_back(roll);
    connect(roll, &RollEditor::changed, this, &CraftDialog::updateSummary);
    connect(remove, &QPushButton::clicked, this, [this, holder, roll] {
        baseAffixes_.erase(std::remove(baseAffixes_.begin(), baseAffixes_.end(), roll), baseAffixes_.end());
        holder->deleteLater();
        updateSummary();
    });
    updateSummary();
}

// Organs: legendaries of the slots this limb accepts ("Graftable: Organs with Gloves, Chest Armor affixes").
void CraftDialog::fillOrganLegendaries() {
    const bool wasLoading = loading_;
    loading_ = true;
    organLegendary_->clear();
    organLegendary_->addItem("(none)", QString());
    const nlohmann::json* t = typeData();
    if (kind_->currentIndex() == 1 && t) {
        std::set<std::string> slugs;
        for (const auto& slotName : t->value("graftable", nlohmann::json::array()))
            for (auto it = data_.gearTypes.begin(); it != data_.gearTypes.end(); ++it)
                if (qs(it.key()).endsWith(qs(slotName.get<std::string>())))
                    for (const auto& slug : it->value("legendaries", nlohmann::json::array())) slugs.insert(slug);
        QStringList names;
        for (const auto& l : data_.legendaries)
            if (slugs.contains(l.value("slug", ""))) names << qs(l.value("name", ""));
        names.sort();
        for (const QString& n : names) organLegendary_->addItem(n, n);
    }
    loading_ = wasLoading;
    fillOrganLines();
}

void CraftDialog::fillOrganLines() {
    for (int i = 0; i < 2; ++i) fillOrganLine(i);
}

// Organs graft a legendary's non-corroded lines (owner).
void CraftDialog::fillOrganLine(int i) {
    organLine_[i]->clear();
    organLine_[i]->addItem("(none)", QString());
    if (const nlohmann::json* l = legendaryData(ss(organLegendary_->currentData().toString())))
        for (const auto& line : l->value("lines", nlohmann::json::array()))
            organLine_[i]->addItem(qs(line.value("text", "")), qs(line.value("text", "")));
    organLine_[i]->setEnabled(organLine_[i]->count() > 1);
}

// The grafted line as it is on the limb: its corroded counterpart when the limb's corrosion picked it.
QString CraftDialog::organText(int i) const {
    const QString text = organLine_[i]->currentData().toString();
    if (text.isEmpty() || !organCorroded_[i]->isChecked()) return text;
    if (const nlohmann::json* l = legendaryData(ss(organLegendary_->currentData().toString())))
        for (const auto& line : l->value("lines", nlohmann::json::array()))
            if (qs(line.value("text", "")) == text && line.contains("corroded")) return qs(line["corroded"].get<std::string>());
    return text;
}

const nlohmann::json* CraftDialog::legendaryData(const std::string& name) const {
    return data_.legendaries.contains(name) ? &data_.legendaries[name] : nullptr;
}

QString CraftDialog::legendaryGearType(const std::string& name) const {
    const nlohmann::json* l = legendaryData(name);
    if (!l) return {};
    for (auto it = data_.gearTypes.begin(); it != data_.gearTypes.end(); ++it) {
        const auto& slugs = it->value("legendaries", nlohmann::json::array());
        if (std::find(slugs.begin(), slugs.end(), l->value("slug", "")) != slugs.end()) return qs(it.key());
    }
    return {};
}

// A legendary's implicit lines stay as they are; each other line has exactly one corroded counterpart,
// which Arrogance (1 line) or Desecration (2 lines) switches to. Mutation adds a Corrosion Base affix of
// the legendary's gear type (owner).
void CraftDialog::fillLegendaryLines(const nlohmann::json& saved) {
    for (RollEditor* r : legendaryImplicit_) r->deleteLater();
    for (RollEditor* r : legendaryRolls_) r->parentWidget()->deleteLater();
    legendaryImplicit_.clear();
    legendaryRolls_.clear();
    legendaryCorroded_.clear();
    legendaryLineData_ = nlohmann::json::array();
    const nlohmann::json* l = legendaryData(ss(legendary_->currentText()));
    const nlohmann::json lines = saved.is_object() ? saved.value("lines", nlohmann::json::array()) : nlohmann::json::array();
    if (l) {
        for (const auto& text : l->value("implicit", nlohmann::json::array())) {
            auto* roll = new RollEditor;
            roll->setText(qs(text.get<std::string>()));
            legendaryLines_->addWidget(roll);
            legendaryImplicit_.push_back(roll);
        }
        std::size_t i = 0;
        for (const auto& line : l->value("lines", nlohmann::json::array())) {
            const bool corroded = i < lines.size() && lines[i].value("corroded", false);
            auto* holder = new QWidget;
            auto* row = new QHBoxLayout(holder);
            row->setContentsMargins(0, 0, 0, 0);
            auto* roll = new RollEditor;
            auto* box = new QCheckBox("Corroded");
            box->setChecked(corroded);
            box->setEnabled(line.contains("corroded"));
            const QString text = qs(line.value(corroded ? "corroded" : "text", ""));
            roll->setText(text, i < lines.size() ? toValues(lines[i]["values"]) : std::vector<double>{});
            row->addWidget(roll, 1);
            row->addWidget(box);
            legendaryLines_->addWidget(holder);
            legendaryRolls_.push_back(roll);
            legendaryCorroded_.push_back(box);
            legendaryLineData_.push_back(line);
            connect(roll, &RollEditor::changed, this, &CraftDialog::updateSummary);
            connect(box, &QCheckBox::toggled, this, [this, roll, line](bool on) {
                roll->setText(qs(line.value(on ? "corroded" : "text", "")));
                refreshLegendaryCorrosion();
            });
            ++i;
        }
    }
    // Mutation choices: the Corrosion Base affixes of the legendary's gear type.
    legendaryMutation_->clear();
    legendaryMutation_->addItem("(none)", QString());
    const std::string type = ss(legendaryGearType(ss(legendary_->currentText())));
    if (data_.gearTypes.contains(type))
        for (const auto& a : data_.gearTypes[type]["corrosion_base"])
            legendaryMutation_->addItem(qs(a.get<std::string>()), qs(a.get<std::string>()));
    if (saved.is_object() && saved.contains("mutation")) {
        legendaryMutation_->setCurrentIndex(std::max(0, legendaryMutation_->findData(qs(saved["mutation"].value("text", "")))));
        legendaryMutationRoll_->setText(legendaryMutationRoll_->text(), toValues(saved["mutation"]["values"]));
    }
    refreshLegendaryCorrosion();
}

void CraftDialog::refreshLegendaryCorrosion() {
    const std::string corrosion = ss(corrosion_->currentData().toString());
    const int allowed = corrosion == "arrogance" ? 1 : corrosion == "desecration" ? 2 : 0;
    int used = 0;
    for (QCheckBox* box : legendaryCorroded_) {
        if (box->isChecked() && used >= allowed) box->setChecked(false);
        used += box->isChecked();
    }
    for (std::size_t i = 0; i < legendaryCorroded_.size(); ++i)
        legendaryCorroded_[i]->setEnabled(legendaryLineData_[i].contains("corroded") &&
                                          (legendaryCorroded_[i]->isChecked() || used < allowed));
    legendaryMutationBox_->setVisible(kind_->currentIndex() == 2 && corrosion == "mutation");
    updateSummary();
}

void CraftDialog::updateSummary() {
    if (loading_) return;
    int pre = 0, suf = 0, advanced = 0, ultimate = 0, upgrades = 0;
    for (std::vector<AffixRow>* rows : {&prefixes_, &suffixes_})
        for (AffixRow& row : *rows) {
            if (row.family->currentIndex() <= 0) continue;
            (rows == &prefixes_ ? pre : suf)++;
            const QString lib = row.family->currentData(Qt::UserRole + 1).toString();
            advanced += lib == "Advanced";
            ultimate += lib == "Ultimate";
            upgrades += row.upgrade->currentIndex() > 0;
        }
    QString text;
    if (kind_->currentIndex() != 2)
        text = QString("Prefixes %1/3 · Suffixes %2/3 · Advanced %3/2 · Ultimate %4/2 · Corrosion upgrades %5")
                   .arg(pre).arg(suf).arg(advanced).arg(ultimate).arg(upgrades);
    QStringList lines;
    for (const QString& l : resolvedLines()) lines << l.toHtmlEscaped();
    text += "<br><i>" + lines.join("<br>") + "</i>";
    summary_->setText(text);
}

QStringList CraftDialog::resolvedLines() const {
    QStringList out;
    auto add = [&out](const QString& s) {
        for (const QString& part : s.split(" | "))
            if (!part.trimmed().isEmpty()) out << part.trimmed();
    };
    if (kind_->currentIndex() == 2) {
        for (const RollEditor* r : legendaryImplicit_) add(r->resolved());
        for (const RollEditor* r : legendaryRolls_) add(r->resolved());
        if (corrosion_->currentData().toString() == "mutation" && !legendaryMutationRoll_->text().isEmpty())
            add(legendaryMutationRoll_->resolved());
        return out;
    }
    if (const nlohmann::json* t = typeData()) {
        const QString base = base_->currentData().toString();
        for (const auto& b : t->value("bases", nlohmann::json::array()))
            if (qs(b.value("name", "")) == base)
                for (const auto& l : b["lines"]) add(qs(l.get<std::string>()));
    }
    for (const RollEditor* r : baseAffixes_) add(r->resolved());
    for (const std::vector<AffixRow>* rows : {&prefixes_, &suffixes_})
        for (const AffixRow& row : *rows)
            if (row.family->currentIndex() > 0) add(row.roll->resolved());
    if (!blendingRoll_->text().isEmpty()) add(blendingRoll_->resolved());
    if (!towerRoll_->text().isEmpty()) add(towerRoll_->resolved());
    if (kind_->currentIndex() == 1)
        for (const RollEditor* r : organRoll_)
            if (!r->text().isEmpty()) add(r->resolved());
    return out;
}

nlohmann::json CraftDialog::craftJson() const {
    nlohmann::json c = {{"kind", kind_->currentIndex() == 0 ? "crafted" : kind_->currentIndex() == 1 ? "vorax" : "legendary"}};
    if (kind_->currentIndex() == 2) {
        nlohmann::json lines = nlohmann::json::array();
        for (std::size_t i = 0; i < legendaryRolls_.size(); ++i)
            lines.push_back({{"corroded", legendaryCorroded_[i]->isChecked()}, {"values", legendaryRolls_[i]->values()}});
        c["legendary"] = {{"name", ss(legendary_->currentText())}, {"lines", lines}};
        c["corrosion"] = ss(corrosion_->currentData().toString());
        if (c["corrosion"] == "mutation" && !legendaryMutationRoll_->text().isEmpty())
            c["legendary"]["mutation"] = rollJson(legendaryMutationRoll_);
        return c;
    }
    c["type"] = ss(type_->currentText());
    c["base"] = ss(base_->currentData().toString());
    c["ilvl"] = ilvl_->currentData().toInt();
    c["corrosion"] = ss(corrosion_->currentData().toString());
    c["base_affixes"] = nlohmann::json::array();
    for (const RollEditor* r : baseAffixes_) c["base_affixes"].push_back(rollJson(r));
    for (auto [rows, key] : {std::pair{&prefixes_, "prefixes"}, std::pair{&suffixes_, "suffixes"}}) {
        c[key] = nlohmann::json::array();
        for (const AffixRow& row : *rows) {
            if (row.family->currentIndex() <= 0) continue;
            c[key].push_back({{"family", row.family->currentData().toLongLong()},
                              {"tier", ss(row.tier->currentData().toString())},
                              {"upgrade", row.upgrade->currentIndex()},
                              {"values", row.roll->values()}});
        }
    }
    if (!blendingRoll_->text().isEmpty()) c["blending"] = rollJson(blendingRoll_);
    if (!towerRoll_->text().isEmpty()) c["tower"] = rollJson(towerRoll_);
    if (kind_->currentIndex() == 1 && !organLegendary_->currentData().toString().isEmpty()) {
        c["organ"] = {{"legendary", ss(organLegendary_->currentData().toString())}, {"lines", nlohmann::json::array()}};
        for (int i = 0; i < 2; ++i) {
            if (organRoll_[i]->text().isEmpty()) continue;
            nlohmann::json line = rollJson(organRoll_[i]);
            line["text"] = ss(organLine_[i]->currentData().toString());   // the non-corroded line
            line["corroded"] = organCorroded_[i]->isChecked();
            c["organ"]["lines"].push_back(line);
        }
    }
    return c;
}

// Restores a previously crafted item, choice by choice.
void CraftDialog::loadCraft(const nlohmann::json& c) {
    loading_ = true;
    const std::string kind = c.value("kind", "crafted");
    kind_->setCurrentIndex(kind == "vorax" ? 1 : kind == "legendary" ? 2 : 0);
    loading_ = false;
    fillTypes();
    loading_ = true;
    if (kind == "legendary") {
        const auto& l = c.value("legendary", nlohmann::json::object());
        legendary_->setCurrentText(qs(l.value("name", "")));
        corrosion_->setCurrentIndex(std::max(0, corrosion_->findData(qs(c.value("corrosion", "none")))));
        loading_ = false;
        fillLegendaryLines(l);
        return;
    }
    type_->setCurrentText(qs(c.value("type", "")));
    loading_ = false;
    typeChanged();
    loading_ = true;
    base_->setCurrentIndex(std::max(0, base_->findData(qs(c.value("base", "")))));
    ilvl_->setCurrentIndex(std::max(0, ilvl_->findData(c.value("ilvl", 100))));
    corrosion_->setCurrentIndex(std::max(0, corrosion_->findData(qs(c.value("corrosion", "none")))));
    loading_ = false;
    for (const auto& a : c.value("base_affixes", nlohmann::json::array()))
        addBaseAffix(qs(a.value("text", "")), toValues(a["values"]));

    // Affix rows: set the families first (choices depend on each other), then tiers and values.
    for (auto [rows, key] : {std::pair{&prefixes_, "prefixes"}, std::pair{&suffixes_, "suffixes"}}) {
        const auto& saved = c.value(key, nlohmann::json::array());
        for (std::size_t i = 0; i < saved.size() && i < rows->size(); ++i) {
            AffixRow& row = (*rows)[i];
            row.family->addItem("saved", saved[i].value("family", 0LL));   // placeholder so the refresh keeps it
            row.family->setCurrentIndex(row.family->count() - 1);
        }
    }
    refreshAffixChoices();
    loading_ = true;
    for (auto [rows, key] : {std::pair{&prefixes_, "prefixes"}, std::pair{&suffixes_, "suffixes"}}) {
        const auto& saved = c.value(key, nlohmann::json::array());
        for (std::size_t i = 0; i < saved.size() && i < rows->size(); ++i) {
            AffixRow& row = (*rows)[i];
            row.tier->setCurrentIndex(std::max(0, row.tier->findData(qs(saved[i].value("tier", "")))));
            row.upgrade->setCurrentIndex(saved[i].value("upgrade", 0));
        }
    }
    loading_ = false;
    refreshAffixChoices();
    for (auto [rows, key] : {std::pair{&prefixes_, "prefixes"}, std::pair{&suffixes_, "suffixes"}}) {
        const auto& saved = c.value(key, nlohmann::json::array());
        for (std::size_t i = 0; i < saved.size() && i < rows->size(); ++i)
            (*rows)[i].roll->setText((*rows)[i].roll->text(), toValues(saved[i]["values"]));
    }
    if (c.contains("blending")) {
        blending_->setCurrentIndex(std::max(0, blending_->findData(qs(c["blending"].value("text", "")))));
        blendingRoll_->setText(blendingRoll_->text(), toValues(c["blending"]["values"]));
    }
    if (c.contains("tower")) {
        tower_->setCurrentIndex(std::max(0, tower_->findData(qs(c["tower"].value("text", "")))));
        towerRoll_->setText(towerRoll_->text(), toValues(c["tower"]["values"]));
    }
    if (c.contains("organ")) {
        organLegendary_->setCurrentIndex(std::max(0, organLegendary_->findData(qs(c["organ"].value("legendary", "")))));
        const auto& lines = c["organ"]["lines"];
        for (std::size_t i = 0; i < 2 && i < lines.size(); ++i) {
            loading_ = true;
            organCorroded_[i]->setChecked(lines[i].value("corroded", false));
            loading_ = false;
            organLine_[i]->setCurrentIndex(std::max(0, organLine_[i]->findData(qs(lines[i].value("text", "")))));
            organRoll_[i]->setText(organRoll_[i]->text(), toValues(lines[i]["values"]));
        }
    }
    updateSummary();
}

nlohmann::json CraftDialog::item() const {
    nlohmann::json out = item_;
    out["slot"] = ss(slot_);
    out["craft"] = craftJson();
    nlohmann::json lines = nlohmann::json::array();
    for (const QString& l : resolvedLines()) lines.push_back(ss(l));
    out["lines"] = lines;
    if (kind_->currentIndex() == 2) out["name"] = ss(legendary_->currentText());
    else if (kind_->currentIndex() == 1) out["name"] = ss(type_->currentText());
    else out["name"] = ss(base_->currentData().toString());
    return out;
}
