#include "SearchBox.h"

#include "Icon.h"
#include "theme/Theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShortcut>

SearchBox::SearchBox(const QString& placeholder, QWidget* parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
{
    setFixedHeight(32);
    setMinimumWidth(160);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::IBeamCursor);
    m_edit->setFrame(false);
    m_edit->setPlaceholderText(placeholder);
    m_edit->installEventFilter(this);
    setFocusProxy(m_edit);
    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString& t) {
        relayout();
        update();
        emit textChanged(t);
    });
    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, &SearchBox::applyTheme);
}

void SearchBox::applyTheme()
{
    m_edit->setFont(Theme::font(13));
    relayout();
    const QColor c = Theme::color(Theme::Role::Text);
    m_edit->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; min-height: 0; color: rgba(%1, %2, %3, %4); }")
                              .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
    // The placeholder colour has no style sheet property; set it after the repolish that the
    // theme switch queues, so the restored palette does not win.
    QMetaObject::invokeMethod(this, [this] {
        QPalette pal = m_edit->palette();
        pal.setColor(QPalette::PlaceholderText, Theme::color(Theme::Role::Text3));
        m_edit->setPalette(pal);
    }, Qt::QueuedConnection);
    update();
}

void SearchBox::installShortcut(QWidget* window)
{
    auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), window);
    connect(sc, &QShortcut::activated, this, [this] {
        m_edit->setFocus(Qt::ShortcutFocusReason);
        m_edit->selectAll();
    });
}

QRect SearchBox::clearRect() const
{
    return QRect(width() - 8 - 16, (height() - 16) / 2, 16, 16);
}

void SearchBox::resizeEvent(QResizeEvent*)
{
    relayout();
}

void SearchBox::relayout()
{
    const int left = 10 + 14 + 8;
    const int right = 8 + (m_edit->text().isEmpty() ? QFontMetrics(Theme::mono(11)).horizontalAdvance(QStringLiteral("Ctrl K")) : 16) + 8;
    m_edit->setGeometry(left, 0, width() - left - right, height());
}

bool SearchBox::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_edit) {
        if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut)
            update();
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape && !m_edit->text().isEmpty()) {
            m_edit->clear();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void SearchBox::mousePressEvent(QMouseEvent* e)
{
    if (!m_edit->text().isEmpty() && clearRect().adjusted(-4, -4, 4, 4).contains(e->position().toPoint())) {
        m_edit->clear();
        m_edit->setFocus();
        return;
    }
    m_edit->setFocus(Qt::MouseFocusReason);
}

void SearchBox::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath path;
    path.addRoundedRect(r, 7, 7);
    p.fillPath(path, c.surface);
    const bool focused = m_edit->hasFocus();
    if (focused) {
        p.setPen(QPen(c.accentSoft, 3));
        p.drawRoundedRect(r.adjusted(-0.5, -0.5, 0.5, 0.5), 7.5, 7.5);
        p.setPen(QPen(c.accent, 1));
    } else {
        p.setPen(QPen(c.borderStrong, 1));
    }
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 6.5, 6.5);
    Icon::paint(p, QRectF(10, 0, 14, height()), QStringLiteral("search"), c.text3, 14);
    if (m_edit->text().isEmpty()) {
        p.setFont(Theme::mono(11));
        p.setPen(c.text3);
        p.drawText(QRect(0, 0, width() - 8, height()), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("Ctrl K"));
    } else {
        Icon::paint(p, clearRect(), QStringLiteral("close"), c.text3, 12);
    }
}
