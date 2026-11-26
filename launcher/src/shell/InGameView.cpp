#include "InGameView.h"

#include "TitleBar.h"
#include "WindowButtons.h"
#include "app/Services.h"
#include "theme/Theme.h"
#include "widgets/Button.h"
#include "widgets/Icon.h"
#include "widgets/Spinner.h"
#include "widgets/Text.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace {

class Bar : public QWidget {
public:
    Bar()
    {
        setFixedHeight(Metrics::GameBar);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::color(Theme::Role::Surface));
        p.fillRect(QRect(0, height() - 1, width(), 1), Theme::color(Theme::Role::Border));
    }
    void mousePressEvent(QMouseEvent* e) override { WindowDrag::press(this, e); }
    void mouseDoubleClickEvent(QMouseEvent* e) override { WindowDrag::doubleClick(this, e); }
};

class Separator : public QWidget {
public:
    Separator() { setFixedSize(1, 20); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::color(Theme::Role::Border));
    }
};

class Host : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
    }
};

}

InGameView::InGameView(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
    , m_title(new Label({}, Label::Style::Title))
    , m_spinner(new Spinner(14))
    , m_status(new Label({}, Label::Style::Small, Theme::Role::Text2))
    , m_pause(new Button(QStringLiteral("Pause"), Button::Variant::Ghost, Button::Size::Small, QStringLiteral("pause")))
    , m_stop(new Button(QStringLiteral("Stop"), Button::Variant::Ghost, Button::Size::Small, QStringLiteral("stop")))
    , m_host(new Host)
    , m_hostTitle(new Label({}, Label::Style::SectionTitle, Theme::Role::HudText))
    , m_hostDetail(new Label({}, Label::Style::Small, Theme::Role::HudText2))
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* bar = new Bar;
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 0, 1);
    h->setSpacing(10);
    h->addWidget(new BrandMark(16));
    auto* back = new Button(QStringLiteral("Library"), Button::Variant::Ghost, Button::Size::Small, QStringLiteral("back"));
    h->addWidget(back);
    h->addWidget(new Separator);
    h->addWidget(m_title);
    h->addSpacing(6);
    h->addWidget(m_spinner);
    h->addWidget(m_status);
    h->addStretch(1);
    h->addWidget(m_pause);
    h->addWidget(m_stop);
    h->addWidget(new Separator);
    h->addWidget(new WindowButtons(Metrics::GameBar));
    v->addWidget(bar);

    auto* hv = new QVBoxLayout(m_host);
    hv->addStretch(1);
    hv->addWidget(m_hostTitle, 0, Qt::AlignHCenter);
    hv->addSpacing(4);
    hv->addWidget(m_hostDetail, 0, Qt::AlignHCenter);
    hv->addStretch(1);
    v->addWidget(m_host, 1);

    connect(back, &Button::clicked, this, &InGameView::backToLibrary);
    connect(m_pause, &Button::clicked, this, [this] {
        m_s.emulator.state() == EmulatorController::State::Paused ? m_s.emulator.resume() : m_s.emulator.pause();
    });
    connect(m_stop, &Button::clicked, &m_s.emulator, &EmulatorController::stop);
    connect(&m_s.emulator, &EmulatorController::stateChanged, this, &InGameView::refresh);
    connect(&m_s.settings, &AppSettings::changed, this, &InGameView::refresh);
    refresh();
}

void InGameView::refresh()
{
    const EmulatorController::State st = m_s.emulator.state();
    const QString title = m_s.emulator.game().title();
    m_title->setText(title);
    const QString where = QStringLiteral("%1 · %2").arg(Services::consoleName(m_s.settings.defaultDeviceType()),
                                                        m_s.settings.offline() ? QStringLiteral("Offline") : QStringLiteral("Online"));
    QString status;
    switch (st) {
    case EmulatorController::State::Booting: status = QStringLiteral("Starting"); break;
    case EmulatorController::State::Running: status = QStringLiteral("Playing"); break;
    case EmulatorController::State::Paused: status = QStringLiteral("Paused"); break;
    case EmulatorController::State::Stopping: status = QStringLiteral("Stopping"); break;
    case EmulatorController::State::Idle: break;
    }
    m_status->setText(status.isEmpty() ? QString() : status + QStringLiteral(" · ") + where);
    m_spinner->setVisible(st == EmulatorController::State::Booting || st == EmulatorController::State::Stopping);
    m_pause->setVisible(st == EmulatorController::State::Running || st == EmulatorController::State::Paused);
    m_pause->setText(st == EmulatorController::State::Paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    m_pause->setIconName(st == EmulatorController::State::Paused ? QStringLiteral("play") : QStringLiteral("pause"));
    m_stop->setText(st == EmulatorController::State::Booting ? QStringLiteral("Cancel") : QStringLiteral("Stop"));
    m_stop->setIconName(st == EmulatorController::State::Booting ? QStringLiteral("close") : QStringLiteral("stop"));
    m_stop->setEnabled(st != EmulatorController::State::Stopping && st != EmulatorController::State::Idle);

    const bool booting = st == EmulatorController::State::Booting;
    const bool paused = st == EmulatorController::State::Paused;
    m_hostTitle->setVisible(booting || paused);
    m_hostDetail->setVisible(booting || paused);
    m_hostTitle->setText(booting ? QStringLiteral("Starting %1").arg(title) : QStringLiteral("Paused"));
    m_hostDetail->setText(booting ? where : QStringLiteral("The game keeps its memory until you stop it."));
}
