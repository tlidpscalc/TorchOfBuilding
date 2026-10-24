#include "Editors.h"

#include <QCheckBox>
#include <QSpinBox>
#include <QRadioButton>
#include <QGridLayout>
#include <QButtonGroup>
#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

nlohmann::json& arrayAt(nlohmann::json& parent, const char* key) {
    if (!parent.contains(key) || !parent[key].is_array()) parent[key] = nlohmann::json::array();
    return parent[key];
}

void containsCompleter(QComboBox* box) {
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    box->completer()->setFilterMode(Qt::MatchContains);
    box->completer()->setCompletionMode(QCompleter::PopupCompletion);
}

// Picks a name from a list (type to filter); empty if cancelled.
QString pickName(QWidget* parent, const QString& title, const QStringList& names) {
    bool ok = false;
    QString name = QInputDialog::getItem(parent, title, "Name:", names, 0, true, &ok);
    return ok ? name.trimmed() : QString();
}

QPushButton* button(const QString& text, QHBoxLayout* row) {
    auto* b = new QPushButton(text);
    row->addWidget(b);
    return b;
}

} // namespace

// --- Skills ------------------------------------------------------------------------------------

namespace {

constexpr int kActiveSlots = 5;    // slot 1 is the main skill (owner)
constexpr int kPassiveSlots = 4;   // (owner)
constexpr int kSupportSlots = 5;

// "This skill can only be installed in the first/third/fifth Support Skill Slot": 1, 3 or 5; 0 = any.
int requiredSupportSlot(const nlohmann::json& card) {
    static const QRegularExpression re("only be installed in the (first|second|third|fourth|fifth) Support Skill Slot");
    for (const auto& block : card.value("description", nlohmann::json::array()))
        for (const auto& line : block) {
            const auto m = re.match(qs(line.get<std::string>()));
            if (!m.hasMatch()) continue;
            const QStringList order = {"first", "second", "third", "fourth", "fifth"};
            return static_cast<int>(order.indexOf(m.captured(1))) + 1;
        }
    return 0;
}

bool isCanvas(const std::string& name) {
    return name.ends_with("(Noble)") || name.ends_with("(Magnificent)");
}

bool isMedium(const nlohmann::json& card) {
    const auto tags = card.value("tags", nlohmann::json::array());
    return std::find(tags.begin(), tags.end(), "Activation Medium") != tags.end();
}

const nlohmann::json* cardFor(const nlohmann::json& skillData, const std::string& name) {
    return skillData.contains(name) ? &skillData[name] : nullptr;
}

// A canvas support's lines from its card: the fixed lines, then the rolled ones.
QStringList canvasTemplate(const nlohmann::json& card) {
    QStringList out;
    const auto& d = card.value("description", nlohmann::json::array());
    for (std::size_t b = 1; b < d.size(); ++b)
        for (const auto& l : d[b])
            for (const QString& part : qs(l.get<std::string>()).replace("\\n", "\n").split('\n'))
                if (!part.trimmed().isEmpty()) out << part.trimmed();
    return out;
}

// Values of the template's ranges read back from lines already in the build, so existing builds keep
// their rolls when shown.
std::vector<double> inferValues(const QStringList& templ, const nlohmann::json& lines) {
    static const QRegularExpression range(QStringLiteral("\\(\\s*(-?[\\d.]+)\\s*[–-]\\s*(-?[\\d.]+)\\s*\\)"));
    std::vector<double> values;
    for (const QString& t : templ) {
        QString pattern;
        qsizetype last = 0;
        std::vector<double> tops;
        auto it = range.globalMatch(t);
        while (it.hasNext()) {
            const auto m = it.next();
            pattern += QRegularExpression::escape(t.mid(last, m.capturedStart() - last)) + "(-?[\\d.]+)";
            tops.push_back(std::max(m.captured(1).toDouble(), m.captured(2).toDouble()));
            last = m.capturedEnd();
        }
        if (tops.empty()) continue;
        pattern = "^" + pattern + QRegularExpression::escape(t.mid(last)) + "$";
        const QRegularExpression re(pattern);
        std::vector<double> found = tops;
        for (const auto& l : lines)
            if (const auto m = re.match(qs(l.get<std::string>())); m.hasMatch())
                for (std::size_t i = 0; i < tops.size(); ++i) found[i] = m.captured(static_cast<int>(i) + 1).toDouble();
        values.insert(values.end(), found.begin(), found.end());
    }
    return values;
}

} // namespace

