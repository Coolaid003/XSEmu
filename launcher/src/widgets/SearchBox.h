#pragma once

#include <QLineEdit>
#include <QWidget>

// 260×32 search field: magnifier, placeholder, "Ctrl K" hint while empty, clear button while not
// (Components: Controls). installShortcut(window) makes Ctrl+K focus it.
class SearchBox : public QWidget {
    Q_OBJECT
public:
    explicit SearchBox(const QString& placeholder = QStringLiteral("Search games"), QWidget* parent = nullptr);

    QString text() const { return m_edit->text(); }
    void setText(const QString& t) { m_edit->setText(t); }
    QLineEdit* lineEdit() const { return m_edit; }
    void installShortcut(QWidget* window);

    QSize sizeHint() const override { return {260, 32}; }

signals:
    void textChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QRect clearRect() const;
    void relayout();
    void applyTheme();

    QLineEdit* m_edit;
};
