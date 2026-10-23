#include "MainWindow.h"

#include "tob/Calc.h"
#include "tob/ModParser.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

using namespace tob;

namespace {

QDoubleSpinBox* spin(double value, double max, int decimals = 1) {
    auto* s = new QDoubleSpinBox;
    s->setRange(-max, max);
    s->setDecimals(decimals);
    s->setValue(value);
    return s;
}

QComboBox* damageTypeCombo(bool withNone) {
    auto* c = new QComboBox;
    if (withNone) c->addItem("(none)", -1);
    for (DamageType t : kAllDamageTypeList) {
        c->addItem(QString::fromUtf8(toString(t).data(), static_cast<qsizetype>(toString(t).size())),
                   static_cast<int>(t));
    }
    return c;
}

QString html(const std::string& s) {
    return QString::fromStdString(s).toHtmlEscaped();
}

// Config lines: "berserk" sets a flag, "rage = 100" sets a variable.
Config parseConfig(const QString& text) {
    Config cfg;
    for (const QString& raw : text.split('\n')) {
        std::string line = normalizeText(raw.toStdString());
        if (line.empty()) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            cfg.flags[line] = true;
            continue;
        }
        std::string name = normalizeText(line.substr(0, eq));
        try {
            cfg.vars[name] = std::stod(line.substr(eq + 1));
        } catch (...) {
        }
    }
    return cfg;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("TorchOfBuilding");

    auto* form = new QWidget;
    auto* layout = new QVBoxLayout(form);

    auto* skillBox = new QGroupBox("Skill");
    auto* skillForm = new QFormLayout(skillBox);
    skillTags_ = new QLineEdit("Attack, Projectile, Cold");
    skillForm->addRow("Tags", skillTags_);
    skillForm->addRow("Weapon damage %", weaponPct_ = spin(407, 100000));
    skillForm->addRow("Added dmg effectiveness %", ade_ = spin(407, 100000));
    skillForm->addRow("Hits per use", hitsPerUse_ = spin(1, 1000, 2));
    skillForm->addRow("Convert Physical to", convertTo_ = damageTypeCombo(true));
    skillForm->addRow("Conversion %", convertPct_ = spin(100, 100));
    skillForm->addRow("Spell base damage type", spellType_ = damageTypeCombo(false));
    skillForm->addRow("Spell base min", spellMin_ = spin(0, 1e9));
    skillForm->addRow("Spell base max", spellMax_ = spin(0, 1e9));
    skillForm->addRow("Spell base cast speed (/s)", spellCastSpeed_ = spin(1, 100, 3));
    skillForm->addRow("Spell base crit rating", spellCrit_ = spin(0, 100000, 0));
    convertTo_->setCurrentIndex(convertTo_->findData(static_cast<int>(DamageType::Cold)));
    layout->addWidget(skillBox);

    auto* weaponBox = new QGroupBox("Weapon (attacks)");
    auto* weaponForm = new QFormLayout(weaponBox);
    weaponForm->addRow("Physical min", weaponMin_ = spin(100, 1e9));
    weaponForm->addRow("Physical max", weaponMax_ = spin(150, 1e9));
    weaponForm->addRow("Attacks / s", weaponAps_ = spin(1.5, 100, 3));
    weaponForm->addRow("Crit rating", weaponCrit_ = spin(500, 100000, 0));
    layout->addWidget(weaponBox);

    auto* enemyBox = new QGroupBox("Enemy (target dummy defaults)");
    auto* enemyForm = new QFormLayout(enemyBox);
    enemyForm->addRow("Elemental/Erosion res %", enemyRes_ = spin(30, 1000));
    enemyForm->addRow("Armor mitigation (phys) %", armorPhys_ = spin(50, 1000));
    enemyForm->addRow("Armor mitigation (non-phys) %", armorNonPhys_ = spin(30, 1000));
    layout->addWidget(enemyBox);

    auto* modsBox = new QGroupBox("Modifiers (one per line)");
    auto* modsLayout = new QVBoxLayout(modsBox);
    mods_ = new QPlainTextEdit("+50% Cold Damage\n+30% additional Attack Damage\n+20% Elemental Penetration");
    modsLayout->addWidget(mods_);
    layout->addWidget(modsBox, 1);

