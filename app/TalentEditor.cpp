#include "TalentEditor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QSpinBox>
#include <QCursor>
#include <QEvent>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

constexpr int kCols = 7;
constexpr int kRows = 5;
constexpr int kRowUnlock = 24;     // points in the row above (owner)
constexpr int kMaxPoints = 115;

const char* kSlots[] = {"main", "sub", "advanced", "advanced"};

// Each god's four professions, in tlidb's Talent page order (owner).
const std::map<std::string, std::vector<std::string>>& professions() {
    static const std::map<std::string, std::vector<std::string>> k = {
        {"God_of_Might", {"The_Brave", "Onslaughter", "Warlord", "Warrior"}},
        {"Goddess_of_Hunting", {"Marksman", "Bladerunner", "Druid", "Assassin"}},
        {"Goddess_of_Knowledge", {"Magister", "Arcanist", "Elementalist", "Prophet"}},
        {"God_of_War", {"Shadowdancer", "Ronin", "Ranger", "Sentinel"}},
        {"Goddess_of_Deception", {"Shadowmaster", "Psychic", "Warlock", "Lich"}},
        {"God_of_Machines", {"Machinist", "Steel_Vanguard", "Alchemist", "Artisan"}},
    };
    return k;
}

bool isGod(const std::string& slug) { return professions().contains(slug); }

const nlohmann::json* findBoard(const nlohmann::json& boards, const std::string& slug) {
    for (const auto& b : boards)
        if (b.value("slug", "") == slug) return &b;
    return nullptr;
}

const nlohmann::json* nodeAt(const nlohmann::json& board, int col, int row) {
    for (const auto& n : board["nodes"])
        if (n.value("col", -1) == col && n.value("row", -1) == row) return &n;
    return nullptr;
}

// Points per grid cell of a panel; the prism slot counts as a fully allocated node that needs nothing.
std::map<std::pair<int, int>, int> allocation(const nlohmann::json& panel) {
    std::map<std::pair<int, int>, int> out;
    for (const auto& n : panel.value("nodes", nlohmann::json::array())) out[{n[0].get<int>(), n[1].get<int>()}] += n[2].get<int>();
    return out;
}

std::pair<int, int> prismSlot(const nlohmann::json& panel) {
    if (!panel.contains("prism")) return {-1, -1};
    const auto slot = panel["prism"].value("slot", std::vector<int>{});
    return slot.size() == 2 ? std::pair{slot[0], slot[1]} : std::pair{-1, -1};
}

int panelTotal(const nlohmann::json& panel) {
    int total = 0;
    for (const auto& [cell, points] : allocation(panel)) total += points;
    return total;
}

// Rule breaks inside one panel: node points, column requirements (points in the columns to the left),
// linked prerequisites (fully allocated), core talents.
QStringList panelProblems(const nlohmann::json& board, const nlohmann::json& panel, bool mainGod) {
    QStringList out;
    const QString name = qs(board.value("name", ""));
    const auto alloc = allocation(panel);
    const auto prism = prismSlot(panel);
    int byCol[kCols] = {};
    for (const auto& [cell, points] : alloc)
        if (cell.first >= 0 && cell.first < kCols) byCol[cell.first] += points;
    auto full = [&](int col, int row) {
        if (std::pair{col, row} == prism) return true;
        const nlohmann::json* n = nodeAt(board, col, row);
        auto it = alloc.find({col, row});
        return n && it != alloc.end() && it->second >= n->value("max_points", 3);
    };
    for (const auto& [cell, points] : alloc) {
        if (points <= 0) continue;
        const auto [col, row] = cell;
        const nlohmann::json* n = nodeAt(board, col, row);
        if (!n) {
            out << QString("%1: no talent at column %2, row %3").arg(name).arg(col + 1).arg(row + 1);
            continue;
        }
        const QString talent = QString("column %1, row %2").arg(col + 1).arg(row + 1);
        if (points > n->value("max_points", 3))
            out << QString("%1: too many points in %2 (%3 of %4)").arg(name, talent).arg(points).arg(n->value("max_points", 3));
        int left = 0;
        for (int c = 0; c < col; ++c) left += byCol[c];
        if (left < n->value("required_points", 0))
            out << QString("%1: not enough points for column %2 (needs %3 in earlier columns, has %4)")
                       .arg(name).arg(col + 1).arg(n->value("required_points", 0)).arg(left);
        for (const auto& link : board.value("links", nlohmann::json::array()))
            if (link["to"][0] == col && link["to"][1] == row && !full(link["from"][0], link["from"][1]))
                out << QString("%1: %2 needs column %3, row %4 fully allocated")
                           .arg(name, talent).arg(link["from"][0].get<int>() + 1).arg(link["from"][1].get<int>() + 1);
    }
    const int total = panelTotal(panel);
    const auto cores = panel.value("core", nlohmann::json::array());
    if (cores.size() > (mainGod ? 2u : 1u)) out << QString("%1: too many core talents").arg(name);
    std::set<int> thresholds;
    for (const auto& c : cores)
        for (const auto& option : board["core_talents"])
            if (option.value("name", "") == c.get<std::string>()) {
                const int need = option.value("required_points", 24);
                if (total < need)
                    out << QString("%1: not enough points for core talent %2 (needs %3, has %4)")
                               .arg(name, qs(c.get<std::string>())).arg(need).arg(total);
                if (mainGod && !thresholds.insert(need).second)
                    out << QString("%1: two core talents from the %2-point tier (pick one at 12 and one at 24)").arg(name).arg(need);
            }
    return out;
}

} // namespace