// Puts a build's skills into the game's slots: 5 active skills with the main one first, and 5 support
// slots per skill with slot-bound supports (Activation Medium 1, Magnificent 3, Noble 5) in their slot.
// Nothing is dropped: anything that does not fit stays after the slots.
void SkillsEditor::normalize(nlohmann::json& build, const nlohmann::json& skillData) {
    nlohmann::json& skills = build["skills"];
    if (!skills.is_object()) skills = nlohmann::json::object();
    nlohmann::json& active = arrayAt(skills, "active");
    const std::string main = skills.value("main", "");
    for (std::size_t i = 1; i < active.size(); ++i)
        if (active[i].is_object() && active[i].value("name", "") == main) {
            std::swap(active[0], active[i]);
            break;
        }
    while (active.size() < kActiveSlots) active.push_back(nullptr);

    auto placeSupports = [&skillData](nlohmann::json& skill) {
        if (!skill.is_object()) return;
        nlohmann::json& old = arrayAt(skill, "supports");
        // Already slotted: 5 entries with every slot-bound support in its own slot.
        bool slotted = old.size() == kSupportSlots;
        for (std::size_t i = 0; slotted && i < old.size(); ++i)
            if (old[i].is_object())
                if (const auto* c = cardFor(skillData, old[i].value("name", ""));
                    c && requiredSupportSlot(*c) && requiredSupportSlot(*c) != static_cast<int>(i) + 1)
                    slotted = false;
        if (slotted) return;
        nlohmann::json placed = nlohmann::json::array();
        for (int i = 0; i < kSupportSlots; ++i) placed.push_back(nullptr);
        std::vector<nlohmann::json> rest, overflow;
        for (const auto& s : old) {
            if (!s.is_object() || s.value("name", "").empty()) continue;
            const auto* c = cardFor(skillData, s.value("name", ""));
            const int want = c ? requiredSupportSlot(*c) : 0;
            if (want && placed[want - 1].is_null()) placed[want - 1] = s;
            else rest.push_back(s);
        }
        for (auto& s : rest) {
            auto free = std::find_if(placed.begin(), placed.end(), [&](const nlohmann::json& x) { return x.is_null(); });
            // Slot-bound slots stay free for their kind when possible: fill slots 2 and 4 first.
            for (int pref : {1, 3, 0, 2, 4})
                if (placed[pref].is_null()) {
                    free = placed.begin() + pref;
                    break;
                }
            if (free == placed.end()) overflow.push_back(s);
            else *free = s;
        }
        for (auto& s : overflow) placed.push_back(s);
        old = placed;
    };
    nlohmann::json& passive = arrayAt(skills, "passive");
    while (passive.size() < kPassiveSlots) passive.push_back(nullptr);
    for (auto& s : active) placeSupports(s);
    for (auto& s : passive) placeSupports(s);
}

bool SkillsEditor::applyTraitSupports(nlohmann::json& build, const nlohmann::json& skillData,
                                      const nlohmann::json& traitData) {
    // Trait lines of the chosen nodes in the build's trait tree.
    const auto traits = build.value("traits", nlohmann::json::object());
    std::set<std::string> chosen;
    for (const auto& n : traits.value("nodes", nlohmann::json::array())) chosen.insert(n.value("name", ""));
    bool clockwork = false;
    for (const auto& tree : traitData) {
        if (tree.value("slug", "") != traits.value("tree", "")) continue;
        for (const auto& node : tree["nodes"]) {
            if (!chosen.contains(node.value("name", ""))) continue;
            for (const auto& lv : node["levels"])
                for (const char* key : {"affix", "server_affix"})
                    for (const auto& a : lv.value(key, nlohmann::json::array()))
                        if (a.get<std::string>().find("supported by Clockwork Core, replacing the support skill in the "
                                                      "second support skill slot") != std::string::npos)
                            clockwork = true;
        }
    }
    nlohmann::json& active = arrayAt(build["skills"], "active");
    if (active.empty() || !active[0].is_object()) return false;
    if (!clockwork) {
        // The trait was dropped: remove the support it had placed.
        for (auto& s : arrayAt(active[0], "supports"))
            if (s.is_object() && s.value("trait", false)) {
                s = nullptr;
                return true;
            }
        return false;
    }
    // Only Main Active Spell Skills (ASSUMED: every main spell counts as tanglable).
    const auto* card = cardFor(skillData, active[0].value("name", ""));
    const auto tags = card ? card->value("tags", nlohmann::json::array()) : nlohmann::json::array();
    if (std::find(tags.begin(), tags.end(), "Spell") == tags.end()) return false;
    nlohmann::json& supports = arrayAt(active[0], "supports");
    while (supports.size() < kSupportSlots) supports.push_back(nullptr);
    if (supports[1].is_object() && supports[1].value("name", "") == "Clockwork Core") return false;
    // Whatever sat in slot 2 is replaced, as in the game; another copy elsewhere is removed.
    for (auto& s : supports)
        if (s.is_object() && s.value("name", "") == "Clockwork Core") s = nullptr;
    supports[1] = {{"name", "Clockwork Core"}, {"trait", true}};
    return true;
}

bool SkillsEditor::traitLocked(int supportSlot) const {
    if (!build_ || selKind_ != "active" || selIndex_ != 0 || supportSlot != 1) return false;
    const auto& active = (*build_)["skills"]["active"];
    if (active.empty() || !active[0].is_object()) return false;
    const auto& supports = active[0].value("supports", nlohmann::json::array());
    return supports.size() > 1 && supports[1].is_object() && supports[1].value("trait", false);
}

