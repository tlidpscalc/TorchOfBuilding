#pragma once

#include <QStringList>
#include <QWidget>

#include <nlohmann/json.hpp>

class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QSpinBox;
class QToolButton;
class LinkCanvas;

// Talent pages as the game lays them out: page 1 the main god, page 2 one of that god's four
// professions, pages 3 and 4 any professions; a page opens at 24 points on the page before. Each panel is a
// 7x5 grid: column c needs 3c points in the columns to its left, a linked node needs its prerequisite
// fully allocated, Micro/Medium nodes take 3 points and Legendary Medium 1. Core talents: two on the main
// god (at 12 and 24 points), one on other panels. Total points are limited by a budget (115 at most).
// Nothing is blocked while editing: broken rules are listed as warnings for the user to fix.
//
// Build file: "talents" [{board, slot, core, nodes [[col,row,points]], prism}], "talent_points".
class TalentEditor : public QWidget {
    Q_OBJECT

public:
    TalentEditor(const nlohmann::json& boards, QWidget* parent = nullptr);
    void setBuild(nlohmann::json* build);

    // Every rule a build's talents break, empty when valid (used by the self-test).
    static QStringList problems(const nlohmann::json& build, const nlohmann::json& boards);

signals:
    void changed();

private:
    struct Row {
        QGroupBox* box;
        QComboBox* board;
        QComboBox* core[2];
        QLabel* points;
        QGridLayout* grid;
        LinkCanvas* canvas;   // draws the links between talents behind the buttons
        QToolButton* nodes[7][5];
    };

    nlohmann::json& panel(int row);
    const nlohmann::json* boardData(const std::string& slug) const;
    int panelPoints(int row);
    int spent();
    void refresh();
    void refreshRow(int row);
    void refreshNodes(int row);
    void refreshWarnings();
    void click(int row, int col, int r, int delta);
    bool eventFilter(QObject* watched, QEvent* event) override;

    const nlohmann::json& boards_;
    nlohmann::json* build_ = nullptr;
    bool updating_ = false;
    QSpinBox* budget_;
    QLabel* spent_;
    QLabel* warnings_;
    Row rows_[4];
};
