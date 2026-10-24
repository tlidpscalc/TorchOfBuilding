#include "MainWindow.h"

#include "Editors.h"
#include "HeroEditor.h"
#include "TalentEditor.h"

#include "tob/ModParser.h"
#include "tob/SkillDps.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QStatusBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>
#include <QEvent>
#include <filesystem>
#include <fstream>
#include <set>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

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

nlohmann::json readJsonFile(const QString& path) {
    std::ifstream in(std::filesystem::path(path.toStdWString()));
    if (!in) throw std::runtime_error("cannot open " + path.toStdString());
    return nlohmann::json::parse(in);
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

void removeString(nlohmann::json& list, const std::string& value) {
    if (!list.is_array()) return;
    list.erase(std::remove(list.begin(), list.end(), nlohmann::json(value)), list.end());
}

// Fusion style with a dark or light palette, the same on every Windows version.
// Windows draws the title bar itself; DWM switches it to dark (Windows 10 20H1+ and 11).
bool g_darkTitleBars = false;

void setTitleBarDark(QWidget* window, bool dark) {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL value = dark ? TRUE : FALSE;
    constexpr DWORD kUseImmersiveDarkMode = 20;   // DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &value, sizeof(value));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);   // redraw now
#else
    (void)window;
    (void)dark;
#endif
}

// Gives every window shown later (dialogs, message boxes) the current title bar.
class TitleBarFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
        // Only real windows and dialogs: tooltips, menus and dropdown lists are windows too, and
        // forcing a frame redraw on them hides them again straight away.
        if (event->type() == QEvent::Show)
            if (auto* w = qobject_cast<QWidget*>(object); w && w->isWindow()) {
                const Qt::WindowType type = w->windowType();
                if (type == Qt::Window || type == Qt::Dialog) setTitleBarDark(w, g_darkTitleBars);
            }
        return false;
    }
};

void applyTheme(bool dark) {
    static TitleBarFilter* filter = [] {
        auto* f = new TitleBarFilter(qApp);
        qApp->installEventFilter(f);
        return f;
    }();
    (void)filter;
    g_darkTitleBars = dark;
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->isVisible() && (w->windowType() == Qt::Window || w->windowType() == Qt::Dialog)) setTitleBarDark(w, dark);

    QApplication::setStyle("Fusion");
    if (!dark) {
        QApplication::setPalette(QApplication::style()->standardPalette());
        return;
    }
    QPalette p;
    const QColor window(37, 37, 38), base(30, 30, 30), alternate(45, 45, 48), text(220, 220, 220),
        disabled(128, 128, 128), highlight(42, 130, 218);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, alternate);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, alternate);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(90, 160, 240));
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Mid, QColor(60, 60, 60));
    p.setColor(QPalette::Dark, QColor(25, 25, 25));
    p.setColor(QPalette::Light, QColor(70, 70, 70));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    QApplication::setPalette(p);
}