SkillsEditor::SkillsEditor(const nlohmann::json& skillData, const nlohmann::json& traitData, QWidget* parent)
    : QWidget(parent), skillData_(skillData), traitData_(traitData) {
    for (auto it = skillData.begin(); it != skillData.end(); ++it) {
        const QString name = qs(it->value("name", it.key()));
        // Tartarus and Origin Tartarus skills belong to followers (the Tartarus mechanic), not heroes (owner).
        if (name.startsWith("Tartarus:") || name.startsWith("Origin Tartarus:")) continue;
        const auto tags = it->value("tags", nlohmann::json::array());
        const bool support = std::find(tags.begin(), tags.end(), "Support") != tags.end();
        (support ? supportNames_ : skillNames_) << name;
    }
    skillNames_.sort();
    supportNames_.sort();

    auto* layout = new QVBoxLayout(this);
    auto* activeBox = new QGroupBox("Active skill slots (slot 1 is the main skill)");
    auto* activeGrid = new QGridLayout(activeBox);
    skillGroup_ = new QButtonGroup(this);
    for (int i = 0; i < kActiveSlots; ++i) {
        auto* pick = new QRadioButton(i == 0 ? "1 (Main)" : QString::number(i + 1));
        auto* combo = new QComboBox;
        combo->addItem("(empty)");
        combo->addItems(skillNames_);
        containsCompleter(combo);
        activeGrid->addWidget(pick, i, 0);
        activeGrid->addWidget(combo, i, 1);
        skillGroup_->addButton(pick, i);
        activeSlots_[i] = combo;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, i, combo] {
            if (updating_ || !build_) return;
            const QString name = combo->currentText();
            if (name != "(empty)" && !skillNames_.contains(name)) return;
            nlohmann::json& skills = (*build_)["skills"];
            nlohmann::json& active = arrayAt(skills, "active");
            while (active.size() < kActiveSlots) active.push_back(nullptr);
            nlohmann::json supports = nlohmann::json::array();
            for (int s = 0; s < kSupportSlots; ++s) supports.push_back(nullptr);
            active[i] = name == "(empty)" ? nlohmann::json(nullptr)
                                          : nlohmann::json{{"name", name.toStdString()}, {"supports", supports}};
            if (i == 0) {
                skills["main"] = name == "(empty)" ? "" : name.toStdString();
                applyTraitSupports(*build_, skillData_, traitData_);
            }
            selectSkill("active", i);
            emit changed();
        });
    }
    connect(skillGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        if (id < kActiveSlots) selectSkill("active", id);
        else selectSkill("passive", id - kActiveSlots);
    });
    layout->addWidget(activeBox);

    auto* passiveBox = new QGroupBox("Passive skill slots");
    auto* passiveGrid = new QGridLayout(passiveBox);
    for (int i = 0; i < kPassiveSlots; ++i) {
        auto* pick = new QRadioButton(QString::number(i + 1));
        auto* combo = new QComboBox;
        combo->addItem("(empty)");
        combo->addItems(skillNames_);
        containsCompleter(combo);
        auto* enabled = new QCheckBox("Enabled");
        passiveGrid->addWidget(pick, i, 0);
        passiveGrid->addWidget(combo, i, 1);
        passiveGrid->addWidget(enabled, i, 2);
        skillGroup_->addButton(pick, kActiveSlots + i);
        passiveSlots_[i] = combo;
        passiveEnabled_[i] = enabled;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, i, combo] {
            if (updating_ || !build_) return;
            const QString name = combo->currentText();
            if (name != "(empty)" && !skillNames_.contains(name)) return;
            nlohmann::json& passive = arrayAt((*build_)["skills"], "passive");
            while (passive.size() < kPassiveSlots) passive.push_back(nullptr);
            nlohmann::json supports = nlohmann::json::array();
            for (int s = 0; s < kSupportSlots; ++s) supports.push_back(nullptr);
            passive[i] = name == "(empty)" ? nlohmann::json(nullptr)
                                           : nlohmann::json{{"name", name.toStdString()}, {"enabled", true}, {"supports", supports}};
            selectSkill("passive", i);
            emit changed();
        });
        connect(enabled, &QCheckBox::toggled, this, [this, i](bool on) {
            if (updating_ || !build_) return;
            nlohmann::json& passive = arrayAt((*build_)["skills"], "passive");
            if (i >= static_cast<int>(passive.size()) || !passive[i].is_object()) return;
            passive[i]["enabled"] = on;
            emit changed();
        });
    }
    layout->addWidget(passiveBox);

    supportBox_ = new QGroupBox("Support slots");
    auto* supportGrid = new QGridLayout(supportBox_);
    supportGroup_ = new QButtonGroup(this);
    const char* labels[] = {"1 (Activation Medium)", "2", "3 (Magnificent)", "4", "5 (Noble)"};
    for (int k = 0; k < kSupportSlots; ++k) {
        auto* pick = new QRadioButton(labels[k]);
        auto* combo = new QComboBox;
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(30);
        containsCompleter(combo);
        supportGrid->addWidget(pick, k, 0);
        supportGrid->addWidget(combo, k, 1);
        supportGroup_->addButton(pick, k);
        supportSlots_[k] = combo;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, k, combo] {
            nlohmann::json* skill = selectedSkill();
            if (updating_ || !skill) return;
            const QString name = combo->currentText();
            if (name != "(empty)" && combo->findText(name) < 0) return;
            nlohmann::json& supports = arrayAt(*skill, "supports");
            while (supports.size() < kSupportSlots) supports.push_back(nullptr);
            supports[k] = name == "(empty)" ? nlohmann::json(nullptr) : nlohmann::json{{"name", name.toStdString()}};
            selSupport_ = k;
            if (supports[k].is_object()) initSupport(supports[k]);
            showSupports();
            emit changed();
        });
    }
    connect(supportGroup_, &QButtonGroup::idClicked, this, [this](int k) {
        selSupport_ = k;
        showSupportDetails();
    });
    layout->addWidget(supportBox_);

    // Activation Mediums have tiers instead of levels; most also offer a Cooldown or a Duration bonus.
    mediumBox_ = new QGroupBox("Activation Medium");
    auto* mediumForm = new QFormLayout(mediumBox_);
    mediumTier_ = new QComboBox;
    mediumBonus_ = new QComboBox;
    mediumBonus_->addItem("Cooldown Recovery Speed", "cooldown");
    mediumBonus_->addItem("Duration", "duration");
    mediumRoll_ = new RollEditor;
    mediumForm->addRow("Tier", mediumTier_);
    mediumForm->addRow("Bonus", mediumBonus_);
    mediumForm->addRow(mediumRoll_);
    layout->addWidget(mediumBox_);
    connect(mediumTier_, &QComboBox::currentIndexChanged, this, [this] {
        if (!updating_) mediumEdited();
    });
    connect(mediumBonus_, &QComboBox::currentIndexChanged, this, [this] {
        if (!updating_) mediumEdited();
    });
    connect(mediumRoll_, &RollEditor::changed, this, [this] {
        nlohmann::json* sup = selectedSupport();
        if (updating_ || !sup) return;
        (*sup)["values"] = mediumRoll_->values();
        emit changed();
    });

    // Divinity Canvas supports (Noble, Magnificent): stars and their rolled lines.
    canvasBox_ = new QGroupBox("Canvas support");
    auto* canvasForm = new QFormLayout(canvasBox_);
    canvasStars_ = new QSpinBox;
    canvasStars_->setRange(1, 5);
    canvasRoll_ = new RollEditor;
    canvasForm->addRow("Stars", canvasStars_);
    canvasForm->addRow(canvasRoll_);
    layout->addWidget(canvasBox_);
    connect(canvasStars_, &QSpinBox::valueChanged, this, [this](int stars) {
        nlohmann::json* sup = selectedSupport();
        if (updating_ || !sup) return;
        (*sup)["stars"] = stars;
        emit changed();
    });
    connect(canvasRoll_, &RollEditor::changed, this, [this] {
        nlohmann::json* sup = selectedSupport();
        if (updating_ || !sup) return;
        storeCanvasLines(*sup);
        emit changed();
    });
    layout->addStretch(1);
}

