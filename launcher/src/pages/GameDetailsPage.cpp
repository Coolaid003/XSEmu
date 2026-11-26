#include "GameDetailsPage.h"

#include "PageScaffold.h"
#include "app/Services.h"
#include "core/Format.h"
#include "core/ShaderCacheInfo.h"
#include "widgets/Button.h"
#include "widgets/CoverTile.h"
#include "widgets/PageHeader.h"
#include "widgets/Text.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

GameDetailsPage::GameDetailsPage(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
    , m_play(new Button(QStringLiteral("Play"), Button::Variant::Primary, Button::Size::Large, QStringLiteral("play")))
    , m_cover(new CoverTile(CoverTile::Kind::NoArt))
    , m_facts(new QGridLayout)
    , m_blocked(new Label({}, Label::Style::Small, Theme::Role::Text3))
{
    const PageScaffold page = makePageScaffold(this, QString());
    m_header = page.header;
    auto* back = new Button(QStringLiteral("Library"), Button::Variant::Ghost, Button::Size::Small, QStringLiteral("back"));
    m_header->addLeading(back);
    m_header->addAction(m_play);

    auto* row = new QHBoxLayout;
    row->setSpacing(32);
    m_cover->setFocusPolicy(Qt::NoFocus);
    m_cover->setCursor(Qt::ArrowCursor);
    row->addWidget(m_cover, 0, Qt::AlignTop);
    auto* right = new QVBoxLayout;
    right->setSpacing(12);
    m_facts->setHorizontalSpacing(24);
    m_facts->setVerticalSpacing(10);
    m_facts->setColumnStretch(1, 1);
    right->addLayout(m_facts);
    m_blocked->setWordWrap(true);
    right->addWidget(m_blocked);
    right->addStretch(1);
    row->addLayout(right, 1);
    page.content->addLayout(row);
    page.content->addStretch(1);

    connect(back, &Button::clicked, this, &GameDetailsPage::back);
    connect(m_play, &Button::clicked, this, [this] { emit playRequested(m_game); });
    connect(&m_s.gate, &ProcessGate::changed, this, &GameDetailsPage::refresh);
    connect(&m_s.covers, &CoverArtService::artChanged, this, [this](const QString& id) {
        if (id == m_game.titleId())
            refresh();
    });
}

void GameDetailsPage::setGame(const Game& game)
{
    m_game = game;
    refresh();
}

void GameDetailsPage::refresh()
{
    m_header->setTitle(m_game.title());
    m_cover->setTitle(m_game.title());
    const QString art = m_s.covers.artPath(m_game.titleId(), CoverCache::Kind::Grid);
    m_cover->setArt(!art.isEmpty() && QFileInfo::exists(art) ? QPixmap(art) : QPixmap());

    QString shaders;
    switch (titleShaderState(m_game.titleId(), m_s.settings.recentWarmups(), ShaderCacheInfo::readDefault())) {
    case TitleShaderState::Ready: shaders = QStringLiteral("Ready"); break;
    case TitleShaderState::NeverWarmed: shaders = QStringLiteral("Never warmed up"); break;
    case TitleShaderState::LastRunIncomplete: shaders = QStringLiteral("Last warm-up didn't finish"); break;
    case TitleShaderState::LastRunFailed: shaders = QStringLiteral("Last warm-up failed"); break;
    case TitleShaderState::Unknown: shaders = QStringLiteral("Unknown"); break;
    }
    const qint64 played = m_s.settings.playSeconds(m_game.titleId());
    const QDateTime last = m_s.settings.lastPlayed(m_game.titleId());
    const QList<QPair<QString, QString>> facts = {
        {QStringLiteral("Publisher"), m_game.publisher.isEmpty() ? QStringLiteral("Unknown") : m_game.publisher},
        {QStringLiteral("Version"), m_game.version.isEmpty() ? QStringLiteral("Unknown") : m_game.version},
        {QStringLiteral("Folder"), QDir::toNativeSeparators(m_game.folder)},
        {QStringLiteral("Executable"), m_game.exe.isEmpty() ? QStringLiteral("None named") : m_game.exe + (m_game.exeExists ? QString() : QStringLiteral(" (missing)"))},
        {QStringLiteral("Size"), m_game.sizeBytes < 0 ? QStringLiteral("Not measured") : Format::bytes(m_game.sizeBytes)},
        {QStringLiteral("Shaders"), shaders},
        {QStringLiteral("Played"), played > 0 ? QStringLiteral("%1 · last %2").arg(Format::playTime(played), Format::ago(last)) : QStringLiteral("Never")},
    };
    while (QLayoutItem* item = m_facts->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int i = 0; i < facts.size(); ++i) {
        m_facts->addWidget(new Label(facts[i].first, Label::Style::Small, Theme::Role::Text3), i, 0, Qt::AlignTop);
        auto* value = new Label(facts[i].second, Label::Style::Small, Theme::Role::Text);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_facts->addWidget(value, i, 1);
    }

    const bool playingThis = m_s.emulator.isActive() && m_s.emulator.game().titleId() == m_game.titleId();
    const QString blocked = playingThis ? QString() : m_s.gate.blockedReason(ProcessGate::Action::LaunchGame);
    m_play->setEnabled(blocked.isEmpty());
    m_play->setText(playingThis ? QStringLiteral("Return to game") : QStringLiteral("Play"));
    m_blocked->setText(blocked);
    m_blocked->setVisible(!blocked.isEmpty());
}
