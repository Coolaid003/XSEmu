#include "WarmupPage.h"

#include "PageScaffold.h"
#include "app/Services.h"
#include "core/Format.h"
#include "core/ShaderCacheInfo.h"
#include "widgets/Badge.h"
#include "widgets/Button.h"
#include "widgets/InfoNote.h"
#include "widgets/PageHeader.h"
#include "widgets/ProgressBlock.h"
#include "widgets/Text.h"

#include <QComboBox>
#include <QGridLayout>
#include <QPainter>
#include <QVBoxLayout>

namespace {

class Rule : public QWidget {
public:
    Rule() { setFixedHeight(1); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::color(Theme::Role::Border));
    }
};

QString phaseText(const WarmupController::Progress& pr)
{
    const int i = WarmupController::phaseIndex(pr.phase);
    if (i < 0)
        return WarmupController::phaseLabel(pr.phase);
    return QStringLiteral("%1 · phase %2 of %3").arg(WarmupController::phaseLabel(pr.phase)).arg(i + 1).arg(WarmupController::phases().size());
}

QList<ProgressBlock::Column> progressColumns(const WarmupController::Progress& pr)
{
    return {
        {QStringLiteral("Done"), QStringLiteral("%1 / %2").arg(Format::count(pr.done), Format::count(pr.total))},
        {QStringLiteral("Speed"), QStringLiteral("%1 keys/s").arg(Format::count(qRound64(pr.keysPerS)))},
        {QStringLiteral("Time left"), pr.etaS ? Format::duration(*pr.etaS) : QStringLiteral("Estimating")},
        {QStringLiteral("Free RAM"), pr.ramFreeMb ? Format::gigabytesFromMb(*pr.ramFreeMb) : QStringLiteral("Unknown")},
        {QStringLiteral("Giant compiles"), QStringLiteral("%1 of %2").arg(pr.giantsCompiling).arg(pr.giantsMax)},
    };
}

struct RunResult {
    QString text;
    Theme::Tone tone;
};

RunResult runResult(const WarmupRun& run)
{
    switch (run.result) {
    case WarmupRun::Result::Finished: return {QStringLiteral("Finished"), Theme::Tone::Success};
    case WarmupRun::Result::Cancelled: return {QStringLiteral("Cancelled"), Theme::Tone::Neutral};
    case WarmupRun::Result::StoppedPartWay:
        return {run.stoppedForRam ? QStringLiteral("Stopped · free RAM below 1 GB") : QStringLiteral("Stopped part way"), Theme::Tone::Warning};
    case WarmupRun::Result::Failed: return {QStringLiteral("Failed · exit code %1").arg(Format::exitCode(run.exitCode)), Theme::Tone::Danger};
    }
    return {};
}

}

WarmupPage::WarmupPage(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
    , m_game(new QComboBox)
    , m_start(new Button(QStringLiteral("Warm up shaders"), Button::Variant::Primary, Button::Size::Large, QStringLiteral("bolt")))
    , m_note(new InfoNote)
    , m_block(new ProgressBlock)
    , m_runs(nullptr)
    , m_runsHost(new QWidget)
{
    const PageScaffold page = makePageScaffold(this, QStringLiteral("Shader warm-up"));
    page.header->setSubtitle(QStringLiteral("Pre-builds every pipeline so the first run doesn't stall"));
    m_game->setMinimumWidth(188);
    m_game->setFixedHeight(Metrics::PageAction);
    page.header->addAction(m_game);
    page.header->addAction(m_start);

    page.content->addWidget(m_note);
    page.content->addWidget(m_block);
    page.content->addWidget(makeSectionLabel(QStringLiteral("Recent runs"), new Label(QStringLiteral("Kept in the launcher's settings"), Label::Style::Caption, Theme::Role::Text3)));
    m_runs = new QGridLayout(m_runsHost);
    m_runs->setContentsMargins(0, 0, 0, 0);
    m_runs->setHorizontalSpacing(24);
    m_runs->setVerticalSpacing(0);
    page.content->addWidget(m_runsHost);
    page.content->addStretch(1);

    connect(m_start, &Button::clicked, this, [this] {
        QString error;
        if (!m_s.warmup.start(false, m_s.library.game(m_game->currentData().toString()), &error))
            m_note->setText(error);
    });
    connect(m_block, &ProgressBlock::actionClicked, &m_s.warmup, &WarmupController::cancel);
    connect(&m_s.warmup, &WarmupController::stateChanged, this, &WarmupPage::refresh);
    connect(&m_s.warmup, &WarmupController::progressChanged, this, &WarmupPage::refresh);
    connect(&m_s.gate, &ProcessGate::changed, this, &WarmupPage::refresh);
    connect(&m_s.library, &Library::gamesChanged, this, &WarmupPage::refreshGames);
    connect(&m_s.settings, &AppSettings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::RecentWarmups))
            refreshRuns();
    });
    refreshGames();
    refresh();
    refreshRuns();
}