void SkillsEditor::setBuild(nlohmann::json* build) {
    build_ = build;
    if (build_) {
        normalize(*build_, skillData_);
        applyTraitSupports(*build_, skillData_, traitData_);
    }
    selectSkill("active", 0);
}

void SkillsEditor::selectSkill(const std::string& kind, int index) {
    selKind_ = kind;
    selIndex_ = index;
    selSupport_ = 0;
    refresh();
}

nlohmann::json* SkillsEditor::selectedSkill() {
    if (!build_) return nullptr;
    nlohmann::json& list = arrayAt((*build_)["skills"], selKind_.c_str());
    if (selIndex_ < 0 || selIndex_ >= static_cast<int>(list.size()) || !list[selIndex_].is_object()) return nullptr;
    return &list[selIndex_];
}

nlohmann::json* SkillsEditor::selectedSupport() {
    nlohmann::json* skill = selectedSkill();
    if (!skill) return nullptr;
    nlohmann::json& list = arrayAt(*skill, "supports");
    if (selSupport_ < 0 || selSupport_ >= static_cast<int>(list.size()) || !list[selSupport_].is_object()) return nullptr;
    return &list[selSupport_];
}

void SkillsEditor::refresh() {
    const bool wasUpdating = updating_;
    updating_ = true;
    if (build_) {
        nlohmann::json& skills = (*build_)["skills"];
        nlohmann::json& active = arrayAt(skills, "active");
        for (int i = 0; i < kActiveSlots; ++i) {
            const bool filled = i < static_cast<int>(active.size()) && active[i].is_object();
            activeSlots_[i]->setCurrentText(filled ? qs(active[i].value("name", "")) : "(empty)");
            activeSlots_[i]->setCurrentIndex(std::max(0, activeSlots_[i]->findText(activeSlots_[i]->currentText())));
        }
        nlohmann::json& passive = arrayAt(skills, "passive");
        for (int i = 0; i < kPassiveSlots; ++i) {
            const bool filled = i < static_cast<int>(passive.size()) && passive[i].is_object();
            passiveSlots_[i]->setCurrentIndex(
                std::max(0, passiveSlots_[i]->findText(filled ? qs(passive[i].value("name", "")) : "(empty)")));
            passiveEnabled_[i]->setEnabled(filled);
            passiveEnabled_[i]->setChecked(filled && passive[i].value("enabled", true));
        }
        const int id = selKind_ == "active" ? selIndex_ : kActiveSlots + selIndex_;
        if (QAbstractButton* b = skillGroup_->button(id)) b->setChecked(true);
    }
    updating_ = wasUpdating;
    showSupports();
}

// The supports that can go in slot k (0-based) of the selected skill: Activation Mediums only in slot 1,
// that skill's Magnificent supports only in slot 3 and its Noble supports only in slot 5, everything else
// in any slot.
QStringList SkillsEditor::supportsForSlot(int k, const QString& skill) const {
    QStringList out;
    for (const QString& name : supportNames_) {
        const nlohmann::json* card = cardFor(skillData_, name.toStdString());
        const int want = card ? requiredSupportSlot(*card) : 0;
        if (want && want != k + 1) continue;
        if (isCanvas(name.toStdString()) && !name.startsWith(skill + ":")) continue;
        out << name;
    }
    return out;
}

