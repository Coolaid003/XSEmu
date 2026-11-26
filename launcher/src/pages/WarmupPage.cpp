#include "WarmupPage.h"

#include "PageScaffold.h"
#include "app/Services.h"
#include "core/Format.h"
#include "core/ShaderCacheInfo.h"
#include "settings/SettingsKit.h"
#include "shell/MainWindow.h"
#include "widgets/Badge.h"
#include "widgets/Button.h"
#include "widgets/CoverThumb.h"
#include "widgets/Icon.h"
#include "widgets/PageHeader.h"
#include "widgets/ProgressBlock.h"
#include "widgets/Text.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>
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

// The amber banner while a game holds the one XSEmu slot (Warm-up: blocked).
class Banner : public QWidget {
public:
    Banner() { connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update)); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 12, 12);
        p.fillPath(path, Theme::toneSoft(Theme::Tone::Warning));
        const QRectF disc(20, (height() - 22) / 2.0, 22, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::withAlpha(Theme::color(Theme::Role::Warning), 0.14));
        p.drawEllipse(disc);
        Icon::paint(p, disc, QStringLiteral("lock"), Theme::color(Theme::Role::Warning), 12);
    }
};

QString phaseText(const WarmupController::Progress& pr)
{
    const int i = WarmupController::phaseIndex(pr.phase);
    if (i < 0)
        return WarmupController::phaseLabel(pr.phase);
    return QStringLiteral("%1 · phase %2 of %3").arg(WarmupController::phaseLabel(pr.phase)).arg(i + 1).arg(WarmupController::phases().size());
}

QString ramText(qint64 mb)
{
    return mb < 1024 ? QStringLiteral("%1 MB").arg(mb) : Format::gigabytesFromMb(mb);
}

// The emulator's message as a sentence: capital first letter, full stop.
QString sentence(QString s)
{
    s = s.trimmed();
    if (s.isEmpty())
        return s;
    s[0] = s[0].toUpper();
    if (!s.endsWith(QLatin1Char('.')))
        s += QLatin1Char('.');
    return s;
}

QString giants(int compiling, int max)
{
    return max > 0 ? QStringLiteral("%1 of %2").arg(compiling).arg(max) : QString::number(compiling);
}

// "27,292 / 41,906", or just the count when the pass reported no comparable total.
QString builtOf(qint64 done, qint64 total)
{
    return total > 0 && done <= total ? QStringLiteral("%1 / %2").arg(Format::count(done), Format::count(total)) : Format::count(done);
}

double fractionOf(qint64 done, qint64 total)
{
    return total > 0 ? qMin(1.0, double(done) / double(total)) : 0.0;
}

struct RunResult {
    QString text;
    Theme::Tone tone;
};

Theme::Role toneText(Theme::Tone tone)
{
    switch (tone) {
    case Theme::Tone::Neutral: return Theme::Role::Text2;
    case Theme::Tone::Accent: return Theme::Role::AccentText;
    case Theme::Tone::Success: return Theme::Role::Success;
    case Theme::Tone::Warning: return Theme::Role::Warning;
    case Theme::Tone::Danger: return Theme::Role::Danger;
    }
    return Theme::Role::Text2;
}

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

QString stateSentence(const QString& name, TitleShaderState st)
{
    switch (st) {
    case TitleShaderState::Ready: return QStringLiteral("%1 is ready").arg(name);
    case TitleShaderState::NeverWarmed: return QStringLiteral("%1 has never been warmed up").arg(name);
    case TitleShaderState::LastRunIncomplete: return QStringLiteral("%1's last warm-up stopped part way").arg(name);
    case TitleShaderState::LastRunFailed: return QStringLiteral("%1's last warm-up failed").arg(name);
    case TitleShaderState::Unknown: break;
    }
    return {};
}

