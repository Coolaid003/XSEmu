#pragma once

#include <QPixmap>
#include <QWidget>

class QPainter;

// A small cover (Game details 96×144, first-launch scan results 44×66, dialogs 24×36): the cover art
// cropped to fill, or the no-art fallback, a sunken card with the title's initials.
//   auto* thumb = new CoverThumb(QSize(44, 66));
//   thumb->setCover(QPixmap(artPath), game.title());
class CoverThumb : public QWidget {
    Q_OBJECT
public:
    explicit CoverThumb(QSize size, QWidget* parent = nullptr);
    void setCover(const QPixmap& art, const QString& title);

    QSize sizeHint() const override { return m_size; }

    // "Signal Garden" -> "SG": the first letter of the first two words.
    static QString initials(const QString& title);
    // Paint art (cropped to fill) or the fallback into rect.
    static void paint(QPainter& p, const QRectF& rect, const QPixmap& art, const QString& title, qreal radius);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QSize m_size;
    QPixmap m_art;
    QString m_title;
};
