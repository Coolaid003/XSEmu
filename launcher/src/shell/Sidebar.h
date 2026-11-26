#pragma once

#include "Page.h"

#include <QWidget>

class Badge;
class Label;
class NavItem;
class NowPlayingCard;
class Services;

// The 224 px sidebar (Library page: sidebar): mark and name, the three destinations, NOW PLAYING
// while a game is booting, running or paused, and the console/network chips at the bottom.
class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(Services& services, QWidget* parent = nullptr);
    void setCurrent(Page page);

signals:
    void navigate(Page page);
    void nowPlayingClicked();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void refresh();

    Services& m_s;
    NavItem* m_library;
    NavItem* m_warmup;
    NavItem* m_settings;
    Label* m_nowLabel;
    NowPlayingCard* m_now;
    Badge* m_console;
    Badge* m_network;
};
