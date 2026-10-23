#include "Editors.h"

#include <QCheckBox>
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

SkillsEditor::SkillsEditor(const nlohmann::json& skillData, QWidget* parent) : QWidget(parent), skillData_(skillData) {
    for (auto it = skillData.begin(); it != skillData.end(); ++it) {
        const auto tags = it->value("tags", nlohmann::json::array());
        const bool support = std::find(tags.begin(), tags.end(), "Support") != tags.end();
        (support ? supportNames_ : skillNames_) << qs(it->value("name", it.key()));
    }
    skillNames_.sort();
    supportNames_.sort();

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    main_ = new QComboBox;
    main_->addItems(skillNames_);
    containsCompleter(main_);
    form->addRow("Main skill", main_);
    layout->addLayout(form);

    layout->addWidget(new QLabel("Skills"));
    skills_ = new QListWidget;
    layout->addWidget(skills_, 1);
    auto* skillButtons = new QHBoxLayout;
    QPushButton* addActive = button("Add active", skillButtons);
    QPushButton* addPassive = button("Add passive", skillButtons);
    QPushButton* removeSkill = button("Remove", skillButtons);
    layout->addLayout(skillButtons);
    enabled_ = new QCheckBox("Passive skill enabled");
    layout->addWidget(enabled_);

    layout->addWidget(new QLabel("Supports of the selected skill"));
    supports_ = new QListWidget;
    layout->addWidget(supports_, 1);
    auto* supportButtons = new QHBoxLayout;
    QPushButton* addSupport = button("Add support", supportButtons);
    QPushButton* removeSupport = button("Remove support", supportButtons);
    layout->addLayout(supportButtons);

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
    mediumBox_->setVisible(false);
    connect(supports_, &QListWidget::currentRowChanged, this, [this] { showMedium(); });
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

    connect(main_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (updating_ || !build_ || !skillNames_.contains(name)) return;
        nlohmann::json& skills = (*build_)["skills"];
        skills["main"] = name.toStdString();
        // The main skill must be one of the active skills.
        nlohmann::json& active = arrayAt(skills, "active");
        const bool present = std::any_of(active.begin(), active.end(),
                                         [&](const nlohmann::json& s) { return s.value("name", "") == name.toStdString(); });
        if (!present) active.insert(active.begin(), {{"name", name.toStdString()}, {"supports", nlohmann::json::array()}});
        refresh();
        emit changed();
    });
    auto addSkill = [this](const char* kind) {
        if (!build_) return;
        const QString name = pickName(this, "Add skill", skillNames_);
        if (name.isEmpty()) return;
        nlohmann::json entry = {{"name", name.toStdString()}, {"supports", nlohmann::json::array()}};
        if (std::string(kind) == "passive") entry["enabled"] = true;
        arrayAt((*build_)["skills"], kind).push_back(entry);
        refresh();
        skills_->setCurrentRow(skills_->count() - 1);
        emit changed();
    };
    connect(addActive, &QPushButton::clicked, this, [addSkill] { addSkill("active"); });
    connect(addPassive, &QPushButton::clicked, this, [addSkill] { addSkill("passive"); });
    connect(removeSkill, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = skills_->currentItem();
        if (!build_ || !item) return;
        const std::string kind = item->data(Qt::UserRole).toString().toStdString();
        nlohmann::json& list = (*build_)["skills"][kind];
        list.erase(list.begin() + item->data(Qt::UserRole + 1).toInt());
        refresh();
        emit changed();
    });
    connect(skills_, &QListWidget::currentRowChanged, this, [this] { showSupports(); });
    connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
        nlohmann::json* skill = selectedSkill();
        if (updating_ || !skill || !skill->contains("enabled")) return;
        (*skill)["enabled"] = on;
        const int row = skills_->currentRow();
        refresh();
        skills_->setCurrentRow(row);
        emit changed();
    });
    connect(addSupport, &QPushButton::clicked, this, [this] {
        nlohmann::json* skill = selectedSkill();
        if (!skill) return;
        const QString name = pickName(this, "Add support", supportNames_);
        if (name.isEmpty()) return;
        arrayAt(*skill, "supports").push_back({{"name", name.toStdString()}});
        showSupports();
        emit changed();
    });
    connect(removeSupport, &QPushButton::clicked, this, [this] {
        nlohmann::json* skill = selectedSkill();
        const int row = supports_->currentRow();
        if (!skill || row < 0) return;
        nlohmann::json& list = arrayAt(*skill, "supports");
        list.erase(list.begin() + row);
        showSupports();
        emit changed();
    });
}

void SkillsEditor::setBuild(nlohmann::json* build) {
    build_ = build;
    refresh();
    if (skills_->count() > 0) skills_->setCurrentRow(0);
}

nlohmann::json* SkillsEditor::selectedSkill() {
    QListWidgetItem* item = skills_->currentItem();
    if (!build_ || !item) return nullptr;
    const std::string kind = item->data(Qt::UserRole).toString().toStdString();
    return &(*build_)["skills"][kind][item->data(Qt::UserRole + 1).toInt()];
}

void SkillsEditor::refresh() {
    const bool wasUpdating = updating_;
    updating_ = true;
    skills_->clear();
    if (build_) {
        nlohmann::json& skills = (*build_)["skills"];
        main_->setCurrentText(qs(skills.value("main", "")));
        for (const char* kind : {"active", "passive"}) {
            const nlohmann::json& list = arrayAt(skills, kind);
            for (std::size_t i = 0; i < list.size(); ++i) {
                QString label = qs(list[i].value("name", ""));
                if (std::string(kind) == "passive")
                    label = "Passive: " + label + (list[i].value("enabled", true) ? "" : "  (off)");
                if (label == main_->currentText()) label += "  (main)";
                auto* item = new QListWidgetItem(label);
                item->setData(Qt::UserRole, kind);
                item->setData(Qt::UserRole + 1, static_cast<int>(i));
                skills_->addItem(item);
            }
        }
    }
    updating_ = wasUpdating;
    showSupports();
}

nlohmann::json* SkillsEditor::selectedSupport() {
    nlohmann::json* skill = selectedSkill();
    const int row = supports_->currentRow();
    if (!skill || row < 0) return nullptr;
    nlohmann::json& list = arrayAt(*skill, "supports");
    return row < static_cast<int>(list.size()) ? &list[row] : nullptr;
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
    const std::string name = sup ? sup->value("name", "") : "";
    const nlohmann::json* card = skillData_.contains(name) ? &skillData_[name] : nullptr;
    const bool tiered = card && !card->value("tiers", nlohmann::json::array()).empty();
    mediumBox_->setVisible(tiered);
    if (!tiered) return;
    const bool wasUpdating = updating_;
    updating_ = true;
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

void SkillsEditor::showSupports() {
    const bool wasUpdating = updating_;
    updating_ = true;
    supports_->clear();
    nlohmann::json* skill = selectedSkill();
    enabled_->setEnabled(skill && skill->contains("enabled"));
    enabled_->setChecked(skill && skill->value("enabled", true));
    if (skill) {
        for (const auto& s : arrayAt(*skill, "supports")) {
            QString label = qs(s.value("name", ""));
            if (s.contains("stars")) label += QString("  (%1 stars, %2 lines)").arg(s.value("stars", 1)).arg(s.value("lines", nlohmann::json::array()).size());
            supports_->addItem(label);
        }
    }
    updating_ = wasUpdating;
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
