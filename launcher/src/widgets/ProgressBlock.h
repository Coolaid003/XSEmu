#pragma once

#include "theme/Theme.h"

#include <QPair>
#include <QWidget>

class Button;
class IconView;
class Label;
class Spinner;
class QHBoxLayout;

// The warm-up block (Components: Progress block): status glyph, title, game name, percent, an
// action ("Cancel"), a 6 px bar and columns of label/value. Running, paused, failed and finished
// all use it; the state picks the glyph and the bar colour.
//   block->setState(ProgressBlock::State::Running);
//   block->setColumns({{"Done", "27,292 / 41,906"}, {"Speed", "243 keys/s"}, ...});
class ProgressBlock : public QWidget {
    Q_OBJECT
public:
    enum class State { Idle, Running, Paused, Failed, Finished, Cancelled };
    using Column = QPair<QString, QString>;

    explicit ProgressBlock(QWidget* parent = nullptr);

    void setState(State state);
    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    void setFraction(double fraction); // 0..1, or < 0 to hide the percent
    void setPercentText(const QString& text); // overrides the computed percent
    void setColumns(const QList<Column>& columns, const QList<double>& stretch = {1.4, 1.1, 1.1, 1.0, 1.2});
    void setActionText(const QString& text); // empty hides the action
    void setNote(const QString& text);       // a line under the bar and columns; empty hides it
    void setColumnTone(int index, Theme::Role role); // after setColumns: the value's colour
    Button* actionButton() const { return m_action; }
    void addHeaderWidget(QWidget* w); // e.g. a primary button next to the percent
    State state() const { return m_state; }

signals:
    void actionClicked();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    State m_state = State::Running;
    double m_fraction = 0;
    Spinner* m_spinner;
    IconView* m_glyph;
    Label* m_title;
    Label* m_subtitle;
    Label* m_percent;
    Button* m_action;
    QHBoxLayout* m_header;
    QWidget* m_bar;
    QHBoxLayout* m_columns;
    Label* m_note;
};
