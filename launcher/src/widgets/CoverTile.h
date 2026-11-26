#pragma once

#include "theme/Theme.h"

#include <QPixmap>
#include <QWidget>

// A game in the cover grid (Components: Cover tiles): 168×252 art, title and a status line below.
//   Art       cover art (600×900 SteamGridDB grid, cropped to fill)
//   NoArt     the "NO COVER ART" placeholder with the title written large
//   AddGames  the dashed "Add games / or drop a folder here" tile (no caption)
// Locked adds the lock chip; the status line then carries the reason ("Stop Signal Garden to play").
// Hovering an unlocked tile shows the play button; selected draws the accent ring.
class CoverTile : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Art, NoArt, AddGames };
    static constexpr int ArtWidth = 168;
    static constexpr int ArtHeight = 252;
    static constexpr int Ring = 4; // room for the selection ring around the art

    explicit CoverTile(Kind kind = Kind::NoArt, QWidget* parent = nullptr);

    void setKind(Kind kind);
    void setTitle(const QString& title);
    void setArt(const QPixmap& art); // also switches to Kind::Art (NoArt when null)
    void setStatus(const QString& text, Theme::Tone tone);
    void setLocked(bool locked, const QString& reason = {});
    void setSelected(bool selected);
    Kind kind() const { return m_kind; }
    bool isLocked() const { return m_locked; }
    QString title() const { return m_title; }

    QSize sizeHint() const override;

signals:
    void clicked();
    void playClicked();
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QRectF artRect() const;
    QRectF playRect() const;

    Kind m_kind;
    QString m_title;
    QPixmap m_art;
    QString m_status;
    Theme::Tone m_tone = Theme::Tone::Neutral;
    bool m_locked = false;
    QString m_reason;
    bool m_selected = false;
    bool m_hover = false;
    bool m_hoverPlay = false;
};
