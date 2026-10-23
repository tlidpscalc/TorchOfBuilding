#include "MainWindow.h"

#include "tob/ModParser.h"
#include "tob/SkillDps.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

using namespace tob;

namespace {

QString html(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

// 7.941e12 -> "7.941T".
QString shortNumber(double v) {
    static const char* kSuffix[] = {"", "K", "M", "B", "T", "Qa", "Qi"};
    int i = 0;
    double a = std::abs(v);
    while (a >= 1000.0 && i < 6) {
        a /= 1000.0;
        v /= 1000.0;
        ++i;
    }
    return QString::number(v, 'f', i == 0 ? 1 : 3) + kSuffix[i];
}

// Walks up from the executable and the working directory to the repository's data folder.
QString findDataDir() {
    for (QString start : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QDir dir(start);
        for (int i = 0; i < 8; ++i) {
            if (QFileInfo::exists(dir.filePath("data/json/skills.json"))) return dir.filePath("data");
            if (!dir.cdUp()) break;
        }
    }
    return {};
}

// Situational state the engine works out itself; showing it as a toggle would only confuse.
const std::set<std::string> kDerivedFlags = {
    "frostbitten", "frozen", "focus blessing active", "night feather", "fervor", "blur", "spell aggression",
    "affected by ailments", "cast by spell burst",
};
const std::set<std::string> kDerivedVars = {
    "stacks", "activated tangles", "ailment types", "projectile speed", "attack or spell block chance",
    "cooldown recovery speed", "skill effect duration", "sealed mana compensation",
};
// Values the engine reads that no modifier line names.
const char* kEngineVars[] = {
    "fight duration", "affliction", "focus blessing", "agility blessing", "tenacity blessing",
    "fervor rating", "blur rating", "beam reaps per second",
};

QDoubleSpinBox* autoSpin(double value) {
    auto* s = new QDoubleSpinBox;
    s->setRange(-1.0, 1e9);
    s->setDecimals(2);
    s->setSpecialValueText("auto");   // -1: not set, the engine's default applies
    s->setValue(value);
    return s;
}

QDoubleSpinBox* percentSpin(double value) {
    auto* s = new QDoubleSpinBox;
    s->setRange(-1000.0, 1000.0);
    s->setDecimals(1);
    s->setSuffix(" %");
    s->setValue(value);
    return s;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("TorchOfBuilding");

    auto* toolbar = addToolBar("Build");
    toolbar->setMovable(false);
    QAction* open = toolbar->addAction("Open build...");
    connect(open, &QAction::triggered, this, &MainWindow::openBuild);
    QAction* reload = toolbar->addAction("Reload");
    connect(reload, &QAction::triggered, this, [this] {
        if (!buildPath_.isEmpty()) loadBuildPath(buildPath_);
    });
    toolbar->addSeparator();
    buildLabel_ = new QLabel("No build loaded");
    toolbar->addWidget(buildLabel_);

    // Left: configuration.
    auto* panel = new QWidget;
    auto* panelLayout = new QVBoxLayout(panel);

    auto* enemyBox = new QGroupBox("Enemy");
    auto* enemyForm = new QFormLayout(enemyBox);
    enemyForm->addRow("Elemental / erosion resistance", enemyRes_ = percentSpin(30));
    enemyForm->addRow("Armor mitigation, physical", armorPhys_ = percentSpin(50));
    enemyForm->addRow("Armor mitigation, non-physical", armorNonPhys_ = percentSpin(30));
    panelLayout->addWidget(enemyBox);

    auto* flagsBox = new QGroupBox("Conditions");
    flagsForm_ = new QFormLayout(flagsBox);
    panelLayout->addWidget(flagsBox);

    auto* varsBox = new QGroupBox("Values (auto = engine default)");
    varsForm_ = new QFormLayout(varsBox);
    panelLayout->addWidget(varsBox);

    auto* customBox = new QGroupBox("Custom modifiers (one per line)");
    auto* customLayout = new QVBoxLayout(customBox);
    custom_ = new QPlainTextEdit;
    custom_->setPlaceholderText("+20% additional damage\n+3 Max Spell Burst");
    custom_->setMinimumHeight(120);
    customLayout->addWidget(custom_);
    panelLayout->addWidget(customBox);
    panelLayout->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(380);

    // Right: results and line report.
    output_ = new QTextBrowser;

    auto* linesPage = new QWidget;
    auto* linesLayout = new QVBoxLayout(linesPage);
    lineFilter_ = new QComboBox;
    lineFilter_->addItems({"Unsupported", "All", "Used", "No DPS effect"});
    linesLayout->addWidget(lineFilter_);
    lines_ = new QTableWidget(0, 4);
    lines_->setHorizontalHeaderLabels({"Status", "Source", "Line", "Reason"});
    lines_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    lines_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    lines_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lines_->setWordWrap(true);
    linesLayout->addWidget(lines_);
    connect(lineFilter_, &QComboBox::currentIndexChanged, this, &MainWindow::showLines);

    auto* tabs = new QTabWidget;
    tabs->addTab(output_, "DPS");
    tabs->addTab(linesPage, "Modifier lines");

    auto* splitter = new QSplitter;
    splitter->addWidget(scroll);
    splitter->addWidget(tabs);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    for (QDoubleSpinBox* s : {enemyRes_, armorPhys_, armorNonPhys_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &MainWindow::recalculate);
    connect(custom_, &QPlainTextEdit::textChanged, this, &MainWindow::recalculate);

    dataDir_ = findDataDir();
    if (dataDir_.isEmpty()) {
        output_->setHtml("<h2>Game data not found</h2><p>Could not find <code>data/json</code> next to the program "
                         "or above the working directory.</p>");
        return;
    }
    try {
        data_ = GameData::load(QDir::toNativeSeparators(dataDir_).toStdString());
    } catch (const std::exception& e) {
        output_->setHtml("<h2>Could not load game data</h2><p>" + html(e.what()) + "</p>");
        return;
    }
    const QString youga = QDir(dataDir_).filePath("builds/youga_ultimate_one.json");
    if (QFileInfo::exists(youga)) loadBuildPath(youga);
    else output_->setHtml("<p>Open a build file to start.</p>");
}

void MainWindow::openBuild() {
    const QString start = buildPath_.isEmpty() ? QDir(dataDir_).filePath("builds") : QFileInfo(buildPath_).path();
    const QString path = QFileDialog::getOpenFileName(this, "Open build", start, "Build files (*.json)");
    if (!path.isEmpty()) loadBuildPath(path);
}

void MainWindow::loadBuildPath(const QString& path) {
    try {
        std::ifstream in(std::filesystem::path(path.toStdWString()));
        build_ = nlohmann::json::parse(in);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Open build", "Could not read the build file:\n" + QString::fromUtf8(e.what()));
        return;
    }
    buildPath_ = path;
    loaded_ = true;
    buildLabel_->setText(QFileInfo(path).fileName());
    rebuildConfigPanel();
    recalculate();
}

BuildState MainWindow::loadState() const {
    nlohmann::json build = build_;
    nlohmann::json lines = nlohmann::json::array();
    for (const QString& l : custom_->toPlainText().split('\n'))
        if (!l.trimmed().isEmpty()) lines.push_back(l.trimmed().toStdString());
    if (!lines.empty()) {
        if (!build.contains("items")) build["items"] = nlohmann::json::array();
        build["items"].push_back({{"slot", "Custom"}, {"name", "modifiers"}, {"lines", lines}});
    }
    return loadBuild(data_, build);
}

// Lists the flags and values this build's modifiers actually check, like Path of Building's
// configuration tab, initialised from the build file.
void MainWindow::rebuildConfigPanel() {
    updating_ = true;
    while (flagsForm_->rowCount() > 0) flagsForm_->removeRow(0);
    while (varsForm_->rowCount() > 0) varsForm_->removeRow(0);
    flags_.clear();
    vars_.clear();

    BuildState base;
    try {
        base = loadState();
    } catch (const std::exception&) {
        updating_ = false;
        return;
    }
    enemyRes_->setValue(base.enemyResistance);
    armorPhys_->setValue(base.enemyArmorPhysical);
    armorNonPhys_->setValue(base.enemyArmorNonPhysical);

    std::set<std::string> flagNames, varNames(std::begin(kEngineVars), std::end(kEngineVars));
    for (const auto& [name, on] : base.config.flags) flagNames.insert(name);
    for (const auto& [name, v] : base.config.vars) varNames.insert(name);
    for (const Mod& m : base.mods.mods()) {
        for (const Condition& c : m.conditions) {
            if (!c.flag.empty()) flagNames.insert(c.flag);
            if (!c.var.empty()) varNames.insert(c.var);
        }
        if (m.per) varNames.insert(m.per->var);
    }
    for (const auto& part : base.main.attackComponents) flagNames.insert("exclude " + part.name);

    for (const std::string& name : flagNames) {
        if (kDerivedFlags.contains(name)) continue;
        auto* box = new QCheckBox(QString::fromStdString(name));
        box->setChecked(base.config.flag(name));
        flagsForm_->addRow(box);
        flags_[name] = box;
        connect(box, &QCheckBox::toggled, this, &MainWindow::recalculate);
    }
    for (const std::string& name : varNames) {
        if (kDerivedVars.contains(name) || name.find(" + ") != std::string::npos) continue;
        auto* spin = autoSpin(base.config.var(name).value_or(-1.0));
        varsForm_->addRow(QString::fromStdString(name), spin);
        vars_[name] = spin;
        connect(spin, &QDoubleSpinBox::valueChanged, this, &MainWindow::recalculate);
    }
    updating_ = false;
}

void MainWindow::recalculate() {
    if (!loaded_ || updating_) return;
    SkillDpsResult r;
    try {
        state_ = loadState();
        for (const auto& [name, box] : flags_) state_.config.flags[name] = box->isChecked();
        for (const auto& [name, spin] : vars_) {
            if (spin->value() >= 0.0) state_.config.vars[name] = spin->value();
            else state_.config.vars.erase(name);
        }
        state_.enemyResistance = enemyRes_->value();
        state_.enemyArmorPhysical = armorPhys_->value();
        state_.enemyArmorNonPhysical = armorNonPhys_->value();
        r = calculateMainSkillDps(state_);
    } catch (const std::exception& e) {
        output_->setHtml("<h2>Calculation failed</h2><p>" + html(e.what()) + "</p>");
        return;
    }

    int unsupported = 0;
    for (const LineReport& l : state_.lines) unsupported += l.ok ? 0 : 1;

    QString out;
    out += QString("<h2>%1</h2>").arg(html(state_.main.name));
    out += QString("<h1>%1 DPS</h1>").arg(shortNumber(r.dps));
    if (unsupported > 0)
        out += QString("<p style='color:#c33'>%1 modifier lines are not supported yet and were left out; see the "
                       "<i>Modifier lines</i> tab.</p>").arg(unsupported);

    out += "<table cellspacing='0' cellpadding='3'>";
    auto row = [&out](const QString& name, const QString& value) {
        out += "<tr><td>" + name + "</td><td align='right'><b>" + value + "</b></td></tr>";
    };
    row("Hit DPS", shortNumber(r.hitDps));
    for (const AilmentDps& a : r.ailments)
        row(QString::fromStdString(a.name) + " DPS", shortNumber(a.dps) + QString(" (%1 stacks)").arg(a.stacks, 0, 'f', 2));
    if (r.reapingDps > 0) row("Reaping DPS", shortNumber(r.reapingDps));
    if (r.deteriorationDps > 0) row("Deterioration DPS", shortNumber(r.deteriorationDps));
    row("Average hit", shortNumber(r.averageHit));
    row("Casts / attacks per second", QString::number(r.castsPerSecond, 'f', 3));
    row("Hits per cast", QString::number(r.hitsPerCast, 'f', 2));
    if (r.tangles > 1) row("Tangles", QString::number(r.tangles, 'f', 0));
    row("Crit chance", QString::number(r.critChance * 100.0, 'f', 2) + " %");
    row("Crit multiplier", QString::number(r.critMultiplier * 100.0, 'f', 1) + " %");
    if (r.spellBurstsPerSecond > 0)
        row("Spell Bursts per second", QString("%1 x %2 casts").arg(r.spellBurstsPerSecond, 0, 'f', 3).arg(r.spellBurstCasts));
    if (r.multistrikeAttacks > 1) row("Attacks per Multistrike", QString::number(r.multistrikeAttacks, 'f', 2));
    out += "</table>";

    if (r.components.size() > 1) {
        out += "<h3>Parts</h3><ul>";
        for (const ComponentDps& c : r.components)
            out += QString("<li>%1: %2%3</li>").arg(html(c.name), shortNumber(c.dps), c.included ? "" : " (excluded)");
        out += "</ul>";
    }
    for (const auto& [section, lines] : r.breakdown.sections) {
        out += "<h3>" + html(section) + "</h3><ul>";
        for (const std::string& line : lines) out += "<li>" + html(line) + "</li>";
        out += "</ul>";
    }
    output_->setHtml(out);
    showLines();
}

void MainWindow::showLines() {
    const int filter = lineFilter_->currentIndex();   // Unsupported, All, Used, No DPS effect
    lines_->setRowCount(0);
    for (const LineReport& l : state_.lines) {
        const QString status = !l.ok ? "Unsupported" : l.inert ? "No DPS effect" : "Used";
        if (filter != 1 && status != lineFilter_->currentText()) continue;
        const int row = lines_->rowCount();
        lines_->insertRow(row);
        auto* statusItem = new QTableWidgetItem(status);
        if (!l.ok) statusItem->setForeground(QColor(200, 50, 50));
        lines_->setItem(row, 0, statusItem);
        lines_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(l.source)));
        lines_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(l.line)));
        lines_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(l.error)));
    }
    lines_->resizeRowsToContents();
}