    auto* configBox = new QGroupBox("Config (flag, or name = value)");
    auto* configLayout = new QVBoxLayout(configBox);
    config_ = new QPlainTextEdit;
    config_->setPlaceholderText("berserk\nrage = 100");
    configLayout->addWidget(config_);
    layout->addWidget(configBox);

    auto* scroll = new QScrollArea;
    scroll->setWidget(form);
    scroll->setWidgetResizable(true);

    output_ = new QTextBrowser;

    auto* splitter = new QSplitter;
    splitter->addWidget(scroll);
    splitter->addWidget(output_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    for (QDoubleSpinBox* s : findChildren<QDoubleSpinBox*>())
        connect(s, &QDoubleSpinBox::valueChanged, this, &MainWindow::recalculate);
    for (QComboBox* c : findChildren<QComboBox*>())
        connect(c, &QComboBox::currentIndexChanged, this, &MainWindow::recalculate);
    connect(skillTags_, &QLineEdit::textChanged, this, &MainWindow::recalculate);
    connect(mods_, &QPlainTextEdit::textChanged, this, &MainWindow::recalculate);
    connect(config_, &QPlainTextEdit::textChanged, this, &MainWindow::recalculate);

    recalculate();
}

void MainWindow::recalculate() {
    std::string unknownTags;
    CalcInput in;
    in.skill.name = "Skill";
    in.skill.tags = parseTagList(skillTags_->text().toStdString(), &unknownTags);
    in.skill.baseCastSpeed = spellCastSpeed_->value();
    in.skill.baseCritRating = spellCrit_->value();

    HitComponent hit;
    hit.name = "Hit";
    hit.weaponDamagePct = weaponPct_->value();
    hit.addedDamageEffectivenessPct = ade_->value();
    hit.hitsPerUse = hitsPerUse_->value();
    auto spellType = static_cast<DamageType>(spellType_->currentData().toInt());
    hit.baseDamage[index(spellType)] = {spellMin_->value(), spellMax_->value()};
    in.skill.components.push_back(hit);

    if (int to = convertTo_->currentData().toInt(); to >= 0 && convertPct_->value() > 0) {
        in.skill.conversions.push_back({DamageType::Physical, static_cast<DamageType>(to), convertPct_->value()});
    }

    in.weapon.damage[index(DamageType::Physical)] = {weaponMin_->value(), weaponMax_->value()};
    in.weapon.attacksPerSecond = weaponAps_->value();
    in.weapon.critRating = weaponCrit_->value();

    for (DamageType t : kAllDamageTypeList) {
        if (t != DamageType::Physical) in.enemy.resistancePct[index(t)] = enemyRes_->value();
    }
    in.enemy.armorMitigationPhysicalPct = armorPhys_->value();
    in.enemy.armorMitigationNonPhysicalPct = armorNonPhys_->value();

    ModDB db;
    QString unparsed;
    for (const ParseResult& r : parseModText(mods_->toPlainText().toStdString(), "Custom")) {
        if (r.ok) {
            db.add(r.mods);
        } else {
            unparsed += QString("<li><span style='color:#d33'>%1</span> &mdash; %2</li>")
                            .arg(html(r.line), html(r.error));
        }
    }
    Config cfg = parseConfig(config_->toPlainText());
    in.mods = &db;
    in.config = &cfg;

    CalcResult res = calculateHit(in);

    QString out;
    out += QString("<h1>%1 DPS</h1>").arg(res.hitDps, 0, 'f', 1);
    out += QString("<p>Crit chance %1% &middot; Crit multiplier %2% &middot; %3 uses/s</p>")
               .arg(res.critChance * 100.0, 0, 'f', 2)
               .arg(res.critMultiplier * 100.0, 0, 'f', 1)
               .arg(res.usesPerSecond, 0, 'f', 3);
    if (!unknownTags.empty()) out += QString("<p style='color:#d33'>Unknown tags: %1</p>").arg(html(unknownTags));
    if (!unparsed.isEmpty()) out += "<h3>Unsupported modifiers (ignored)</h3><ul>" + unparsed + "</ul>";
    for (const auto& [section, lines] : res.breakdown.sections) {
        out += "<h3>" + html(section) + "</h3><ul>";
        for (const std::string& line : lines) out += "<li>" + html(line) + "</li>";
        out += "</ul>";
    }
    output_->setHtml(out);
}