void WarmupPage::refreshGames()
{
    const QString current = m_game->currentData().toString();
    m_game->clear();
    for (const Game& g : m_s.library.games())
        m_game->addItem(g.title(), g.titleId());
    const int i = m_game->findData(current);
    if (i >= 0)
        m_game->setCurrentIndex(i);
    m_game->setVisible(m_game->count() > 0);
}

void WarmupPage::refresh()
{
    const WarmupController::State st = m_s.warmup.state();
    const WarmupController::Progress pr = m_s.warmup.progress();
    const QString blocked = m_s.gate.blockedReason(ProcessGate::Action::StartWarmup);
    const QString cannot = blocked.isEmpty() ? m_s.warmup.checkStart() : blocked;
    const bool active = m_s.warmup.isActive();
    m_start->setEnabled(cannot.isEmpty() && !active);
    m_start->setText(active ? QStringLiteral("Warming up...") : QStringLiteral("Warm up shaders"));
    m_start->setToolTip(cannot);
    m_game->setEnabled(!active);
    if (active) {
        const int i = m_game->findData(m_s.warmup.game().titleId());
        if (i >= 0)
            m_game->setCurrentIndex(i);
    }

    if (m_s.gate.owner() == ProcessGate::Owner::Game) {
        m_note->setTone(Theme::Tone::Warning);
        m_note->setText(blocked);
        m_note->show();
    } else if (st == WarmupController::State::Idle) {
        const ShaderCacheInfo cache = ShaderCacheInfo::readDefault();
        m_note->setTone(Theme::Tone::Neutral);
        m_note->setText(cache.pipelines > 0
                            ? QStringLiteral("XSEmu has recorded %1 pipelines across your games. A warm-up builds every one of them ahead of time, so games don't stall the first time they need one.").arg(Format::count(cache.pipelines))
                            : QStringLiteral("XSEmu records each pipeline a game builds. Play a game once, then a warm-up builds every recorded pipeline ahead of time so later runs don't stall."));
        m_note->show();
    } else {
        m_note->hide();
    }

    const QString game = m_s.warmup.game().title();
    m_block->setVisible(st != WarmupController::State::Idle);
    switch (st) {
    case WarmupController::State::Idle:
        break;
    case WarmupController::State::Running:
        m_block->setState(ProgressBlock::State::Running);
        m_block->setTitle(QStringLiteral("Warming up shaders"));
        m_block->setSubtitle(game.isEmpty() ? phaseText(pr) : game + QStringLiteral(" · ") + phaseText(pr));
        m_block->setFraction(pr.phaseFraction());
        m_block->setColumns(progressColumns(pr));
        m_block->setActionText(m_s.warmup.cancelRequested() ? QString() : QStringLiteral("Cancel"));
        break;
    case WarmupController::State::PausedForRam:
        m_block->setState(ProgressBlock::State::Paused);
        m_block->setTitle(QStringLiteral("Waiting for free RAM"));
        m_block->setSubtitle(QStringLiteral("%1 · continues when more than 1 GB is free").arg(phaseText(pr)));
        m_block->setFraction(pr.phaseFraction());
        m_block->setColumns(progressColumns(pr));
        m_block->setActionText(QStringLiteral("Cancel"));
        break;
    case WarmupController::State::Finished:
    case WarmupController::State::Cancelled:
    case WarmupController::State::StoppedPartWay:
    case WarmupController::State::Failed: {
        WarmupRun fromProgress;
        fromProgress.done = pr.done;
        fromProgress.total = pr.total;
        fromProgress.failed = pr.failed;
        fromProgress.tookS = pr.elapsedS;
        const WarmupRun run = m_s.warmup.lastRun().value_or(fromProgress);
        const ProgressBlock::State bs = st == WarmupController::State::Finished ? ProgressBlock::State::Finished
                                      : st == WarmupController::State::Failed   ? ProgressBlock::State::Failed
                                      : st == WarmupController::State::Cancelled ? ProgressBlock::State::Cancelled
                                                                                 : ProgressBlock::State::Paused;
        m_block->setState(bs);
        const QString title = st == WarmupController::State::Finished ? QStringLiteral("Shaders warmed up")
                            : st == WarmupController::State::Failed   ? QStringLiteral("Warm-up failed")
                            : st == WarmupController::State::Cancelled ? QStringLiteral("Warm-up cancelled")
                                                                       : QStringLiteral("Warm-up stopped part way");
        m_block->setTitle(title);
        const QString message = st == WarmupController::State::Failed ? m_s.warmup.errorMessage() : QString();
        m_block->setSubtitle(message.isEmpty() ? game : game.isEmpty() ? message : game + QStringLiteral(" · ") + message);
        m_block->setFraction(run.total > 0 ? double(run.done) / double(run.total) : -1);
        m_block->setColumns({
            {QStringLiteral("Built"), QStringLiteral("%1 / %2").arg(Format::count(run.done), Format::count(run.total))},
            {QStringLiteral("Failed"), Format::count(run.failed)},
            {QStringLiteral("Took"), Format::shortDuration(run.tookS)},
        });
        m_block->setActionText({});
        break;
    }
    }
}

