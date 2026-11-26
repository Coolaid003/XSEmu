#pragma once

#include <QAbstractButton>

class Label;

// A radio card (first launch: "Series S / Series X"): radio dot, title, an optional accent tag
// ("RECOMMENDED"), a line of facts and a quieter line of advice. Use in an exclusive QButtonGroup.
class ChoiceCard : public QAbstractButton {
    Q_OBJECT
public:
    ChoiceCard(const QString& title, const QString& facts, const QString& advice = {}, const QString& tag = {}, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_facts;
    QString m_advice;
    QString m_tag;
};