nlohmann::json emptyBuild() {
    return {{"schema", 1},
            {"skills", {{"main", ""}, {"active", nlohmann::json::array()}, {"passive", nlohmann::json::array()}}},
            {"items", nlohmann::json::array()},
            {"config", {{"flags", nlohmann::json::array()}, {"vars", nlohmann::json::object()}}}};
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* toolbar = addToolBar("Build");
    toolbar->setMovable(false);
    QAction* newAction = toolbar->addAction("New");
    QAction* open = toolbar->addAction("Open...");
    QAction* save = toolbar->addAction("Save");
    QAction* saveAs = toolbar->addAction("Save as...");
    QAction* reload = toolbar->addAction("Reload");
    newAction->setShortcut(QKeySequence::New);
    open->setShortcut(QKeySequence::Open);
    save->setShortcut(QKeySequence::Save);
    saveAs->setShortcut(QKeySequence::SaveAs);
    connect(newAction, &QAction::triggered, this, &MainWindow::newBuild);
    connect(open, &QAction::triggered, this, &MainWindow::openBuild);
    connect(save, &QAction::triggered, this, &MainWindow::saveBuild);
    connect(saveAs, &QAction::triggered, this, &MainWindow::saveBuildAs);
    connect(reload, &QAction::triggered, this, [this] {
        if (!buildPath_.isEmpty() && confirmDiscard()) loadBuildPath(buildPath_);
    });
    // Dark mode is the default; the choice is remembered between runs.
    QAction* dark = toolbar->addAction("Dark mode");
    dark->setCheckable(true);
    dark->setChecked(QSettings().value("darkMode", true).toBool());
    applyTheme(dark->isChecked());
    connect(dark, &QAction::toggled, this, [](bool on) {
        QSettings().setValue("darkMode", on);
        applyTheme(on);
    });
    toolbar->addSeparator();
    buildLabel_ = new QLabel("No build loaded");
    toolbar->addWidget(buildLabel_);

    recalcTimer_ = new QTimer(this);
    recalcTimer_->setSingleShot(true);
    recalcTimer_->setInterval(300);   // typing in a line editor recalculates once it pauses
    connect(recalcTimer_, &QTimer::timeout, this, [this] {
        recalculate();
        if (rebuildPending_) rebuildConfigPanel();
        rebuildPending_ = false;
    });

    // Config tab: enemy, conditions and values.
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
    panelLayout->addStretch(1);
    auto* configScroll = new QScrollArea;
    configScroll->setWidget(panel);
    configScroll->setWidgetResizable(true);
    for (QDoubleSpinBox* s : {enemyRes_, armorPhys_, armorNonPhys_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &MainWindow::setEnemy);

    auto* editTabs = new QTabWidget;
    editTabs->setMinimumWidth(420);

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
    auto* resultTabs = new QTabWidget;
    resultTabs->addTab(output_, "DPS");
    resultTabs->addTab(linesPage, "Modifier lines");
    // The line table is slow to fill, so it is refreshed only while it is shown.
    connect(resultTabs, &QTabWidget::currentChanged, this, [this, linesPage, resultTabs] {
        if (resultTabs->currentWidget() == linesPage && linesStale_) showLines();
    });

    // Like Path of Building: results in a narrow panel on the left, the editors take the rest. The
    // divider position is remembered between runs.
    auto* splitter = new QSplitter;
    splitter->addWidget(resultTabs);
    splitter->addWidget(editTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    resultTabs->setMinimumWidth(300);
    splitter->setSizes({420, 1000});
    if (const QByteArray state = QSettings().value("splitter").toByteArray(); !state.isEmpty()) splitter->restoreState(state);
    connect(splitter, &QSplitter::splitterMoved, this, [splitter] { QSettings().setValue("splitter", splitter->saveState()); });
    setCentralWidget(splitter);

    dataDir_ = findDataDir();
    if (dataDir_.isEmpty()) {
        output_->setHtml("<h2>Game data not found</h2><p>Could not find <code>data/json</code> next to the program "
                         "or above the working directory.</p>");
        return;
    }
    try {
        data_ = GameData::load(QDir::toNativeSeparators(dataDir_).toStdString());
        const QDir json(QDir(dataDir_).filePath("json"));
        craftData_.legendaries = readJsonFile(json.filePath("legendaries.json"));
        craftData_.gearTypes = readJsonFile(json.filePath("gear_types.json"));
        craftData_.vorax = readJsonFile(json.filePath("vorax.json"));
        craftData_.crafting = readJsonFile(json.filePath("crafting.json"));
    } catch (const std::exception& e) {
        output_->setHtml("<h2>Could not load game data</h2><p>" + html(e.what()) + "</p>");
        return;
    }

    skillsEditor_ = new SkillsEditor(data_.skills, data_.traits);
    gearEditor_ = new GearEditor(craftData_);
    heroEditor_ = new HeroEditor(data_.traits, craftData_.crafting);
    talentEditor_ = new TalentEditor(data_.boards);
    editTabs->addTab(heroEditor_, "Hero");
    editTabs->addTab(talentEditor_, "Talents");
    editTabs->addTab(skillsEditor_, "Skills");
    editTabs->addTab(gearEditor_, "Gear");
    editTabs->addTab(configScroll, "Config");
    connect(skillsEditor_, &SkillsEditor::changed, this, [this] { buildEdited(true); });
    connect(gearEditor_, &GearEditor::changed, this, [this] { buildEdited(true); });
    connect(talentEditor_, &TalentEditor::changed, this, [this] { buildEdited(true); });
    // Trait changes can add or remove supports a trait grants (Clockwork Core).
    connect(heroEditor_, &HeroEditor::changed, this, [this] {
        if (SkillsEditor::applyTraitSupports(build_, data_.skills, data_.traits)) skillsEditor_->setBuild(&build_);
        buildEdited(true);
    });

    const QString youga = QDir(dataDir_).filePath("builds/youga_ultimate_one.json");
    if (QFileInfo::exists(youga)) loadBuildPath(youga);
    else newBuild();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscard()) event->accept();
    else event->ignore();
}

bool MainWindow::confirmDiscard() {
    if (!dirty_) return true;
    const auto answer = QMessageBox::question(this, "Unsaved changes", "Save the changes to this build first?",
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) return saveBuild();
    return answer == QMessageBox::Discard;
}

void MainWindow::newBuild() {
    if (!confirmDiscard()) return;
    buildPath_.clear();
    setBuild(emptyBuild());
}

void MainWindow::openBuild() {
    if (!confirmDiscard()) return;
    const QString start = buildPath_.isEmpty() ? QDir(dataDir_).filePath("builds") : QFileInfo(buildPath_).path();
    const QString path = QFileDialog::getOpenFileName(this, "Open build", start, "Build files (*.json)");
    if (!path.isEmpty()) loadBuildPath(path);
}

void MainWindow::loadBuildPath(const QString& path) {
    nlohmann::json build;
    try {
        build = readJsonFile(path);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Open build", "Could not read the build file:\n" + QString::fromUtf8(e.what()));
        return;
    }
    buildPath_ = path;
    setBuild(std::move(build));
}

bool MainWindow::saveBuild() {
    if (buildPath_.isEmpty()) return saveBuildAs();
    std::ofstream out(std::filesystem::path(buildPath_.toStdWString()));
    if (!out) {
        QMessageBox::warning(this, "Save build", "Could not write " + buildPath_);
        return false;
    }
    out << build_.dump(1) << '\n';
    setDirty(false);
    return true;
}

bool MainWindow::saveBuildAs() {
    const QString start = buildPath_.isEmpty() ? QDir(dataDir_).filePath("builds/new_build.json") : buildPath_;
    const QString path = QFileDialog::getSaveFileName(this, "Save build", start, "Build files (*.json)");
    if (path.isEmpty()) return false;
    buildPath_ = path;
    return saveBuild();
}

void MainWindow::setBuild(nlohmann::json build) {
    build_ = std::move(build);
    loaded_ = true;
    if (heroEditor_) heroEditor_->setBuild(&build_);
    if (talentEditor_) talentEditor_->setBuild(&build_);
    if (skillsEditor_) skillsEditor_->setBuild(&build_);
    if (gearEditor_) gearEditor_->setBuild(&build_);
    setDirty(false);
    recalculate();
    rebuildConfigPanel();
}

void MainWindow::setDirty(bool dirty) {
    dirty_ = dirty;
    const QString name = buildPath_.isEmpty() ? "Untitled build" : QFileInfo(buildPath_).fileName();
    buildLabel_->setText(name + (dirty ? " *" : ""));
    setWindowTitle(name + (dirty ? " *" : "") + " - TorchOfBuilding");
}

// Skill and gear edits can add or remove conditions, so they also rebuild the config panel. Config
// edits only recalculate, so the box being edited keeps its focus.
void MainWindow::buildEdited(bool linesChanged) {
    if (updating_) return;
    setDirty(true);
    rebuildPending_ = rebuildPending_ || linesChanged;
    recalcTimer_->start();
}

void MainWindow::setConfigFlag(const std::string& name, bool on) {
    nlohmann::json& cfg = build_["config"];
    removeString(cfg["flags"], name);
    removeString(cfg["flags_off"], name);
    cfg[on ? "flags" : "flags_off"].push_back(name);
    if (cfg["flags_off"].empty()) cfg.erase("flags_off");
    buildEdited(false);
}

void MainWindow::setConfigVar(const std::string& name, double value) {
    nlohmann::json& vars = build_["config"]["vars"];
    if (value >= 0.0) vars[name] = value;
    else vars.erase(name);
    buildEdited(false);
}

void MainWindow::setEnemy() {
    if (updating_) return;
    build_["config"]["enemy"] = {{"resistance", enemyRes_->value()},
                                 {"armor_physical", armorPhys_->value()},
                                 {"armor_non_physical", armorNonPhys_->value()}};
    buildEdited(false);
}

// Lists the flags and values this build's modifiers actually check, like Path of Building's
// configuration tab, with their current state in the build. Uses the state the last recalculation
// loaded, so call it after recalculate().
void MainWindow::rebuildConfigPanel() {
    updating_ = true;
    while (flagsForm_->rowCount() > 0) flagsForm_->removeRow(0);
    while (varsForm_->rowCount() > 0) varsForm_->removeRow(0);
    const BuildState& base = state_;
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
        connect(box, &QCheckBox::toggled, this, [this, name](bool on) { setConfigFlag(name, on); });
    }
    const nlohmann::json vars = build_.value("config", nlohmann::json::object()).value("vars", nlohmann::json::object());
    for (const std::string& name : varNames) {
        if (kDerivedVars.contains(name) || name.find(" + ") != std::string::npos) continue;
        // Values set in the build file; everything else shows "auto".
        auto* spin = autoSpin(vars.contains(name) ? vars[name].get<double>() : base.config.var(name).value_or(-1.0));
        varsForm_->addRow(QString::fromStdString(name), spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, name](double v) { setConfigVar(name, v); });
    }
    updating_ = false;
}