// The grid's background: a line from each prerequisite to the talent it unlocks, green once the
// prerequisite is fully allocated. The talent buttons are children, drawn on top.
class LinkCanvas : public QWidget {
public:
    struct Link {
        QWidget* from;
        QWidget* to;
        bool met;
    };
    std::vector<Link> links;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (const Link& l : links) {
            if (!l.from->isVisible() || !l.to->isVisible()) continue;
            painter.setPen(QPen(l.met ? QColor(63, 163, 77) : QColor(120, 120, 120), l.met ? 4 : 3));
            const QRect a = l.from->geometry(), b = l.to->geometry();
            painter.drawLine(a.center(), b.center());
        }
    }
};

QStringList TalentEditor::problems(const nlohmann::json& build, const nlohmann::json& boards) {
    QStringList out;
    const auto talents = build.value("talents", nlohmann::json::array());
    int total = 0;
    std::set<std::string> used;
    std::string god;
    int previous = kMaxPoints;
    for (int r = 0; r < 4; ++r) {
        const nlohmann::json panel = r < static_cast<int>(talents.size()) ? talents[r] : nlohmann::json::object();
        const std::string slug = panel.value("board", "");
        const int points = slug.empty() ? 0 : panelTotal(panel);
        if (slug.empty()) {
            previous = 0;
            continue;
        }
        const nlohmann::json* board = findBoard(boards, slug);
        if (!board) {
            out << QString("Row %1: unknown panel %2").arg(r + 1).arg(qs(slug));
            continue;
        }
        const QString page = QString("Talent Page %1").arg(r + 1);
        if (r == 0 && !isGod(slug)) out << page + " must be a god";
        if (r == 0) god = slug;
        if (r == 1 && (!professions().contains(god) ||
                       std::ranges::find(professions().at(god), slug) == professions().at(god).end()))
            out << page + " must be one of the main god's professions";
        if (r >= 2 && isGod(slug)) out << page + " must be a profession";
        if (!used.insert(slug).second) out << QString("%1 is used twice").arg(qs(board->value("name", "")));
        if (r > 0 && previous < kRowUnlock)
            out << QString("%1 needs %2 points on Talent Page %3 (has %4)").arg(page).arg(kRowUnlock).arg(r).arg(previous);
        if (r == 0 && panel.contains("prism")) out << "Prisms only go in profession pages";
        out << panelProblems(*board, panel, r == 0);
        total += points;
        previous = points;
    }
    const int budget = build.value("talent_points", kMaxPoints);
    if (total > budget) out << QString("Too many points: %1 spent, %2 available").arg(total).arg(budget);
    return out;
}

