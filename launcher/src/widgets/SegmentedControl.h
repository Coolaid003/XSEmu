#pragma once

#include <QStringList>
#include <QWidget>

// A row of 24 px segments on a sunken track (Components: Controls, "Window | Fullscreen").
//   auto* seg = new SegmentedControl({QStringLiteral("Default"), QStringLiteral("On"), QStringLiteral("Off")});
//   connect(seg, &SegmentedControl::currentChanged, ...);
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(const QStringList& items = {}, QWidget* parent = nullptr);

    void setItems(const QStringList& items);
    QStringList items() const { return m_items; }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    void setItemEnabled(int index, bool enabled);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QList<QRect> segmentRects() const;

    QStringList m_items;
    QList<bool> m_enabled;
    int m_current = 0;
};
