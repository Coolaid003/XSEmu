#include "LibraryPage.h"

#include "PageScaffold.h"
#include "app/Services.h"
#include "core/Format.h"
#include "core/ShaderCacheInfo.h"
#include "widgets/Badge.h"
#include "widgets/Button.h"
#include "widgets/Card.h"
#include "widgets/CoverTile.h"
#include "widgets/Icon.h"
#include "widgets/PageHeader.h"
#include "widgets/SearchBox.h"
#include "widgets/Text.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {

constexpr int kTileGap = 16; // 24 px between the art, less the selection ring on both tiles

struct TileStatus {
    QString text;
    Theme::Tone tone = Theme::Tone::Neutral;
};

TileStatus shaderStatus(TitleShaderState state)
{
    switch (state) {
    case TitleShaderState::Ready: return {QStringLiteral("Shaders ready"), Theme::Tone::Success};
    case TitleShaderState::NeverWarmed: return {QStringLiteral("Never warmed up"), Theme::Tone::Neutral};
    case TitleShaderState::LastRunIncomplete: return {QStringLiteral("Warm-up needed"), Theme::Tone::Warning};
    case TitleShaderState::LastRunFailed: return {QStringLiteral("Warm-up failed"), Theme::Tone::Danger};
    case TitleShaderState::Unknown: break;
    }
    return {};
}

}

LibraryPage::LibraryPage(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
    , m_search(new SearchBox)
    , m_banner(new Card(Card::Kind::Surface))
    , m_bannerTitle(new Label({}, Label::Style::Title))
    , m_bannerText(new Label({}, Label::Style::Small, Theme::Role::Text2))
    , m_bannerHeld(new Badge({}, Theme::Tone::Warning))
    , m_resume(new Button(QStringLiteral("Resume"), Button::Variant::Secondary, Button::Size::Small, QStringLiteral("play")))
    , m_stop(new Button(QStringLiteral("Stop game"), Button::Variant::DangerSecondary, Button::Size::Small, QStringLiteral("stop")))
    , m_gridHost(new QWidget)
    , m_grid(new QGridLayout(m_gridHost))
    , m_addTile(new CoverTile(CoverTile::Kind::AddGames))
    , m_empty(new QWidget)
    , m_emptyTitle(new Label({}, Label::Style::SectionTitle))
    , m_emptyText(new Label({}, Label::Style::Body, Theme::Role::Text2))
    , m_emptyAction(new Button(QStringLiteral("Add game folder"), Button::Variant::Primary, Button::Size::Large, QStringLiteral("folder")))
{
    const PageScaffold page = makePageScaffold(this, QStringLiteral("Library"));
    m_header = page.header;
    m_header->addAction(m_search);
    auto* add = new Button(QStringLiteral("Add games"), Button::Variant::Secondary, Button::Size::Large, QStringLiteral("plus"));
    m_header->addAction(add);

    auto* bh = new QHBoxLayout(m_banner);
    bh->setContentsMargins(18, 16, 18, 18);
    bh->setSpacing(16);
    auto* bannerText = new QVBoxLayout;
    bannerText->setSpacing(4);
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    titleRow->addWidget(m_bannerTitle);
    titleRow->addWidget(m_bannerHeld);
    titleRow->addStretch(1);
    bannerText->addLayout(titleRow);
    m_bannerText->setWordWrap(true);
    bannerText->addWidget(m_bannerText);
    bh->addLayout(bannerText, 1);
    bh->addWidget(m_resume, 0, Qt::AlignVCenter);
    bh->addWidget(m_stop, 0, Qt::AlignVCenter);
    page.content->addWidget(m_banner);

    m_gridLabel = makeSectionLabel(QStringLiteral("All games"));
    page.content->addWidget(m_gridLabel);
    m_grid->setContentsMargins(-CoverTile::Ring, -CoverTile::Ring, 0, 0);
    m_grid->setHorizontalSpacing(kTileGap);
    m_grid->setVerticalSpacing(20);
    m_grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    page.content->addWidget(m_gridHost);

    auto* ev = new QVBoxLayout(m_empty);
    ev->setContentsMargins(0, 80, 0, 0);
    ev->setSpacing(8);
    auto* emptyIcon = new IconView(QStringLiteral("library"), Theme::Role::Text3, 28);
    ev->addWidget(emptyIcon, 0, Qt::AlignHCenter);
    ev->addSpacing(8);
    ev->addWidget(m_emptyTitle, 0, Qt::AlignHCenter);
    m_emptyText->setAlignment(Qt::AlignHCenter);
    m_emptyText->setWordWrap(true);
    m_emptyText->setMaximumWidth(460);
    ev->addWidget(m_emptyText, 0, Qt::AlignHCenter);
    ev->addSpacing(12);
    ev->addWidget(m_emptyAction, 0, Qt::AlignHCenter);
    page.content->addWidget(m_empty);
    page.content->addStretch(1);

    m_search->installShortcut(this);
    connect(m_search, &SearchBox::textChanged, this, &LibraryPage::rebuild);
    connect(add, &Button::clicked, this, &LibraryPage::addGamesRequested);
    connect(m_addTile, &CoverTile::clicked, this, &LibraryPage::addGamesRequested);
    connect(m_emptyAction, &Button::clicked, this, [this] {
        if (m_s.library.games().isEmpty())
            emit addGamesRequested();
        else
            m_search->setText({});
    });
    connect(m_resume, &Button::clicked, this, [this] {
        m_s.emulator.resume();
        emit nowPlayingRequested();
    });
    connect(m_stop, &Button::clicked, &m_s.gate, &ProcessGate::stopOwner);
    connect(&m_s.library, &Library::gamesChanged, this, &LibraryPage::rebuild);
    connect(&m_s.covers, &CoverArtService::artChanged, this, &LibraryPage::rebuild);
    connect(&m_s.gate, &ProcessGate::changed, this, &LibraryPage::refreshState);
    connect(&m_s.emulator, &EmulatorController::stateChanged, this, &LibraryPage::refreshState);
    connect(&m_s.emulator, &EmulatorController::statsChanged, this, &LibraryPage::refreshState);
    connect(&m_s.settings, &AppSettings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::RecentWarmups))
            refreshState();
    });
    rebuild();
}

