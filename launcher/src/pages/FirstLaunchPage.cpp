#include "FirstLaunchPage.h"

#include "app/Services.h"
#include "core/Format.h"
#include "core/HardwareInfo.h"
#include "theme/Theme.h"
#include "widgets/Button.h"
#include "widgets/ChoiceCard.h"
#include "widgets/Icon.h"
#include "widgets/InfoNote.h"
#include "widgets/Text.h"

#include <QButtonGroup>
#include <QDir>
#include <QHBoxLayout>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>

class StepIndicator : public QWidget {
public:
    StepIndicator()
    {
        setFixedHeight(24);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    void setStep(int step)
    {
        m_step = step;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Palette& c = Theme::instance().palette();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QStringList names = {QStringLiteral("Console"), QStringLiteral("Network"), QStringLiteral("Game folders")};
        qreal x = 0;
        for (int i = 0; i < names.size(); ++i) {
            const bool current = i == m_step;
            const bool done = i < m_step;
            const QRectF dot(x + 1, 1, 22, 22);
            p.setPen(QPen(current || done ? c.accent : c.borderStrong, current ? 1.5 : 1));
            p.setBrush(done ? c.accent : c.surface);
            p.drawEllipse(dot);
            if (done) {
                Icon::paint(p, dot, QStringLiteral("check"), c.onAccent, 12);
            } else {
                p.setFont(Theme::font(Metrics::TextXs, 500));
                p.setPen(current ? c.accentText : c.text3);
                p.drawText(dot, Qt::AlignCenter, QString::number(i + 1));
            }
            x += 32;
            const QFont f = Theme::font(Metrics::TextSm, current ? 600 : 400);
            p.setFont(f);
            p.setPen(current ? c.text : c.text3);
            const int w = QFontMetrics(f).horizontalAdvance(names[i]);
            p.drawText(QRectF(x, 0, w, 24), Qt::AlignLeft | Qt::AlignVCenter, names[i]);
            x += w + 14;
            if (i + 1 < names.size()) {
                p.fillRect(QRectF(x, 12, 30, 1), c.borderStrong);
                x += 44;
            }
        }
    }

private:
    int m_step = 0;
};

namespace {

QWidget* stepPage(int index, const QString& question, const QString& body, QVBoxLayout** content)
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(new Label(QStringLiteral("Step %1 of 3").arg(index + 1), Label::Style::CapsMedium, Theme::Role::Text3));
    v->addSpacing(12);
    v->addWidget(new Label(question, Label::Style::Display));
    v->addSpacing(12);
    auto* b = new Label(body, Label::Style::Body, Theme::Role::Text2);
    b->setWordWrap(true);
    v->addWidget(b);
    v->addSpacing(24);
    auto* c = new QVBoxLayout;
    c->setSpacing(12);
    v->addLayout(c);
    v->addStretch(1);
    *content = c;
    return page;
}

}

FirstLaunchPage::FirstLaunchPage(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
    , m_steps(new StepIndicator)
    , m_stack(new QStackedWidget)
    , m_back(new Button(QStringLiteral("Back"), Button::Variant::Ghost, Button::Size::Large))
    , m_next(new Button(QStringLiteral("Continue"), Button::Variant::Primary, Button::Size::Large))
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* column = new QWidget;
    column->setFixedWidth(640);
    auto* cv = new QVBoxLayout(column);
    cv->setContentsMargins(0, 52, 0, 24);
    cv->setSpacing(0);
    auto* brand = new QHBoxLayout;
    brand->setSpacing(10);
    brand->addWidget(new BrandMark(28));
    auto* name = new Label(QStringLiteral("XSEmu"), Label::Style::SectionTitle);
    brand->addWidget(name);
    brand->addStretch(1);
    cv->addLayout(brand);
    cv->addSpacing(34);
    cv->addWidget(m_steps);
    cv->addSpacing(34);
    m_stack->addWidget(buildConsole());
    m_stack->addWidget(buildNetwork());
    m_stack->addWidget(buildFolders());
    cv->addWidget(m_stack, 1);

    auto* center = new QHBoxLayout;
    center->addStretch(1);
    center->addWidget(column);
    center->addStretch(1);
    v->addLayout(center, 1);

    auto* footer = new QWidget;
    footer->setObjectName(QStringLiteral("firstLaunchFooter"));
    footer->setFixedHeight(68);
    auto* fh = new QHBoxLayout(footer);
    fh->setContentsMargins(0, 0, 0, 0);
    auto* fcol = new QWidget;
    fcol->setFixedWidth(640);
    auto* fch = new QHBoxLayout(fcol);
    fch->setContentsMargins(0, 0, 0, 0);
    fch->setSpacing(8);
    fch->addWidget(new Label(QStringLiteral("XSEmu launcher %1").arg(QStringLiteral(XSEMU_VERSION)), Label::Style::Small, Theme::Role::Text3));
    fch->addStretch(1);
    fch->addWidget(m_back);
    fch->addWidget(m_next);
    fh->addStretch(1);
    fh->addWidget(fcol);
    fh->addStretch(1);
    v->addWidget(footer);

    connect(m_back, &Button::clicked, this, [this] { setStep(m_step - 1); });
    connect(m_next, &Button::clicked, this, [this] {
        if (m_step == 0)
            m_s.settings.setDefaultDeviceType(m_seriesS->isChecked() ? QStringLiteral("lockhart") : QStringLiteral("anaconda"));
        if (m_step == 1)
            m_s.settings.setOffline(m_offline->isChecked());
        if (m_step < 2) {
            setStep(m_step + 1);
            return;
        }
        m_s.settings.setFirstLaunchDone(true);
        emit finished();
    });
    connect(&m_s.settings, &AppSettings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::GameFolders))
            refreshFolders();
    });
    connect(&m_s.library, &Library::gamesChanged, this, &FirstLaunchPage::refreshFolders);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    setStep(0);
}

