#pragma once

#include <QWidget>

class Label;
class QHBoxLayout;

// The 64 px page header (Library page: page header): title, a quiet subtitle next to it, actions on
// the right and a hairline below.
//   auto* h = new PageHeader(QStringLiteral("Library"));
//   h->setSubtitle(QStringLiteral("3 games"));
//   h->addAction(searchBox);
class PageHeader : public QWidget {
    Q_OBJECT
public:
    explicit PageHeader(const QString& title, QWidget* parent = nullptr);
    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    void addAction(QWidget* w);
    void addLeading(QWidget* w); // before the title (a back button)

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QHBoxLayout* m_row;
    Label* m_title;
    Label* m_subtitle;
    int m_leading = 0;
};