TalentEditor::TalentEditor(const nlohmann::json& boards, QWidget* parent) : QWidget(parent), boards_(boards) {
    auto* outer = new QVBoxLayout(this);
    auto* head = new QHBoxLayout;
    head->addWidget(new QLabel("Talent points"));
    budget_ = new QSpinBox;
    budget_->setRange(0, kMaxPoints);
    head->addWidget(budget_);
    spent_ = new QLabel;
    head->addWidget(spent_, 1);
    outer->addLayout(head);
    warnings_ = new QLabel;
    warnings_->setWordWrap(true);
    warnings_->setStyleSheet("color: #e05555");
    outer->addWidget(warnings_);
    auto* hint = new QLabel("Left-click a talent to add a point, right-click to remove one. Hover for details.");
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    const char* titles[] = {"Talent Page 1: main god", "Talent Page 2: profession of the main god",
                            "Talent Page 3: profession", "Talent Page 4: profession"};
    for (int r = 0; r < 4; ++r) {
        Row& row = rows_[r];
        row.box = new QGroupBox(titles[r]);
        auto* v = new QVBoxLayout(row.box);
        auto* form = new QFormLayout;
        row.board = new QComboBox;
        row.points = new QLabel;
        form->addRow("Panel", row.board);
        for (int k = 0; k < 2; ++k) {
            row.core[k] = new QComboBox;
            form->addRow(r == 0 ? (k == 0 ? "Core talent (12)" : "Core talent (24)") : "Core talent", row.core[k]);
            if (r > 0 && k == 1) {
                row.core[k]->setVisible(false);
                form->labelForField(row.core[k])->setVisible(false);
            }
        }
        form->addRow("Points", row.points);
        v->addLayout(form);
        row.canvas = new LinkCanvas;
        row.grid = new QGridLayout(row.canvas);
        row.grid->setHorizontalSpacing(22);   // room to see the links
        row.grid->setVerticalSpacing(8);
        for (int c = 0; c < kCols; ++c)
            for (int rr = 0; rr < kRows; ++rr) {
                auto* b = new QToolButton;
                b->setFixedSize(124, 52);
                b->installEventFilter(this);   // tooltip as soon as the mouse is over it
                b->setContextMenuPolicy(Qt::CustomContextMenu);
                row.grid->addWidget(b, rr, c);
                row.nodes[c][rr] = b;
                connect(b, &QToolButton::clicked, this, [this, r, c, rr] { click(r, c, rr, +1); });
                connect(b, &QToolButton::customContextMenuRequested, this, [this, r, c, rr] { click(r, c, rr, -1); });
            }
        v->addWidget(row.canvas);
        layout->addWidget(row.box);

        connect(row.board, &QComboBox::currentIndexChanged, this, [this, r] {
            if (updating_ || !build_) return;
            nlohmann::json& p = panel(r);
            p = {{"board", rows_[r].board->currentData().toString().toStdString()}, {"slot", kSlots[r]},
                 {"core", nlohmann::json::array()}, {"nodes", nlohmann::json::array()}};
            // A new main god invalidates row 2's profession.
            if (r == 0) panel(1) = {{"board", ""}, {"slot", "sub"}, {"core", nlohmann::json::array()}, {"nodes", nlohmann::json::array()}};
            refresh();
            emit changed();
        });
        for (int k = 0; k < 2; ++k)
            connect(row.core[k], &QComboBox::currentIndexChanged, this, [this, r] {
                if (updating_ || !build_) return;
                nlohmann::json cores = nlohmann::json::array();
                for (QComboBox* box : rows_[r].core)
                    if (box->isVisibleTo(this) && !box->currentData().toString().isEmpty())
                        cores.push_back(box->currentData().toString().toStdString());
                panel(r)["core"] = cores;
                refreshWarnings();
                emit changed();
            });
    }
    layout->addStretch(1);
    connect(budget_, &QSpinBox::valueChanged, this, [this](int v) {
        if (updating_ || !build_) return;
        (*build_)["talent_points"] = v;
        refresh();
        emit changed();
    });
}

void TalentEditor::setBuild(nlohmann::json* build) {
    build_ = build;
    if (!build_) return;
    nlohmann::json& talents = (*build_)["talents"];
    if (!talents.is_array()) talents = nlohmann::json::array();
    while (talents.size() < 4)
        talents.push_back({{"board", ""}, {"slot", kSlots[talents.size()]}, {"core", nlohmann::json::array()},
                           {"nodes", nlohmann::json::array()}});
    refresh();
}

nlohmann::json& TalentEditor::panel(int row) { return (*build_)["talents"][row]; }

const nlohmann::json* TalentEditor::boardData(const std::string& slug) const { return findBoard(boards_, slug); }

int TalentEditor::panelPoints(int row) { return panel(row).value("board", "").empty() ? 0 : panelTotal(panel(row)); }

int TalentEditor::spent() {
    int total = 0;
    for (int r = 0; r < 4; ++r) total += panelPoints(r);
    return total;
}