void SkillsEditor::showSupports() {
    const bool wasUpdating = updating_;
    updating_ = true;
    nlohmann::json* skill = selectedSkill();
    supportBox_->setTitle(skill ? "Support slots: " + qs(skill->value("name", "")) : "Support slots");
    supportBox_->setEnabled(skill != nullptr);
    const QString skillName = skill ? qs(skill->value("name", "")) : QString();
    for (int k = 0; k < kSupportSlots; ++k) {
        QComboBox* combo = supportSlots_[k];
        combo->clear();
        combo->addItem("(empty)");
        combo->addItems(supportsForSlot(k, skillName));
        QString current = "(empty)";
        if (skill) {
            const nlohmann::json& list = arrayAt(*skill, "supports");
            if (k < static_cast<int>(list.size()) && list[k].is_object()) current = qs(list[k].value("name", ""));
        }
        if (combo->findText(current) < 0) combo->addItem(current);   // kept from the build file even if unknown
        combo->setCurrentIndex(combo->findText(current));
        // A support placed by a hero trait cannot be changed here.
        const bool locked = traitLocked(k);
        combo->setEnabled(!locked);
        if (locked) combo->setItemText(combo->currentIndex(), current + "  (hero trait)");
    }
    if (QAbstractButton* b = supportGroup_->button(selSupport_)) b->setChecked(true);
    updating_ = wasUpdating;
    showSupportDetails();
}

// Default settings for a newly slotted support: canvas supports get 5 stars and their lines at the top
// of each roll; Activation Mediums use the engine's defaults until changed.
void SkillsEditor::initSupport(nlohmann::json& sup) {
    const nlohmann::json* card = cardFor(skillData_, sup.value("name", ""));
    if (!card || !isCanvas(sup.value("name", ""))) return;
    sup["stars"] = 5;
    RollEditor roll;
    roll.setText(canvasTemplate(*card).join(" | "));
    sup["values"] = roll.values();
    nlohmann::json lines = nlohmann::json::array();
    for (const QString& l : roll.resolved().split(" | ")) lines.push_back(l.toStdString());
    sup["lines"] = lines;
}

void SkillsEditor::storeCanvasLines(nlohmann::json& sup) {
    sup["values"] = canvasRoll_->values();
    nlohmann::json lines = nlohmann::json::array();
    for (const QString& l : canvasRoll_->resolved().split(" | ")) lines.push_back(l.toStdString());
    sup["lines"] = lines;
}

void SkillsEditor::showSupportDetails() {
    nlohmann::json* sup = selectedSupport();
    const std::string name = sup ? sup->value("name", "") : "";
    const nlohmann::json* card = cardFor(skillData_, name);
    const bool medium = card && isMedium(*card) && !card->value("tiers", nlohmann::json::array()).empty();
    const bool canvas = card && isCanvas(name);
    mediumBox_->setVisible(medium);
    canvasBox_->setVisible(canvas);
    if (medium) showMedium();
    if (canvas) {
        const bool wasUpdating = updating_;
        updating_ = true;
        canvasBox_->setTitle(qs(name));
        canvasStars_->setValue(std::clamp(sup->value("stars", 5), 1, 5));
        const QStringList templ = canvasTemplate(*card);
        std::vector<double> values;
        if (sup->contains("values"))
            for (const auto& v : (*sup)["values"]) values.push_back(v.get<double>());
        else
            values = inferValues(templ, sup->value("lines", nlohmann::json::array()));
        canvasRoll_->setText(templ.join(" | "), values);
        updating_ = wasUpdating;
    }
}

// The medium's lines for a tier and bonus, joined with " | " (same selection as the engine's loader).
QString SkillsEditor::mediumText(const nlohmann::json& card, const QString& tier, const QString& bonus) const {
    QStringList out;
    for (const auto& t : card["tiers"]) {
        if (qs(t.value("tier", "")) != tier) continue;
        for (const auto& l : t["lines"]) {
            const QString line = qs(l.get<std::string>());
            if (card.value("bonus_choice", false)) {
                const bool cooldown = line.contains("Cooldown Recovery Speed");
                const bool duration = line.contains("Duration");
                if ((cooldown && bonus != "cooldown") || (duration && !cooldown && bonus != "duration")) continue;
            }
            out << line;
        }
    }
    return out.join(" | ");
}

void SkillsEditor::showMedium() {
    nlohmann::json* sup = selectedSupport();
    const nlohmann::json* card = sup ? cardFor(skillData_, sup->value("name", "")) : nullptr;
    if (!card) return;
    const bool wasUpdating = updating_;
    updating_ = true;
    mediumBox_->setTitle(qs(sup->value("name", "")));
    mediumTier_->clear();
    for (const auto& t : (*card)["tiers"]) mediumTier_->addItem("T" + qs(t.value("tier", "")), qs(t.value("tier", "")));
    const QString tier = qs(sup->value("tier", (*card)["tiers"].back().value("tier", "")));
    mediumTier_->setCurrentIndex(std::max(0, mediumTier_->findData(tier)));
    mediumBonus_->setEnabled(card->value("bonus_choice", false));
    mediumBonus_->setCurrentIndex(std::max(0, mediumBonus_->findData(qs(sup->value("bonus", "cooldown")))));
    std::vector<double> values;
    for (const auto& v : sup->value("values", nlohmann::json::array())) values.push_back(v.get<double>());
    mediumRoll_->setText(mediumText(*card, mediumTier_->currentData().toString(), mediumBonus_->currentData().toString()),
                         values);
    updating_ = wasUpdating;
}

