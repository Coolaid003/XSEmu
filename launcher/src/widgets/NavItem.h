#pragma once

#include "theme/Theme.h"

#include <QAbstractButton>

// A 34 px sidebar row (Library page: sidebar): icon, label, and on the right a count, a percent or a
// lock. Checkable; the checked row gets the sunken fill.
class NavItem : public QAbstractButton {
    Q_OBJECT
public:
    explicit NavItem(const QString& text, const QString& icon = {}, QWidget* parent = nullptr);
    void setTrailingText(const QString& text, Theme::Role role = Theme::Role::Text3);
    void setTrailingIcon(const QString& icon); // replaces the text; empty clears
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QString m_icon;
    QString m_trailing;
    QString m_trailingIcon;
    Theme::Role m_trailingRole = Theme::Role::Text3;
    bool m_hover = false;
};