QString joinSentences(const QStringList& parts)
{
    if (parts.isEmpty())
        return {};
    if (parts.size() == 1)
        return parts.first() + QLatin1Char('.');
    return QStringList(parts.mid(0, parts.size() - 1)).join(QStringLiteral(", ")) + QStringLiteral(", and ") + parts.last() + QLatin1Char('.');
}

}

WarmupPage::WarmupPage(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
    , m_game(SettingsKit::dropdown({}, {}, 200))
    , m_start(new Button(QStringLiteral("Start warm-up"), Button::Variant::Primary, Button::Size::Large, QStringLiteral("bolt")))
    , m_banner(new Banner)
    , m_bannerTitle(new Label({}, Label::Style::Title))
    , m_bannerText(new Label({}, Label::Style::Small, Theme::Role::Text2))
    , m_stopGame(new Button(QStringLiteral("Stop game"), Button::Variant::DangerSecondary, Button::Size::Small, QStringLiteral("stop")))
    , m_block(new ProgressBlock)
    , m_play(new Button({}, Button::Variant::Primary, Button::Size::Small, QStringLiteral("play")))
    , m_warm(new Button({}, Button::Variant::Primary, Button::Size::Small, QStringLiteral("bolt")))
    , m_resume(new Button({}, Button::Variant::Secondary, Button::Size::Small, QStringLiteral("bolt")))
    , m_log(new Button(QStringLiteral("Open log"), Button::Variant::Secondary, Button::Size::Small, QStringLiteral("file")))
    , m_dismiss(new Button(QStringLiteral("Dismiss"), Button::Variant::Ghost, Button::Size::Small))
    , m_cancel(new Button(QStringLiteral("Cancel"), Button::Variant::Ghost, Button::Size::Small))
    , m_runs(nullptr)
{
    const PageScaffold page = makePageScaffold(this, QStringLiteral("Shader warm-up"));
    page.header->setSubtitle(QStringLiteral("Pre-builds every pipeline so the first run doesn't stall"));
    m_game->setFixedHeight(Metrics::PageAction);
    page.header->addAction(m_game);
    page.header->addAction(m_start);

    auto* bh = new QHBoxLayout(m_banner);
    bh->setContentsMargins(58, 16, 20, 16);
    bh->setSpacing(16);
    auto* bt = new QVBoxLayout;
    bt->setSpacing(4);
    bt->addWidget(m_bannerTitle);
    m_bannerText->setWordWrap(true);
    bt->addWidget(m_bannerText);
    bh->addLayout(bt, 1);
    bh->addWidget(m_stopGame, 0, Qt::AlignVCenter);
    page.content->addWidget(m_banner);

    m_block->setActionText({});
    for (Button* b : {m_warm, m_play, m_resume, m_log, m_dismiss, m_cancel})
        m_block->addHeaderWidget(b);
    page.content->addWidget(m_block);

    auto* runsHead = makeSectionLabel(QStringLiteral("Recent runs"), new Label(QStringLiteral("The last 20, kept by the launcher"), Label::Style::Caption, Theme::Role::Text3));
    page.content->addSpacing(4);
    page.content->addWidget(runsHead);
    auto* runsHost = new QWidget;
    m_runs = new QGridLayout(runsHost);
    m_runs->setContentsMargins(0, 0, 0, 0);
    m_runs->setHorizontalSpacing(24);
    m_runs->setVerticalSpacing(0);
    page.content->addWidget(runsHost);
    page.content->addStretch(1);

    connect(m_start, &Button::clicked, this, [this] { start(false); });
    connect(m_warm, &Button::clicked, this, [this] { start(false); });
    connect(m_resume, &Button::clicked, this, [this] {
        // stopped part way: the full pass builds what the budgeted one left; otherwise run the same pass again
        const bool full = m_s.warmup.state() == WarmupController::State::StoppedPartWay || m_s.warmup.progress().full;
        start(full);
    });
    connect(m_play, &Button::clicked, this, [this] {
        if (auto* w = qobject_cast<MainWindow*>(window()))
            w->play(m_s.warmup.game());
    });
    connect(m_log, &Button::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(WarmupController::logPath())); });
    connect(m_dismiss, &Button::clicked, this, [this] {
        m_dismissed = true;
        refresh();
    });
    connect(m_cancel, &Button::clicked, &m_s.warmup, &WarmupController::cancel);
    connect(m_stopGame, &Button::clicked, &m_s.emulator, &EmulatorController::stop);
    connect(m_game, &QComboBox::activated, this, [this] {
        if (!m_s.warmup.isActive())
            m_dismissed = true;
        refresh();
    });
    connect(&m_s.warmup, &WarmupController::stateChanged, this, [this](WarmupController::State st) {
        if (st == WarmupController::State::Running)
            m_dismissed = false;
        refresh();
        refreshRuns();
    });
    connect(&m_s.warmup, &WarmupController::progressChanged, this, [this] {
        refresh();
        refreshRuns();
    });
    connect(&m_s.gate, &ProcessGate::changed, this, &WarmupPage::refresh);
    connect(&m_s.emulator, &EmulatorController::statsChanged, this, &WarmupPage::refresh);
    connect(&m_s.library, &Library::gamesChanged, this, [this] {
        refreshGames();
        refreshRuns();
    });
    connect(&m_s.covers, &CoverArtService::artChanged, this, &WarmupPage::refreshRuns);
    connect(&m_s.settings, &AppSettings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::RecentWarmups)) {
            refreshRuns();
            refresh();
        }
    });
    refreshGames();
    refresh();
    refreshRuns();
}