void SkillsEditor::mediumEdited() {
    nlohmann::json* sup = selectedSupport();
    if (!sup) return;
    (*sup)["tier"] = mediumTier_->currentData().toString().toStdString();
    (*sup)["bonus"] = mediumBonus_->currentData().toString().toStdString();
    sup->erase("values");   // new lines: rolls start at the top of their ranges
    showMedium();
    (*sup)["values"] = mediumRoll_->values();
    emit changed();
}

// --- Gear --------------------------------------------------------------------------------------

namespace {

const char* kEquipSlots[] = {"Helmet", "Chest", "Gloves", "Boots", "Belt", "Amulet",
                             "Ring 1", "Ring 2", "Main Hand", "Off Hand", "Custom"};

// "Ring 1" -> "ring", "Main Hand" -> "mainhand", "Necklace" -> "amulet".
std::string slotCategory(const QString& slot) {
    const QString k = slot.toLower().remove(QRegularExpression("[^a-z]"));
    if (k.startsWith("helm")) return "helmet";
    if (k.startsWith("chest")) return "chest";
    if (k.startsWith("glove")) return "gloves";
    if (k.startsWith("boot")) return "boots";
    if (k.startsWith("belt")) return "belt";
    if (k.startsWith("amulet") || k.startsWith("neck")) return "amulet";
    if (k.startsWith("ring")) return "ring";
    if (k.startsWith("main") || k == "weapon") return "mainhand";
    if (k.startsWith("off")) return "offhand";
    return "custom";
}

bool isWeaponCategory(const std::string& c) { return c == "mainhand" || c == "offhand"; }

} // namespace

GearEditor::GearEditor(const CraftData& data, QWidget* parent) : QWidget(parent), data_(data) {
    auto* layout = new QVBoxLayout(this);

    auto* equippedBox = new QGroupBox("Equipped (pick from the inventory)");
    auto* equipForm = new QFormLayout(equippedBox);
    for (const char* slot : kEquipSlots) {
        auto* combo = new QComboBox;
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(24);
        equipForm->addRow(slot, combo);
        equip_.emplace_back(slot, combo);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, slot = QString(slot), combo] {
            if (updating_ || !build_) return;
            nlohmann::json& equipped = (*build_)["equipped"];
            const long long id = combo->currentData().toLongLong();
            if (id > 0) equipped[slot.toStdString()] = id;
            else equipped.erase(slot.toStdString());
            syncEquipped();
            refresh();
            emit changed();
        });
    }
    layout->addWidget(equippedBox);
    warning_ = new QLabel;
    warning_->setStyleSheet("color: #e05555");
    warning_->setWordWrap(true);
    layout->addWidget(warning_);

    auto* inventoryBox = new QGroupBox("Inventory");
    auto* inventoryLayout = new QVBoxLayout(inventoryBox);
    items_ = new QListWidget;
    inventoryLayout->addWidget(items_, 1);
    auto* buttons = new QHBoxLayout;
    QPushButton* add = button("New item...", buttons);
    QPushButton* edit = button("Edit...", buttons);
    QPushButton* duplicate = button("Duplicate", buttons);
    QPushButton* remove = button("Delete", buttons);
    inventoryLayout->addLayout(buttons);
    auto* form = new QFormLayout;
    name_ = new QLineEdit;
    form->addRow("Name", name_);
    inventoryLayout->addLayout(form);
    auto* hint = new QLabel("Lines of the selected item. Crafted items are changed with Edit...; custom items "
                            "(one modifier per line, as shown in game) are typed here.");
    hint->setWordWrap(true);
    inventoryLayout->addWidget(hint);
    lines_ = new QPlainTextEdit;
    inventoryLayout->addWidget(lines_, 1);
    layout->addWidget(inventoryBox, 1);

    connect(items_, &QListWidget::currentRowChanged, this, [this] { showItem(); });
    connect(items_, &QListWidget::itemDoubleClicked, this, [this] { editItem(); });
    connect(add, &QPushButton::clicked, this, &GearEditor::newItem);
    connect(edit, &QPushButton::clicked, this, &GearEditor::editItem);
    connect(duplicate, &QPushButton::clicked, this, &GearEditor::duplicateItem);
    connect(remove, &QPushButton::clicked, this, &GearEditor::deleteItem);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& name) {
        nlohmann::json* item = selectedItem();
        if (updating_ || !item) return;
        (*item)["name"] = name.toStdString();
        syncEquipped();
        refresh();
        emit changed();
    });
    connect(lines_, &QPlainTextEdit::textChanged, this, [this] {
        nlohmann::json* item = selectedItem();
        if (updating_ || !item || item->contains("craft")) return;
        nlohmann::json lines = nlohmann::json::array();
        for (const QString& l : lines_->toPlainText().split('\n'))
            if (!l.trimmed().isEmpty()) lines.push_back(l.trimmed().toStdString());
        (*item)["lines"] = lines;
        syncEquipped();
        emit changed();
    });
}

