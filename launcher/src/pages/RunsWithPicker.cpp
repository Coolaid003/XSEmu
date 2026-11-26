#include "RunsWithPicker.h"

#include "RunnerUi.h"
#include "app/Services.h"
#include "core/HardwareInfo.h"
#include "widgets/Badge.h"
#include "widgets/Button.h"
#include "widgets/Card.h"
#include "widgets/Icon.h"
#include "widgets/Text.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {

// How a runner shows on this PC, in the picker's words.
QString pickerLine(Services& s, const RunnerSpec& spec)
{
    using S = RunnerManager::State;
    const RunnerManager::Status st = s.runners.status(spec.id);
    const QString shows = spec.isBuiltIn() ? QStringLiteral("embeds the game in this window") : QStringLiteral("opens its own window");
    switch (st.state) {
    case S::BuiltIn: return QStringLiteral("Built in \u00B7 %1").arg(shows);
    case S::Installed: return QStringLiteral("Installed \u00B7 %1 \u00B7 its own shader cache").arg(shows);
    case S::UpdateAvailable: return QStringLiteral("Installed, update available \u00B7 %1").arg(shows);
    case S::Installing: return QStringLiteral("Installing now \u00B7 %1").arg(shows);
    case S::NotInstalled:
        return spec.install == RunnerSpec::Install::BuildFromSource ? QStringLiteral("Not installed \u00B7 builds from source on this PC")
                                                                    : QStringLiteral("Not installed \u00B7 downloads the pinned release");
    case S::Failed: return QStringLiteral("The last install failed");
    case S::Unavailable: return spec.windowsOnly ? QStringLiteral("Runs on Windows only") : QStringLiteral("Not available on this PC");
    }
    return {};
}

QWidget* runnerIcon(const RunnerSpec& spec, int size)
{
    if (spec.isBuiltIn())
        return new BrandMark(size);
    auto* tile = new Card(Card::Kind::Sunken, Metrics::RadiusMd);
    tile->setShadow(false);
    tile->setFixedSize(size, size);
    auto* l = new QVBoxLayout(tile);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new IconView(QStringLiteral("runner"), Theme::Role::Text2, size / 2), 0, Qt::AlignCenter);
    return tile;
}

// One runner in the picker; the chosen one has the accent wash and a check.
class RunnerRow : public QWidget {
public:
    RunnerRow(Services& s, const RunnerSpec& spec, bool chosen, bool isDefault, bool recommended, std::function<void()> onPick,
              std::function<void()> onInstall)
        : m_chosen(chosen)
        , m_onPick(std::move(onPick))
    {
        using S = RunnerManager::State;
        setObjectName(QStringLiteral("runsWith-") + spec.id);
        const RunnerManager::Status st = s.runners.status(spec.id);
        m_pickable = s.runners.isRunnable(spec.id);
        setCursor(m_pickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(12, 10, 12, 10);
        h->setSpacing(12);
        h->addWidget(runnerIcon(spec, 30), 0, Qt::AlignTop);
        auto* mid = new QVBoxLayout;
        mid->setSpacing(4);
        auto* title = new QHBoxLayout;
        title->setSpacing(8);
        title->addWidget(new Label(spec.name, Label::Style::SmallStrong));
        if (isDefault)
            title->addWidget(new Label(QStringLiteral("Default"), Label::Style::Caption, Theme::Role::Text3));
        if (Badge* b = RunnerUi::vendorBadge(spec))
            title->addWidget(b);
        title->addStretch(1);
        mid->addLayout(title);
        if (recommended) {
            auto* rec = new Badge(QStringLiteral("Recommended for your GPU"), Theme::Tone::Accent);
            rec->setIcon(QStringLiteral("check"));
            auto* rl = new QHBoxLayout;
            rl->addWidget(rec);
            rl->addStretch(1);
            mid->addLayout(rl);
        }
        auto* line = new Label(pickerLine(s, spec), Label::Style::Caption, Theme::Role::Text2);
        line->setWordWrap(true);
        mid->addWidget(line);
        h->addLayout(mid, 1);
        if (chosen)
            h->addWidget(new IconView(QStringLiteral("check"), Theme::Role::Accent, 14), 0, Qt::AlignVCenter);
        if (st.state == S::NotInstalled || st.state == S::Failed) {
            auto* install = new Button(st.state == S::Failed ? QStringLiteral("Try again") : QStringLiteral("Install"), Button::Variant::Secondary,
                                       Button::Size::Tiny, QStringLiteral("download"));
            install->setObjectName(QStringLiteral("runsWithInstall-") + spec.id);
            QObject::connect(install, &Button::clicked, this, std::move(onInstall));
            h->addWidget(install, 0, Qt::AlignVCenter);
        }
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_chosen && !(m_hover && m_pickable))
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_chosen ? Theme::color(Theme::Role::AccentSoft) : Theme::color(Theme::Role::Hover));
        p.drawRoundedRect(QRectF(rect()), Metrics::RadiusLg, Metrics::RadiusLg);
    }
    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_pickable && rect().contains(e->position().toPoint()))
            m_onPick();
    }

