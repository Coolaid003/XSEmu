#pragma once

#include <QAbstractButton>
#include <QVariantAnimation>

// 36×20 switch (Components: Controls). Checkable; toggled(bool) is the change signal.
class Toggle : public QAbstractButton {
    Q_OBJECT
public:
    explicit Toggle(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {36, 20}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    QVariantAnimation m_anim;
    qreal m_pos = 0; // 0 off .. 1 on
};