void LibraryPage::rebuild()
{
    const GameList& all = m_s.library.games();
    const QString needle = m_search->text().trimmed();
    m_shown.clear();
    for (const Game& g : all) {
        if (needle.isEmpty() || g.title().contains(needle, Qt::CaseInsensitive))
            m_shown << g;
    }
    qDeleteAll(m_tiles);
    m_tiles.clear();
    for (const Game& g : std::as_const(m_shown)) {
        auto* tile = new CoverTile(CoverTile::Kind::NoArt, m_gridHost);
        tile->setTitle(g.title());
        const QString art = m_s.covers.artPath(g.titleId(), CoverCache::Kind::Grid);
        if (!art.isEmpty() && QFileInfo::exists(art))
            tile->setArt(QPixmap(art));
        connect(tile, &CoverTile::clicked, this, [this, g] { emit detailsRequested(g); });
        connect(tile, &CoverTile::playClicked, this, [this, g] { emit playRequested(g); });
        connect(tile, &CoverTile::doubleClicked, this, [this, g] { emit playRequested(g); });
        m_tiles << tile;
        m_s.covers.request(g);
    }
    m_header->setSubtitle(all.size() == 1 ? QStringLiteral("1 game") : QStringLiteral("%1 games").arg(all.size()));

    const bool noGames = all.isEmpty();
    const bool noMatches = !noGames && m_shown.isEmpty();
    m_empty->setVisible(noGames || noMatches);
    m_gridLabel->setVisible(!noGames && !noMatches);
    m_gridHost->setVisible(!noGames && !noMatches);
    if (noGames) {
        m_emptyTitle->setText(QStringLiteral("No games yet"));
        m_emptyText->setText(QStringLiteral("Add the folder that holds your unpacked Xbox Series games. XSEmu finds every game with a MicrosoftGame.config up to three folders deep."));
        m_emptyAction->setText(QStringLiteral("Add game folder"));
        m_emptyAction->setIconName(QStringLiteral("folder"));
    } else if (noMatches) {
        m_emptyTitle->setText(QStringLiteral("No games match \u201C%1\u201D").arg(needle));
        m_emptyText->setText(QStringLiteral("Search looks at game names. Check the spelling, or add the folder the game is in."));
        m_emptyAction->setText(QStringLiteral("Clear search"));
        m_emptyAction->setIconName(QStringLiteral("close"));
    }
    m_addTile->setVisible(needle.isEmpty());
    m_columns = 0;
    layoutGrid();
    refreshState();
}