void WarmupPage::start(bool full)
{
    m_startError.clear();
    if (!m_s.warmup.start(full, m_s.library.game(m_game->currentData().toString()), &m_startError))
        refresh();
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
    refresh();
}

void WarmupPage::refresh()
{
    using State = WarmupController::State;
    const State st = m_s.warmup.state();
    const WarmupController::Progress pr = m_s.warmup.progress();
    const bool active = m_s.warmup.isActive();
    const bool gameHolds = m_s.gate.owner() == ProcessGate::Owner::Game;
    const QString cannot = active ? QString() : m_s.warmup.checkStart();

    if (active || (st != State::Idle && !m_dismissed)) {
        const int i = m_game->findData(m_s.warmup.game().titleId());
        if (i >= 0)
            m_game->setCurrentIndex(i);
    }
    m_game->setEnabled(!active);
    const Game picked = m_s.library.game(m_game->currentData().toString());
    const QString pickedName = picked.isValid() ? picked.title() : QString();

    m_start->setEnabled(cannot.isEmpty() && !active);
    m_start->setText(active ? QStringLiteral("Warming up...") : QStringLiteral("Start warm-up"));
    m_start->setIconName(gameHolds ? QStringLiteral("lock") : QStringLiteral("bolt"));
    m_start->setToolTip(cannot);

    // the banner: a game holds the one XSEmu slot, or something else keeps the warm-up from starting
    const QString blocker = gameHolds ? QString() : (!m_startError.isEmpty() ? m_startError : cannot);
    const bool bannerShown = gameHolds || (!blocker.isEmpty() && !active);
    m_banner->setVisible(bannerShown);
    m_stopGame->setVisible(gameHolds);
    if (gameHolds) {
        const Game playing = m_s.emulator.game();
        const bool paused = m_s.emulator.state() == EmulatorController::State::Paused;
        const qint64 held = m_s.emulator.stats().heldBytes;
        m_bannerTitle->setText(QStringLiteral("Warm-up waits for %1").arg(playing.title()));
        const QString holds = held > 0 ? QStringLiteral("The %1 game still holds %2, and XSEmu runs one process at a time.").arg(paused ? QStringLiteral("paused") : QStringLiteral("running"), Format::gigabytes(held))
                                       : QStringLiteral("XSEmu runs one process at a time.");
        m_bannerText->setText(holds + (pickedName.isEmpty() ? QStringLiteral(" Stop the game to warm up.") : QStringLiteral(" Stop the game to warm up %1.").arg(pickedName)));
    } else if (bannerShown) {
        m_bannerTitle->setText(QStringLiteral("Warm-up can't start"));
        m_bannerText->setText(blocker);
    }

    for (Button* b : {m_warm, m_play, m_resume, m_log, m_dismiss, m_cancel})
        b->hide();
    m_block->setNote({});
    const bool showResult = !active && st != State::Idle && !m_dismissed;
    const bool blockShown = !gameHolds && (active || showResult || picked.isValid() || m_game->count() == 0);
    m_block->setVisible(blockShown);
    if (!blockShown)
        return;

    const QString game = m_s.warmup.game().title();
    const auto withGame = [&game](const QString& s) { return game.isEmpty() ? s : game + QStringLiteral(" · ") + s; };
    QList<ProgressBlock::Column> progressColumns{
        {QStringLiteral("Done"), QStringLiteral("%1 / %2").arg(Format::count(pr.done), Format::count(pr.total))},
        {QStringLiteral("Speed"), QStringLiteral("%1 keys/s").arg(Format::count(qRound64(pr.keysPerS)))},
    };
    if (st == State::PausedForRam)
        progressColumns.append({QStringLiteral("Time left"), QStringLiteral("Paused")});
    else if (pr.etaS)
        progressColumns.append({QStringLiteral("Time left"), Format::duration(*pr.etaS)});
    progressColumns.append({QStringLiteral("Free RAM"), pr.ramFreeMb ? ramText(*pr.ramFreeMb) : QStringLiteral("Unknown")});
    progressColumns.append({QStringLiteral("Giant compiles"), giants(pr.giantsCompiling, pr.giantsMax)});

    if (st == State::Running) {
        m_block->setState(ProgressBlock::State::Running);
        m_block->setTitle(QStringLiteral("Warming up shaders"));
        m_block->setSubtitle(withGame(phaseText(pr)));
        m_block->setFraction(pr.phaseFraction());
        m_block->setColumns(progressColumns);
        m_cancel->setText(m_s.warmup.cancelRequested() ? QStringLiteral("Cancelling...") : QStringLiteral("Cancel"));
        m_cancel->setEnabled(!m_s.warmup.cancelRequested());
        m_cancel->show();
        if (m_s.warmup.cancelRequested())
            m_block->setNote(QStringLiteral("No new compiles start. The ones in flight finish first, which can take a few minutes for giant shaders."));
        return;
    }
    if (st == State::PausedForRam) {
        m_block->setState(ProgressBlock::State::Paused);
        m_block->setTitle(QStringLiteral("Paused at the RAM floor"));
        m_block->setSubtitle(withGame(phaseText(pr)));
        m_block->setFraction(pr.phaseFraction());
        m_block->setColumns(progressColumns);
        m_block->setColumnTone(int(progressColumns.size()) - 2, Theme::Role::Warning);
        const QString ram = pr.ramFreeMb ? QStringLiteral("Free RAM fell to %1, below the 1 GB floor. ").arg(ramText(*pr.ramFreeMb)) : QStringLiteral("Free RAM is below the 1 GB floor. ");
        m_block->setNote(ram + QStringLiteral("Compiles wait and continue on their own when memory frees; closing other apps helps. Finished pipelines are kept."));
        m_cancel->setText(m_s.warmup.cancelRequested() ? QStringLiteral("Cancelling...") : QStringLiteral("Cancel"));
        m_cancel->setEnabled(!m_s.warmup.cancelRequested());
        m_cancel->show();
        return;
    }

    if (showResult) {
        WarmupRun fromProgress;
        fromProgress.done = pr.done;
        fromProgress.total = pr.total;
        fromProgress.failed = pr.failed;
        fromProgress.tookS = pr.elapsedS;
        const WarmupRun run = m_s.warmup.lastRun().value_or(fromProgress);
        const QString at = QStringLiteral("at %1 of %2").arg(Format::count(run.done), Format::count(run.total));
        const bool comparable = run.total > 0 && run.done <= run.total;
        const bool canPlay = m_s.warmup.game().isValid() && m_s.gate.isFree();
        switch (st) {
        case State::Finished: {
            m_block->setState(ProgressBlock::State::Finished);
            m_block->setTitle(QStringLiteral("Warm-up finished"));
            m_block->setSubtitle(QStringLiteral("%1 pipelines in %2").arg(Format::count(run.done), Format::duration(qint64(run.tookS + 0.5))));
            m_block->setFraction(1.0);
            const double avg = run.tookS > 0 ? run.done / run.tookS : 0;
            m_block->setColumns({
                {QStringLiteral("Done"), builtOf(run.done, run.total)},
                {QStringLiteral("Speed"), QStringLiteral("%1 keys/s avg").arg(Format::count(qRound64(avg)))},
                {QStringLiteral("Time left"), QStringLiteral("Done")},
                {QStringLiteral("Free RAM"), pr.ramFreeMb ? Format::gigabytesFromMb(*pr.ramFreeMb) : QStringLiteral("Unknown")},
                {QStringLiteral("Giant compiles"), giants(0, pr.giantsMax)},
            });
            if (run.failed > 0)
                m_block->setNote(QStringLiteral("%1 pipelines failed to build; the game compiles them when it needs them. The log names them.").arg(Format::count(run.failed)));
            if (canPlay) {
                const QString name = m_s.warmup.game().title();
                m_play->setText(name.size() > 24 ? QStringLiteral("Play") : QStringLiteral("Play %1").arg(name));
                m_play->show();
            }
            if (run.failed > 0)
                m_log->show();
            m_dismiss->show();
            return;
        }
        case State::Cancelled: {
            m_block->setState(ProgressBlock::State::Cancelled);
            m_block->setTitle(QStringLiteral("Warm-up cancelled"));
            m_block->setSubtitle(comparable ? at : game);
            m_block->setFraction(comparable ? fractionOf(run.done, run.total) : -1);
            m_block->setColumns({});
            const QString left = comparable && run.total > run.done
                                     ? QStringLiteral(" Playing now still stalls on the %1 that are left.").arg(Format::count(run.total - run.done))
                                     : QString();
            m_block->setNote(QStringLiteral("Finished pipelines are kept, so resuming picks up where it stopped.") + left);
            m_resume->setText(QStringLiteral("Resume warm-up"));
            m_resume->show();
            m_dismiss->show();
            return;
        }
        case State::StoppedPartWay: {
            m_block->setState(ProgressBlock::State::Paused);
            m_block->setTitle(QStringLiteral("Warm-up stopped part way"));
            m_block->setSubtitle(comparable ? at : game);
            m_block->setFraction(comparable ? fractionOf(run.done, run.total) : -1);
            m_block->setColumns({});
            m_block->setNote(run.stoppedForRam
                                 ? QStringLiteral("Free RAM stayed below the 1 GB floor. Close other apps, then run it again; finished pipelines are kept.")
                                 : QStringLiteral("The budgeted pass builds the rest largest first for 20 minutes. A full pass builds everything with no time limit and goes on where an earlier full pass stopped."));
            m_resume->setText(run.stoppedForRam ? QStringLiteral("Run again") : QStringLiteral("Run full pass"));
            m_resume->show();
            m_dismiss->show();
            return;
        }
        case State::Failed: {
            m_block->setState(ProgressBlock::State::Failed);
            m_block->setTitle(QStringLiteral("Warm-up failed"));
            m_block->setColumns({});
            QString kept;
            if (!m_s.warmup.doneLine() && pr.total > 0) {
                // no warmup_done line: say where the last progress line had it
                m_block->setSubtitle(QStringLiteral("in %1, at %2 of %3").arg(WarmupController::phaseLabel(pr.phase).toLower(), Format::count(pr.done), Format::count(pr.total)));
                m_block->setFraction(pr.phaseFraction());
                kept = QStringLiteral(" Finished pipelines are kept.");
            } else {
                m_block->setSubtitle(comparable ? at : game);
                m_block->setFraction(comparable ? fractionOf(run.done, run.total) : -1);
                if (run.done > 0)
                    kept = QStringLiteral(" The %1 finished pipelines are kept.").arg(Format::count(qMax<qint64>(0, run.done - run.failed)));
            }
            const QString why = m_s.warmup.errorMessage().isEmpty() ? QString() : QStringLiteral(" ") + sentence(m_s.warmup.errorMessage());
            m_block->setNote(QStringLiteral("XSEmu exited with code %1.").arg(Format::exitCode(run.exitCode)) + kept + why);
            m_resume->setText(QStringLiteral("Retry"));
            m_resume->show();
            m_log->setEnabled(QFileInfo::exists(WarmupController::logPath()));
            m_log->show();
            return;
        }
        default:
            break;
        }
    }

    // idle: what a warm-up would do for the picked game
    const ShaderCacheInfo cache = ShaderCacheInfo::readDefault();
    const QList<WarmupRun> recent = m_s.settings.recentWarmups();
    m_block->setState(ProgressBlock::State::Idle);
    m_block->setFraction(0);
    m_block->setColumns({});
    const TitleShaderState pickedState = picked.isValid() ? titleShaderState(picked.titleId(), recent, cache) : TitleShaderState::Unknown;
    switch (pickedState) {
    case TitleShaderState::Ready:
        m_block->setTitle(QStringLiteral("%1 is warmed up").arg(pickedName));
        break;
    case TitleShaderState::LastRunIncomplete:
        m_block->setTitle(QStringLiteral("%1's last warm-up stopped part way").arg(pickedName));
        break;
    case TitleShaderState::LastRunFailed:
        m_block->setTitle(QStringLiteral("%1's last warm-up failed").arg(pickedName));
        break;
    case TitleShaderState::NeverWarmed:
    case TitleShaderState::Unknown:
        m_block->setTitle(pickedName.isEmpty() ? QStringLiteral("Warm up shaders") : QStringLiteral("%1 needs a warm-up").arg(pickedName));
        break;
    }
    QString lastRunText;
    for (const WarmupRun& r : recent) {
        if (picked.isValid() && r.titleId == picked.titleId()) {
            lastRunText = QStringLiteral("last run %1").arg(Format::ago(r.finishedAt).toLower());
            break;
        }
    }
    m_block->setSubtitle(pickedState == TitleShaderState::NeverWarmed ? QStringLiteral("never warmed up") : lastRunText);

    double built = 0, took = 0;
    for (const WarmupRun& r : recent) {
        if (r.result == WarmupRun::Result::Finished && r.tookS > 0) {
            built += r.done;
            took += r.tookS;
        }
    }
    QString note;
    if (cache.pipelines > 0) {
        note = QStringLiteral("%1 pipelines recorded").arg(Format::count(cache.pipelines));
        if (built > 0 && took > 0)
            note += QStringLiteral(", about %1 on this PC").arg(Format::duration(qint64(cache.pipelines / (built / took) + 0.5)));
        note += QStringLiteral(". ");
    } else {
        note = QStringLiteral("XSEmu records each pipeline a game builds; play a game once and a warm-up builds them all ahead of time. ");
    }
    QStringList others;
    for (const Game& g : m_s.library.games()) {
        if (g.titleId() == picked.titleId() || others.size() >= 3)
            continue;
        const QString s = stateSentence(g.title(), titleShaderState(g.titleId(), recent, cache));
        if (!s.isEmpty())
            others << s;
    }
    m_block->setNote((note + joinSentences(others)).trimmed());
    m_warm->setText(pickedName.isEmpty() || pickedName.size() > 24 ? QStringLiteral("Warm up") : QStringLiteral("Warm up %1").arg(pickedName));
    m_warm->setEnabled(cannot.isEmpty());
    m_warm->setToolTip(cannot);
    m_warm->show();
}

