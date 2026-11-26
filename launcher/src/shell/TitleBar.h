#pragma once

#include <QWidget>

class BrandMark;
class Label;
class QHBoxLayout;

// The 32 px title bar of the frameless window: an empty drag area with the window buttons. Setup
// shows the mark and a caption ("XSEmu · Setup"); the library has its mark in the sidebar instead.
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);
    void setCaption(const QString& caption); // empty hides mark and caption

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    BrandMark* m_mark;
    Label* m_caption;
};

// Shared by the title bar and the in-game bar: press-drag moves the window, double-click maximises.
namespace WindowDrag {
void press(QWidget* w, QMouseEvent* e);
void doubleClick(QWidget* w, QMouseEvent* e);
}