void MainWindow::recalculate() {
    if (!loaded_) return;
    SkillDpsResult r;
    QElapsedTimer clock;
    clock.start();
    try {
        state_ = loadBuild(data_, build_);
        r = calculateMainSkillDps(state_);
        statusBar()->showMessage(QString("Recalculated in %1 ms").arg(clock.elapsed()));
    } catch (const std::exception& e) {
        output_->setHtml("<h2>Calculation failed</h2><p>" + html(e.what()) + "</p>");
        return;
    }
    if (state_.main.name.empty()) {
        output_->setHtml("<h2>No main skill</h2><p>Pick a main skill on the <i>Skills</i> tab.</p>");
        linesStale_ = true;
        if (lines_->isVisible()) showLines();
        return;
    }

    int unsupported = 0;
    for (const LineReport& l : state_.lines) unsupported += l.ok ? 0 : 1;

    QString out;
    out += QString("<h2>%1</h2>").arg(html(state_.main.name));
    out += QString("<h1>%1 DPS</h1>").arg(shortNumber(r.dps));
    if (unsupported > 0)
        out += QString("<p style='color:#e05555'>%1 modifier lines are not supported yet and were left out; see the "
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
    linesStale_ = true;
    if (lines_->isVisible()) showLines();
}

void MainWindow::showLines() {
    linesStale_ = false;
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