void LibraryPage::refreshState()
{
    const ShaderCacheInfo cache = ShaderCacheInfo::readDefault();
    const QList<WarmupRun> recent = m_s.settings.recentWarmups();
    const EmulatorController::State st = m_s.emulator.state();
    const QString playingId = st == EmulatorController::State::Idle ? QString() : m_s.emulator.game().titleId();
    const QString blockedBy = m_s.gate.isFree() ? QString() : m_s.gate.ownerTitle();
    for (int i = 0; i < m_tiles.size(); ++i) {
        const Game& g = m_shown[i];
        CoverTile* tile = m_tiles[i];
        TileStatus status = shaderStatus(titleShaderState(g.titleId(), recent, cache));
        if (!g.exeExists)
            status = {QStringLiteral("Game files missing"), Theme::Tone::Danger};
        const bool isPlaying = g.titleId() == playingId;
        if (isPlaying) {
            switch (st) {
            case EmulatorController::State::Booting: status = {QStringLiteral("Starting"), Theme::Tone::Accent}; break;
            case EmulatorController::State::Running: status = {QStringLiteral("Playing"), Theme::Tone::Success}; break;
            case EmulatorController::State::Paused: status = {QStringLiteral("Paused · in Now playing"), Theme::Tone::Warning}; break;
            case EmulatorController::State::Stopping: status = {QStringLiteral("Stopping"), Theme::Tone::Neutral}; break;
            case EmulatorController::State::Idle: break;
            }
        }
        tile->setStatus(status.text, status.tone);
        tile->setSelected(isPlaying);
        const bool locked = !blockedBy.isEmpty() && !isPlaying;
        const QString reason = m_s.gate.owner() == ProcessGate::Owner::Warmup ? QStringLiteral("Wait for the warm-up")
                                                                                : QStringLiteral("Stop %1 to play").arg(blockedBy);
        tile->setLocked(locked, locked ? reason : QString());
    }

    const bool paused = st == EmulatorController::State::Paused;
    m_banner->setVisible(paused);
    if (paused) {
        m_bannerTitle->setText(QStringLiteral("%1 is paused").arg(m_s.emulator.game().title()));
        const qint64 held = m_s.emulator.stats().heldBytes;
        m_bannerHeld->setVisible(held > 0);
        m_bannerHeld->setText(QStringLiteral("%1 held").arg(Format::gigabytes(held)));
        m_bannerText->setText(QStringLiteral("A paused game keeps its memory. Warm-ups and other games wait until you stop it, because only one XSEmu process runs at a time."));
    }
}

void LibraryPage::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    layoutGrid();
}

void LibraryPage::layoutGrid()
{
    const int tileW = CoverTile::ArtWidth + 2 * CoverTile::Ring;
    const int avail = width() - 2 * Metrics::PagePadding + 2 * CoverTile::Ring;
    const int columns = qMax(1, (avail + kTileGap) / (tileW + kTileGap));
    if (columns == m_columns)
        return;
    m_columns = columns;
    while (m_grid->count())
        delete m_grid->takeAt(0);
    QList<CoverTile*> all = m_tiles;
    if (m_search->text().trimmed().isEmpty())
        all << m_addTile;
    for (int i = 0; i < all.size(); ++i)
        m_grid->addWidget(all[i], i / columns, i % columns, Qt::AlignTop | Qt::AlignLeft);
}
