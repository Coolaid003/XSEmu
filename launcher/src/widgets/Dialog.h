#pragma once

#include <QWidget>

class Button;
class Card;
class QHBoxLayout;
class QVBoxLayout;

// A modal card over a scrim that covers the whole window (Error states: "Minecraft can't start").
// The dialog deletes itself when it closes.
//   auto* d = new Dialog(window());
//   d->setIcon(QStringLiteral("folder"), Theme::Tone::Danger);
//   d->body()->addWidget(...);
//   d->addButton(retry);  // right-aligned; addButton(b, true) puts it on the left
//   d->open();
class Dialog : public QWidget {
    Q_OBJECT
public:
    explicit Dialog(QWidget* window, int width = 500);
    void setIcon(const QString& icon, int tone); // Theme::Tone as int keeps Theme.h out of here
    void setTitle(const QString& title);
    QVBoxLayout* body() const { return m_body; }
    void addButton(Button* button, bool left = false);
    void open();

signals:
    void closed();

public slots:
    void dismiss();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    Card* m_card;
    QVBoxLayout* m_body;
    QHBoxLayout* m_buttons;
    QWidget* m_iconTile = nullptr;
    QWidget* m_titleLabel = nullptr;
    int m_leftButtons = 0;
};