// Adds or removes one point (within 0..max). Rules are not enforced here; broken ones show as warnings.
void TalentEditor::click(int row, int col, int r, int delta) {
    if (!build_) return;
    const nlohmann::json* board = boardData(panel(row).value("board", ""));
    const nlohmann::json* node = board ? nodeAt(*board, col, r) : nullptr;
    if (!node || prismSlot(panel(row)) == std::pair{col, r}) return;
    nlohmann::json& nodes = panel(row)["nodes"];
    if (!nodes.is_array()) nodes = nlohmann::json::array();
    auto it = std::find_if(nodes.begin(), nodes.end(), [&](const nlohmann::json& n) { return n[0] == col && n[1] == r; });
    const int current = it == nodes.end() ? 0 : (*it)[2].get<int>();
    const int next = std::clamp(current + delta, 0, node->value("max_points", 3));
    if (next == current) return;
    if (it == nodes.end()) nodes.push_back({col, r, next});
    else if (next == 0) nodes.erase(it);
    else (*it)[2] = next;
    refreshNodes(row);
    refreshWarnings();
    emit changed();
}

bool TalentEditor::eventFilter(QObject* watched, QEvent* event) {
    // Show the talent's details at once and keep them while the pointer is over it (Qt's own tooltip
    // would wait, then hide on the next mouse move).
    auto* b = qobject_cast<QToolButton*>(watched);
    if (b && !b->toolTip().isEmpty()) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::ToolTip) {
            QToolTip::showText(QCursor::pos(), b->toolTip(), b, b->rect(), 60000);
            return event->type() == QEvent::ToolTip;   // handled: no second, delayed tooltip
        }
        if (event->type() == QEvent::Leave) QToolTip::hideText();
    }
    return QWidget::eventFilter(watched, event);
}

void TalentEditor::refreshWarnings() {
    const QStringList issues = problems(*build_, boards_);
    spent_->setText(QString("%1 spent, %2 left").arg(spent()).arg(budget_->value() - spent()));
    warnings_->setText(issues.join("\n"));
    warnings_->setVisible(!issues.isEmpty());
    for (int r = 0; r < 4; ++r) rows_[r].points->setText(QString::number(panelPoints(r)));
}

void TalentEditor::refresh() {
    if (!build_) return;
    const bool wasUpdating = updating_;
    updating_ = true;
    budget_->setValue(build_->value("talent_points", kMaxPoints));
    for (int r = 0; r < 4; ++r) refreshRow(r);
    refreshWarnings();
    updating_ = wasUpdating;
}

void TalentEditor::refreshRow(int r) {
    Row& row = rows_[r];
    const nlohmann::json& p = panel(r);
    const std::string slug = p.value("board", "");

    // Panel choices for this row.
    std::set<std::string> elsewhere;
    for (int o = 0; o < 4; ++o)
        if (o != r) elsewhere.insert(panel(o).value("board", ""));
    row.board->clear();
    row.board->addItem("(none)", QString());
    for (const auto& b : boards_) {
        const std::string s = b.value("slug", "");
        bool allowed = false;
        if (r == 0) allowed = isGod(s);
        else if (r == 1) {
            const std::string god = panel(0).value("board", "");
            allowed = professions().contains(god) && std::ranges::find(professions().at(god), s) != professions().at(god).end();
        } else allowed = !isGod(s);
        if (allowed && (!elsewhere.contains(s) || s == slug)) row.board->addItem(qs(b.value("name", s)), qs(s));
    }
    if (row.board->findData(qs(slug)) < 0 && !slug.empty()) row.board->addItem(qs(slug), qs(slug));
    row.board->setCurrentIndex(std::max(0, row.board->findData(qs(slug))));

    const nlohmann::json* board = boardData(slug);
    const int total = slug.empty() ? 0 : panelTotal(p);
    row.points->setText(QString::number(total));
    const auto cores = p.value("core", nlohmann::json::array());
    for (int k = 0; k < 2; ++k) {
        QComboBox* box = row.core[k];
        box->clear();
        box->addItem("(none)", QString());
        if (board)
            for (const auto& c : (*board)["core_talents"]) {
                const int need = c.value("required_points", 24);
                QString tip = QString("Needs %1 points on this page\n").arg(need);
                for (const auto& m : c.value("mods", nlohmann::json::array())) tip += qs(m.get<std::string>()) + "\n";
                box->addItem(QString("%1 (%2)").arg(qs(c.value("name", ""))).arg(need), qs(c.value("name", "")));
                box->setItemData(box->count() - 1, tip.trimmed(), Qt::ToolTipRole);
            }
        // Each picker shows one of the saved cores, in order.
        const QString chosen = k < static_cast<int>(cores.size()) ? qs(cores[k].get<std::string>()) : QString();
        box->setCurrentIndex(std::max(0, box->findData(chosen)));
    }
    refreshNodes(r);
}