void FirstLaunchPage::setStep(int step)
{
    m_step = qBound(0, step, 2);
    m_steps->setStep(m_step);
    m_stack->setCurrentIndex(m_step);
    m_back->setVisible(m_step > 0);
    m_next->setText(m_step == 2 ? QStringLiteral("Open library") : QStringLiteral("Continue"));
}

QWidget* FirstLaunchPage::buildConsole()
{
    QVBoxLayout* c = nullptr;
    QWidget* page = stepPage(0, QStringLiteral("Which console should XSEmu be?"),
                             QStringLiteral("Games pick their memory budget and resolution from the console model. You can change this later in Settings \u2192 Console."), &c);
    m_seriesS = new ChoiceCard(QStringLiteral("Series S"), QStringLiteral("8 GB title memory · lower resolution targets"),
                               QStringLiteral("Fits a 32 GB PC and leaves 6 GB free while a game runs."), QStringLiteral("Recommended"));
    auto* seriesX = new ChoiceCard(QStringLiteral("Series X"), QStringLiteral("13.5 GB title memory · higher targets"),
                                   QStringLiteral("Needs much more RAM and VRAM. Pick it only on a 64 GB PC."));
    auto* group = new QButtonGroup(page);
    group->addButton(m_seriesS);
    group->addButton(seriesX);
    (m_s.settings.defaultDeviceType() == QLatin1String("anaconda") ? seriesX : m_seriesS)->setChecked(true);
    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    row->addWidget(m_seriesS);
    row->addWidget(seriesX);
    c->addLayout(row);
    const HardwareInfo hw = HardwareInfo::read();
    QString gpu = hw.gpuNameOrUnknown();
    if (hw.dedicatedVramBytes > 0)
        gpu += QStringLiteral(" with %1").arg(Format::gigabytes(qint64(hw.dedicatedVramBytes), 0));
    c->addWidget(new InfoNote(QStringLiteral("This PC has %1 of RAM and %2. Series S is the safe choice.").arg(Format::gigabytes(qint64(hw.totalRamBytes), 0), gpu)));
    return page;
}

QWidget* FirstLaunchPage::buildNetwork()
{
    QVBoxLayout* c = nullptr;
    QWidget* page = stepPage(1, QStringLiteral("Should games go online?"),
                             QStringLiteral("XSEmu has no Xbox network sign-in. Offline is the honest setting: store and online checks fail the way they do on a console without a connection."), &c);
    m_offline = new ChoiceCard(QStringLiteral("Offline"), QStringLiteral("No network for games · store queries report no connection"), {}, QStringLiteral("Recommended"));
    auto* online = new ChoiceCard(QStringLiteral("Online"), QStringLiteral("Games may reach the internet"), QStringLiteral("Online features still need Xbox services XSEmu doesn't provide."));
    auto* group = new QButtonGroup(page);
    group->addButton(m_offline);
    group->addButton(online);
    (m_s.settings.offline() ? m_offline : online)->setChecked(true);
    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    row->addWidget(m_offline);
    row->addWidget(online);
    c->addLayout(row);
    return page;
}

QWidget* FirstLaunchPage::buildFolders()
{
    QVBoxLayout* c = nullptr;
    QWidget* page = stepPage(2, QStringLiteral("Where are your games?"),
                             QStringLiteral("Pick the folder that holds your unpacked games. XSEmu finds every game with a MicrosoftGame.config up to three folders deep."), &c);
    auto* list = new QWidget;
    m_folders = new QVBoxLayout(list);
    m_folders->setContentsMargins(0, 0, 0, 0);
    m_folders->setSpacing(6);
    c->addWidget(list);
    auto* row = new QHBoxLayout;
    auto* add = new Button(QStringLiteral("Add folder"), Button::Variant::Secondary, Button::Size::Large, QStringLiteral("folder"));
    row->addWidget(add);
    row->addSpacing(8);
    m_found = new Label({}, Label::Style::Small, Theme::Role::Text2);
    row->addWidget(m_found);
    row->addStretch(1);
    c->addLayout(row);
    connect(add, &Button::clicked, this, &FirstLaunchPage::addGamesRequested);
    refreshFolders();
    return page;
}

void FirstLaunchPage::refreshFolders()
{
    if (!m_folders)
        return;
    while (QLayoutItem* item = m_folders->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const QString& folder : m_s.settings.gameFolders()) {
        auto* line = new QWidget;
        auto* h = new QHBoxLayout(line);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addWidget(new IconView(QStringLiteral("folder"), Theme::Role::Text3, 14));
        h->addWidget(new Label(QDir::toNativeSeparators(folder), Label::Style::Mono, Theme::Role::Text2), 1);
        m_folders->addWidget(line);
    }
    const qsizetype n = m_s.library.games().size();
    m_found->setText(m_s.settings.gameFolders().isEmpty() ? QString()
                     : n == 1                             ? QStringLiteral("1 game found")
                                                          : QStringLiteral("%1 games found").arg(n));
}

void FirstLaunchPage::paintEvent(QPaintEvent*)
{
    const Palette& c = Theme::instance().palette();
    QPainter p(this);
    p.fillRect(rect(), c.bg);
    const QRect footer(0, height() - 68, width(), 68);
    p.fillRect(footer, c.surface);
    p.fillRect(QRect(0, footer.top(), width(), 1), c.border);
}