void GearEditor::setBuild(nlohmann::json* build) {
    build_ = build;
    migrate();
    refresh();
    if (items_->count() > 0) items_->setCurrentRow(0);
}

nlohmann::json& GearEditor::inventory() { return arrayAt(*build_, "inventory"); }

nlohmann::json* GearEditor::itemById(long long id) {
    for (auto& item : inventory())
        if (item.value("id", 0LL) == id) return &item;
    return nullptr;
}

long long GearEditor::nextId() {
    long long id = 0;
    for (const auto& item : inventory()) id = std::max(id, item.value("id", 0LL));
    return id + 1;
}

nlohmann::json* GearEditor::selectedItem() {
    QListWidgetItem* row = items_->currentItem();
    return build_ && row ? itemById(row->data(Qt::UserRole).toLongLong()) : nullptr;
}

// Builds made before the inventory existed: every item goes into the inventory, worn in its slot.
void GearEditor::migrate() {
    if (!build_ || build_->contains("inventory")) return;
    nlohmann::json& inv = inventory();
    nlohmann::json equipped = nlohmann::json::object();
    for (const auto& old : arrayAt(*build_, "items")) {
        nlohmann::json item = old;
        item["id"] = static_cast<long long>(inv.size()) + 1;
        std::string slot = item.value("slot", "");
        // Use a dropdown slot when the item's category has a free one; otherwise keep its own slot name.
        for (const char* s : kEquipSlots)
            if (slotCategory(s) == slotCategory(QString::fromStdString(slot)) && !equipped.contains(s)) {
                slot = s;
                break;
            }
        if (equipped.contains(slot)) slot += " (" + std::to_string(item["id"].get<long long>()) + ")";
        equipped[slot] = item["id"];
        inv.push_back(item);
    }
    (*build_)["equipped"] = equipped;
}

// Writes the worn items to "items", the list the engine reads. A two-handed main hand drops the off hand.
void GearEditor::syncEquipped() {
    if (!build_) return;
    nlohmann::json& equipped = (*build_)["equipped"];
    if (!equipped.is_object()) equipped = nlohmann::json::object();
    if (equipped.contains("Main Hand"))
        if (const nlohmann::json* main = itemById(equipped["Main Hand"].get<long long>()); main && isTwoHanded(*main))
            equipped.erase("Off Hand");
    nlohmann::json items = nlohmann::json::array();
    for (auto it = equipped.begin(); it != equipped.end();) {
        const nlohmann::json* item = itemById(it.value().get<long long>());
        if (!item) {
            it = equipped.erase(it);
            continue;
        }
        nlohmann::json worn = *item;
        worn["slot"] = it.key();
        items.push_back(worn);
        ++it;
    }
    (*build_)["items"] = items;
}

// Weapons: one-handed fit either hand, shields only the off hand, two-handed only the main hand.
bool GearEditor::fits(const QString& slot, const nlohmann::json& item) const {
    const std::string want = slotCategory(slot);
    const std::string have = slotCategory(QString::fromStdString(item.value("slot", "")));
    if (!isWeaponCategory(want)) return want == have;
    if (!isWeaponCategory(have)) return false;
    const auto craft = item.value("craft", nlohmann::json::object());
    const std::string type = craft.value("type", "");
    if (QString::fromStdString(type).endsWith("Shield")) return want == "offhand";
    if (isTwoHanded(item)) return want == "mainhand";
    return true;
}

bool GearEditor::isTwoHanded(const nlohmann::json& item) const {
    const auto craft = item.value("craft", nlohmann::json::object());
    std::string type = craft.value("type", "");
    std::string base = craft.value("base", "");
    if (craft.value("kind", "") == "legendary") {
        // A legendary's gear type comes from the type pages that list it.
        const std::string name = craft.value("legendary", nlohmann::json::object()).value("name", "");
        if (!data_.legendaries.contains(name)) return false;
        const std::string slug = data_.legendaries[name].value("slug", "");
        for (auto it = data_.gearTypes.begin(); it != data_.gearTypes.end(); ++it) {
            const auto& slugs = it->value("legendaries", nlohmann::json::array());
            if (std::find(slugs.begin(), slugs.end(), slug) != slugs.end()) type = it.key();
        }
        base.clear();
    }
    if (!data_.gearTypes.contains(type)) return false;
    for (const auto& b : data_.gearTypes[type]["bases"])
        if (base.empty() || b.value("name", "") == base)
            return QString::fromStdString(b.value("category", "")).contains("two", Qt::CaseInsensitive);
    return false;
}