private:
    bool m_chosen;
    bool m_pickable = false;
    bool m_hover = false;
    std::function<void()> m_onPick;
};

// The picker: a card under the selector over a transparent layer that closes it on any click outside.
class RunsWithPopover : public QWidget {
public:
    RunsWithPopover(QWidget* window, QWidget* anchor)
        : QWidget(window)
        , m_anchor(anchor)
        , m_card(new Card(Card::Kind::Surface, Metrics::RadiusXl))
    {
        setObjectName(QStringLiteral("runsWithPicker"));
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::StrongFocus);
        m_card->setParent(this);
        m_card->setFixedWidth(460);
        window->installEventFilter(this);
    }
    Card* card() const { return m_card; }

    void open()
    {
        place();
        show();
        raise();
        setFocus();
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == parentWidget() && e->type() == QEvent::Resize)
            place();
        return QWidget::eventFilter(o, e);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (!m_card->geometry().contains(e->position().toPoint()))
            close();
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape)
            close();
        else
            QWidget::keyPressEvent(e);
    }

private:
    void place()
    {
        setGeometry(parentWidget()->rect());
        m_card->adjustSize();
        const QPoint a = m_anchor->mapTo(parentWidget(), QPoint(m_anchor->width(), m_anchor->height()));
        const int x = qBound(8, a.x() - m_card->width(), width() - m_card->width() - 8);
        m_card->move(x, a.y() + 6);
    }

    QWidget* m_anchor;
    Card* m_card;
};

}

RunsWithSelector::RunsWithSelector(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
{
    setObjectName(QStringLiteral("runsWith"));
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(&m_s.settings, &AppSettings::changed, this, [this] { updateGeometry(); update(); });
    connect(&m_s.runners, &RunnerManager::changed, this, [this] { update(); });
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void RunsWithSelector::setGame(const Game& game)
{
    m_game = game;
    updateGeometry();
    update();
}

QString RunsWithSelector::runnerId() const
{
    const QString id = m_s.settings.runnerFor(m_game.titleId());
    return Runners::find(id) ? id : QString::fromLatin1(Runners::XSEmu);
}

QString RunsWithSelector::trailing() const
{
    return runnerId() == m_s.settings.defaultRunner() ? QStringLiteral("Default") : QString();
}

QSize RunsWithSelector::sizeHint() const
{
    const RunnerSpec* spec = Runners::find(runnerId());
    const int label = QFontMetrics(Theme::font(13)).horizontalAdvance(QStringLiteral("Runs with"));
    const int name = QFontMetrics(Theme::font(13, 600)).horizontalAdvance(spec ? spec->name : QString());
    const QString tail = trailing();
    const int tailW = tail.isEmpty() ? 0 : 6 + QFontMetrics(Theme::font(12)).horizontalAdvance(tail);
    return {label + 10 + 8 + 20 + 8 + name + tailW + 8 + 12 + 10, 36};
}

void RunsWithSelector::paintEvent(QPaintEvent*)
{
    const RunnerSpec* spec = Runners::find(runnerId());
    if (!spec)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(Theme::font(13));
    p.setPen(Theme::color(Theme::Role::Text3));
    const int label = p.fontMetrics().horizontalAdvance(QStringLiteral("Runs with"));
    p.drawText(QRect(0, 0, label, height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Runs with"));

    const QRectF box(label + 10.5, 0.5, width() - label - 11, height() - 1);
    p.setPen(QPen(Theme::color(m_hover ? Theme::Role::BorderStrong : Theme::Role::Border), 1));
    p.setBrush(Theme::color(Theme::Role::Surface));
    p.drawRoundedRect(box, Metrics::RadiusLg, Metrics::RadiusLg);

    int x = int(box.left()) + 8;
    const QRect icon(x, (height() - 20) / 2, 20, 20);
    if (spec->isBuiltIn()) {
        p.drawPixmap(icon, Icon::appMark(20, devicePixelRatioF()));
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color(Theme::Role::Sunken));
        p.drawRoundedRect(QRectF(icon), 5, 5);
        Icon::paint(p, QRectF(icon), QStringLiteral("runner"), Theme::color(Theme::Role::Text2), 12);
    }
    x += 28;
    p.setFont(Theme::font(13, 600));
    p.setPen(Theme::color(Theme::Role::Text));
    const int name = p.fontMetrics().horizontalAdvance(spec->name);
    p.drawText(QRect(x, 0, name + 1, height()), Qt::AlignVCenter | Qt::AlignLeft, spec->name);
    x += name;
    const QString tail = trailing();
    if (!tail.isEmpty()) {
        x += 6;
        p.setFont(Theme::font(12));
        p.setPen(Theme::color(Theme::Role::Text3));
        const int w = p.fontMetrics().horizontalAdvance(tail);
        p.drawText(QRect(x, 0, w + 1, height()), Qt::AlignVCenter | Qt::AlignLeft, tail);
    }
    Icon::paint(p, QRectF(box.right() - 22, (height() - 12) / 2.0, 12, 12), QStringLiteral("chevron-down"), Theme::color(Theme::Role::Text3), 12);
}

void RunsWithSelector::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()))
        openPicker();
}