void WarmupPage::refreshRuns()
{
    while (QLayoutItem* item = m_runs->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<WarmupRun> runs = m_s.settings.recentWarmups();
    if (runs.isEmpty()) {
        m_runs->addWidget(new Label(QStringLiteral("No warm-ups yet."), Label::Style::Small, Theme::Role::Text3), 0, 0);
        return;
    }
    const QStringList heads = {QStringLiteral("Game"), QStringLiteral("Pipelines"), QStringLiteral("Took"), QStringLiteral("Result"), QStringLiteral("When")};
    for (int c = 0; c < heads.size(); ++c) {
        auto* h = new Label(heads[c], Label::Style::Caption, Theme::Role::Text3);
        h->setFixedHeight(28);
        if (c == 1 || c == 2 || c == 4)
            h->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(h, 0, c);
    }
    m_runs->addWidget(new Rule, 1, 0, 1, heads.size());
    int row = 2;
    for (const WarmupRun& run : runs) {
        auto* name = new Label(run.titleName.isEmpty() ? QStringLiteral("All games") : run.titleName, Label::Style::BodyMedium);
        name->setFixedHeight(47);
        m_runs->addWidget(name, row, 0);
        auto* count = new Label(QStringLiteral("%1 / %2").arg(Format::count(run.done), Format::count(run.total)), Label::Style::Number, Theme::Role::Text2);
        count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(count, row, 1);
        auto* took = new Label(Format::shortDuration(run.tookS), Label::Style::Number, Theme::Role::Text2);
        took->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(took, row, 2);
        const RunResult rr = runResult(run);
        m_runs->addWidget(new Badge(rr.text, rr.tone, Badge::Kind::Plain), row, 3, Qt::AlignLeft | Qt::AlignVCenter);
        auto* when = new Label(Format::ago(run.finishedAt), Label::Style::Small, Theme::Role::Text3);
        when->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(when, row, 4);
        m_runs->addWidget(new Rule, row + 1, 0, 1, heads.size());
        row += 2;
    }
    m_runs->setColumnStretch(0, 1);
}