void WarmupPage::refreshRuns()
{
    while (QLayoutItem* item = m_runs->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    QList<WarmupRun> runs = m_s.settings.recentWarmups();
    const bool active = m_s.warmup.isActive();
    if (runs.isEmpty() && !active) {
        m_runs->addWidget(new Label(QStringLiteral("No warm-ups yet."), Label::Style::Small, Theme::Role::Text3), 0, 0);
        return;
    }
    const QStringList heads = {QStringLiteral("Game"), QStringLiteral("Pipelines"), QStringLiteral("Took"), QStringLiteral("Result"), QStringLiteral("When")};
    for (int c = 0; c < heads.size(); ++c) {
        auto* h = new Label(heads[c], Label::Style::Caption, Theme::Role::Text2);
        h->setFixedHeight(32);
        if (c == 1 || c == 2 || c == 4)
            h->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(h, 0, c);
    }
    m_runs->addWidget(new Rule, 1, 0, 1, heads.size());
    int row = 2;
    const auto addRow = [&](const QString& titleId, const QString& titleName, const QString& count, const QString& took, const RunResult& rr, const QString& when) {
        auto* nameCell = new QWidget;
        nameCell->setFixedHeight(47);
        auto* nh = new QHBoxLayout(nameCell);
        nh->setContentsMargins(0, 0, 0, 0);
        nh->setSpacing(10);
        auto* thumb = new CoverThumb(QSize(22, 30));
        thumb->setCover(titleId.isEmpty() ? QPixmap() : QPixmap(m_s.covers.artPath(titleId, CoverCache::Kind::Grid)), titleName);
        nh->addWidget(thumb);
        nh->addWidget(new Label(titleName.isEmpty() ? QStringLiteral("All games") : titleName, Label::Style::BodyMedium), 1);
        m_runs->addWidget(nameCell, row, 0);
        auto* countLabel = new Label(count, Label::Style::Number, Theme::Role::Text2);
        countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(countLabel, row, 1);
        auto* tookLabel = new Label(took, Label::Style::Number, Theme::Role::Text2);
        tookLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(tookLabel, row, 2);
        auto* resultCell = new QWidget;
        auto* rh = new QHBoxLayout(resultCell);
        rh->setContentsMargins(0, 0, 0, 0);
        rh->setSpacing(0);
        rh->addWidget(new Badge({}, rr.tone, Badge::Kind::Plain));
        rh->addWidget(new Label(rr.text, Label::Style::Small, toneText(rr.tone)));
        m_runs->addWidget(resultCell, row, 3, Qt::AlignLeft | Qt::AlignVCenter);
        auto* whenLabel = new Label(when, Label::Style::Small, Theme::Role::Text3);
        whenLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_runs->addWidget(whenLabel, row, 4);
        m_runs->addWidget(new Rule, row + 1, 0, 1, heads.size());
        row += 2;
    };
    if (active) {
        const WarmupController::Progress pr = m_s.warmup.progress();
        const Game g = m_s.warmup.game();
        addRow(g.titleId(), g.title(), QStringLiteral("%1 / %2").arg(Format::count(pr.done), Format::count(pr.total)), Format::shortDuration(pr.elapsedS),
               {m_s.warmup.state() == WarmupController::State::PausedForRam ? QStringLiteral("Paused · RAM floor") : QStringLiteral("Running"),
                m_s.warmup.state() == WarmupController::State::PausedForRam ? Theme::Tone::Warning : Theme::Tone::Accent},
               QStringLiteral("Now"));
    }
    for (const WarmupRun& run : runs)
        addRow(run.titleId, run.titleName, builtOf(run.done, run.total), Format::shortDuration(run.tookS), runResult(run), Format::ago(run.finishedAt));
    m_runs->setColumnStretch(0, 1);
}