void RunsWithSelector::enterEvent(QEnterEvent*)
{
    m_hover = true;
    update();
}

void RunsWithSelector::leaveEvent(QEvent*)
{
    m_hover = false;
    update();
}

void RunsWithSelector::openPicker()
{
    if (!m_game.isValid())
        return;
    QWidget* win = window();
    auto* pop = new RunsWithPopover(win, this);
    auto* v = new QVBoxLayout(pop->card());
    v->setContentsMargins(6, 14, 6, 10);
    v->setSpacing(2);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(12, 0, 12, 6);
    head->addWidget(new Label(QStringLiteral("Runs with"), Label::Style::Caps, Theme::Role::Text3));
    head->addStretch(1);
    head->addWidget(new IconView(QStringLiteral("chip"), Theme::Role::Text3, 12));
    head->addWidget(new Label(QStringLiteral("Your GPU: %1").arg(m_s.hardware.gpuLabel()), Label::Style::Caption, Theme::Role::Text3));
    v->addLayout(head);

    const QString chosen = runnerId();
    const QString def = m_s.settings.defaultRunner();
    const QString recommended = RunnerUi::recommendation(m_s).runnerId;
    const QString titleId = m_game.titleId();
    for (const RunnerSpec* runner : RunnerUi::displayOrder(m_s)) {
        const RunnerSpec& spec = *runner;
        const QString id = spec.id;
        auto pick = [this, pop, id, titleId, def] {
            if (id == def)
                m_s.settings.clearGameOverride(titleId, QStringLiteral("runner"));
            else
                m_s.settings.setGameOverride(titleId, QStringLiteral("runner"), id);
            pop->close();
        };
        auto install = [this, pop, win, id] {
            pop->close();
            RunnerUi::confirmInstall(win, m_s, id);
        };
        v->addWidget(new RunnerRow(m_s, spec, id == chosen, id == def, !spec.isBuiltIn() && id == recommended, pick, install));
    }

    auto* rule = new QFrame;
    rule->setFixedHeight(1);
    rule->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::color(Theme::Role::Border).name(QColor::HexArgb)));
    v->addSpacing(6);
    v->addWidget(rule);
    auto* foot = new QHBoxLayout;
    foot->setContentsMargins(12, 6, 6, 0);
    foot->addWidget(new Label(QStringLiteral("Applies to %1 only.").arg(m_game.title()), Label::Style::Caption, Theme::Role::Text3), 1);
    auto* manage = new Button(QStringLiteral("Manage runners"), Button::Variant::Link, Button::Size::Tiny);
    manage->setObjectName(QStringLiteral("manageRunners"));
    connect(manage, &Button::clicked, this, [this, pop] {
        pop->close();
        emit manageRunners();
    });
    foot->addWidget(manage);
    v->addLayout(foot);
    // an install that starts or ends elsewhere while the picker is open: show the rows as they are now
    connect(&m_s.runners, &RunnerManager::stateChanged, pop, [this, pop] {
        pop->close();
        QTimer::singleShot(0, this, &RunsWithSelector::openPicker);
    });
    pop->open();
}