void GearEditor::refresh() {
    if (!build_) return;
    const bool wasUpdating = updating_;
    updating_ = true;
    const long long current = items_->currentItem() ? items_->currentItem()->data(Qt::UserRole).toLongLong() : 0;
    nlohmann::json& equipped = (*build_)["equipped"];
    if (!equipped.is_object()) equipped = nlohmann::json::object();

    items_->clear();
    for (const auto& item : inventory()) {
        const long long id = item.value("id", 0LL);
        QString wornIn;
        for (auto it = equipped.begin(); it != equipped.end(); ++it)
            if (it.value().get<long long>() == id) wornIn = qs(it.key());
        QString label = "[" + qs(item.value("slot", "")) + "] " + qs(item.value("name", ""));
        if (!item.contains("craft")) label += "  (custom)";
        if (!wornIn.isEmpty()) label += "  — worn: " + wornIn;
        auto* row = new QListWidgetItem(label);
        row->setData(Qt::UserRole, id);
        items_->addItem(row);
        if (id == current) items_->setCurrentItem(row);
    }

    bool twoHanded = false;
    if (equipped.contains("Main Hand"))
        if (const nlohmann::json* main = itemById(equipped["Main Hand"].get<long long>())) twoHanded = isTwoHanded(*main);
    for (auto& [slot, combo] : equip_) {
        combo->clear();
        combo->addItem("(empty)", 0LL);
        for (const auto& item : inventory())
            if (fits(slot, item)) combo->addItem(qs(item.value("name", "")), item.value("id", 0LL));
        const std::string key = slot.toStdString();
        const int index = equipped.contains(key) ? combo->findData(equipped[key].get<long long>()) : 0;
        combo->setCurrentIndex(std::max(0, index));
        combo->setEnabled(!(slot == "Off Hand" && twoHanded));
        combo->setToolTip(slot == "Off Hand" && twoHanded ? "The main-hand weapon is two-handed." : QString());
    }
    // Items worn in slots without a dropdown (from older build files) still count; say so.
    QStringList extra;
    for (auto it = equipped.begin(); it != equipped.end(); ++it)
        if (std::none_of(equip_.begin(), equip_.end(), [&](const auto& e) { return e.first.toStdString() == it.key(); }))
            extra << qs(it.key());
    warning_->setText(extra.isEmpty() ? QString() : "Also worn (from the build file): " + extra.join(", "));
    updating_ = wasUpdating;
    showItem();
}

void GearEditor::showItem() {
    const bool wasUpdating = updating_;
    updating_ = true;
    nlohmann::json* item = selectedItem();
    name_->setText(item ? qs(item->value("name", "")) : QString());
    QStringList lines;
    if (item)
        for (const auto& l : item->value("lines", nlohmann::json::array())) lines << qs(l.get<std::string>());
    lines_->setPlainText(lines.join('\n'));
    name_->setEnabled(item != nullptr);
    lines_->setReadOnly(!item || item->contains("craft"));
    updating_ = wasUpdating;
}

void GearEditor::newItem() {
    if (!build_) return;
    bool ok = false;
    const QStringList kinds = {"Helmet", "Chest", "Gloves", "Boots", "Belt", "Amulet", "Ring", "Main Hand", "Off Hand",
                               "Custom lines"};
    const QString kind = QInputDialog::getItem(this, "New item", "Slot:", kinds, 0, false, &ok);
    if (!ok) return;
    nlohmann::json item;
    if (kind == "Custom lines") {
        item = {{"slot", "Custom"}, {"name", "Custom modifiers"}, {"lines", nlohmann::json::array()}};
    } else {
        CraftDialog dialog(data_, kind, {{"slot", kind.toStdString()}}, this);
        if (dialog.exec() != QDialog::Accepted) return;
        item = dialog.item();
    }
    item["id"] = nextId();
    inventory().push_back(item);
    // Wear it straight away when a fitting slot is empty.
    nlohmann::json& equipped = (*build_)["equipped"];
    for (const auto& [slot, combo] : equip_)
        if (!equipped.contains(slot.toStdString()) && combo->isEnabled() && fits(slot, item)) {
            equipped[slot.toStdString()] = item["id"];
            break;
        }
    syncEquipped();
    refresh();
    for (int i = 0; i < items_->count(); ++i)
        if (items_->item(i)->data(Qt::UserRole).toLongLong() == item["id"].get<long long>()) items_->setCurrentRow(i);
    emit changed();
}

// Crafted items reopen in the crafter; custom items are edited in the lines box.
void GearEditor::editItem() {
    nlohmann::json* item = selectedItem();
    if (!item) return;
    if (!item->contains("craft")) {
        lines_->setFocus();
        return;
    }
    CraftDialog dialog(data_, qs(item->value("slot", "")), *item, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const nlohmann::json id = (*item)["id"];
    *item = dialog.item();
    (*item)["id"] = id;
    syncEquipped();
    refresh();
    emit changed();
}

void GearEditor::duplicateItem() {
    nlohmann::json* item = selectedItem();
    if (!item) return;
    nlohmann::json copy = *item;
    copy["id"] = nextId();
    copy["name"] = item->value("name", "") + " (copy)";
    inventory().push_back(copy);
    refresh();
    items_->setCurrentRow(items_->count() - 1);
    emit changed();
}

void GearEditor::deleteItem() {
    nlohmann::json* item = selectedItem();
    if (!item) return;
    if (QMessageBox::question(this, "Delete item", "Delete \"" + qs(item->value("name", "")) + "\" from the inventory?") !=
        QMessageBox::Yes)
        return;
    const long long id = item->value("id", 0LL);
    nlohmann::json& inv = inventory();
    inv.erase(std::remove_if(inv.begin(), inv.end(), [id](const nlohmann::json& i) { return i.value("id", 0LL) == id; }),
              inv.end());
    syncEquipped();   // drops slots that wore it
    refresh();
    emit changed();
}