// Node buttons of one page: text, rich tooltip and border by allocation.
void TalentEditor::refreshNodes(int r) {
    Row& row = rows_[r];
    const nlohmann::json& p = panel(r);
    const nlohmann::json* board = boardData(p.value("board", ""));

    const auto alloc = allocation(p);
    const auto prism = prismSlot(p);
    // Links, met when the prerequisite is fully allocated (or is the prism slot).
    row.canvas->links.clear();
    if (board)
        for (const auto& link : board->value("links", nlohmann::json::array())) {
            const int fc = link["from"][0], fr = link["from"][1], tc = link["to"][0], tr = link["to"][1];
            if (fc < 0 || fc >= kCols || tc < 0 || tc >= kCols || fr < 0 || fr >= kRows || tr < 0 || tr >= kRows) continue;
            // A Prism removes its slot's links: nothing leads to it and nothing needs it (owner).
            if (std::pair{fc, fr} == prism || std::pair{tc, tr} == prism) continue;
            const nlohmann::json* from = nodeAt(*board, fc, fr);
            const auto it = alloc.find({fc, fr});
            const bool met = from && it != alloc.end() && it->second >= from->value("max_points", 3);
            row.canvas->links.push_back({row.nodes[fc][fr], row.nodes[tc][tr], met});
        }
    row.canvas->update();
    for (int c = 0; c < kCols; ++c)
        for (int rr = 0; rr < kRows; ++rr) {
            QToolButton* b = row.nodes[c][rr];
            const nlohmann::json* n = board ? nodeAt(*board, c, rr) : nullptr;
            b->setVisible(n != nullptr);
            if (!n) continue;
            const auto it = alloc.find({c, rr});
            const int points = it == alloc.end() ? 0 : it->second;
            const int max = n->value("max_points", 3);
            QString first = n->value("mods", nlohmann::json::array()).empty() ? qs(n->value("name", ""))
                                                                              : qs((*n)["mods"][0].get<std::string>());
            QString tip = "<b>" + qs(n->value("name", "")).toHtmlEscaped() + "</b>";
            for (const auto& m : n->value("mods", nlohmann::json::array())) tip += "<br>" + qs(m.get<std::string>()).toHtmlEscaped();
            tip += QString("<br><br>Points: %1 / %2").arg(points).arg(max);
            if (n->value("required_points", 0) > 0)
                tip += QString("<br>Needs %1 points in earlier columns").arg(n->value("required_points", 0));
            for (const auto& link : board->value("links", nlohmann::json::array()))
                if (link["to"][0] == c && link["to"][1] == rr &&
                    std::pair{link["from"][0].get<int>(), link["from"][1].get<int>()} != prism)
                    tip += QString("<br>Needs column %1, row %2 fully allocated")
                               .arg(link["from"][0].get<int>() + 1).arg(link["from"][1].get<int>() + 1);
            if (std::pair{c, rr} == prism) {
                b->setText("Prism\n" + qs(p["prism"].value("name", "")).remove("Ethereal Prism: "));
                QString prismTip = "<b>" + qs(p["prism"].value("name", "Ethereal Prism")).toHtmlEscaped() + "</b>";
                for (const auto& l : p["prism"].value("lines", nlohmann::json::array()))
                    prismTip += "<br>" + qs(l.get<std::string>()).toHtmlEscaped();
                b->setToolTip(prismTip + "<br><br>Replaces the talent in this slot");
                b->setStyleSheet("QToolButton { background: #2a1f38; color: #f0f0f0; border: 3px solid #b07ce0; border-radius: 4px; }");
                continue;
            }
            if (first.size() > 26) first = first.left(25) + "…";
            b->setText(first + QString("\n%1/%2").arg(points).arg(max));
            b->setToolTip(tip);
            // Type colour (Micro blue, Medium purple, Legendary Medium yellow); the border shows allocation.
            const int type = n->value("type", 1);
            const char* fill = type == 4 ? "#7a6416" : type == 3 ? "#4b2d73" : "#1f4470";
            const char* border = points >= max ? "3px solid #3fa34d" : points > 0 ? "2px dashed #f0f0f0" : "1px solid #555555";
            b->setStyleSheet(QString("QToolButton { background: %1; color: #f0f0f0; border: %2; border-radius: 4px; }")
                                 .arg(fill, border));
        }
}
