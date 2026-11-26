#pragma once

#include "theme/Theme.h"

#include <QIcon>
#include <QPixmap>
#include <QWidget>

// Original line icons (resources/icons/<name>.svg, stroke="currentColor") rendered in any colour.
// Names: library bolt gear search plus play pause stop overlay fullscreen back lock folder info
// keyboard monitor file network network-off chip gamepad warning check refresh close chevron-down
// chevron-right external key win-min win-max win-restore win-close.
namespace Icon {

QPixmap pixmap(const QString& name, const QColor& color, int size, qreal dpr = 1.0);
QIcon icon(const QString& name, const QColor& color, int size = 16);
// Paint centred in rect.
void paint(QPainter& p, const QRectF& rect, const QString& name, const QColor& color, int size);

// The app mark (assets/logo): the light or dark variant for the current theme.
QPixmap appMark(int size, qreal dpr = 1.0);
QIcon appIcon();

}

// A fixed-size icon that follows the theme: IconView("lock", Theme::Role::Text3, 13).
class IconView : public QWidget {
    Q_OBJECT
public:
    IconView(const QString& name, Theme::Role role, int size = 16, QWidget* parent = nullptr);
    void setIcon(const QString& name);
    void setRole(Theme::Role role);
    QSize sizeHint() const override { return {m_size, m_size}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_name;
    Theme::Role m_role;
    int m_size;
};

// The app mark as a widget (sidebar header, in-game bar).
class BrandMark : public QWidget {
    Q_OBJECT
public:
    explicit BrandMark(int size, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {m_size, m_size}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int m_size;
};
