#include "SettingsPage.h"

#include "app/Services.h"
#include "core/Format.h"
#include "core/HardwareInfo.h"
#include "core/Paths.h"
#include "core/ShaderCacheInfo.h"
#include "widgets/Button.h"
#include "widgets/Card.h"
#include "widgets/Icon.h"
#include "widgets/InfoNote.h"
#include "widgets/NavItem.h"
#include "widgets/PageHeader.h"
#include "widgets/SegmentedControl.h"
#include "widgets/Text.h"
#include "widgets/Toggle.h"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct SectionPage {
    QWidget* page;
    QVBoxLayout* body;
};

SectionPage makeSection(const QString& title, const QString& description)
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(32, 24, Metrics::PagePadding, Metrics::PagePadding);
    v->setSpacing(Metrics::S4);
    v->addWidget(new Label(title, Label::Style::SectionTitle));
    auto* d = new Label(description, Label::Style::Small, Theme::Role::Text2);
    d->setWordWrap(true);
    v->addWidget(d);
    v->addSpacing(4);
    auto* body = new QVBoxLayout;
    body->setSpacing(Metrics::S3);
    v->addLayout(body);
    v->addStretch(1);
    return {page, body};
}

// A settings card row: label and help on the left, the control on the right.
QWidget* settingRow(const QString& label, const QString& help, QWidget* control)
{
    auto* card = new Card(Card::Kind::Surface, Metrics::RadiusXl);
    card->setShadow(false);
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(18, 14, 18, 15);
    h->setSpacing(24);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(new Label(label, Label::Style::BodyMedium));
    if (!help.isEmpty()) {
        auto* l = new Label(help, Label::Style::Small, Theme::Role::Text3);
        l->setWordWrap(true);
        text->addWidget(l);
    }
    h->addLayout(text, 1);
    if (control)
        h->addWidget(control, 0, Qt::AlignVCenter);
    return card;
}

QWidget* valueLabel(const QString& value, bool mono = false)
{
    auto* l = new Label(value, mono ? Label::Style::Mono : Label::Style::Number, Theme::Role::Text2);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QString countOrUnknown(qint64 n)
{
    return n < 0 ? QStringLiteral("None yet") : Format::count(n);
}

}

SettingsPage::SettingsPage(Services& services, QWidget* parent)
    : QWidget(parent)
    , m_s(services)
    , m_stack(new QStackedWidget)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(new PageHeader(QStringLiteral("Settings")));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* nav = new QWidget;
    nav->setFixedWidth(220);
    auto* nv = new QVBoxLayout(nav);
    nv->setContentsMargins(Metrics::PagePadding - 10, 20, 0, 20);
    nv->setSpacing(2);
    auto* group = new QButtonGroup(this);
    const QStringList names = {
        QStringLiteral("Console"), QStringLiteral("Network"), QStringLiteral("Shaders"),
        QStringLiteral("Overlay"), QStringLiteral("Game folders"), QStringLiteral("Input"),
        QStringLiteral("Graphics"), QStringLiteral("Logs & crash reports"), QStringLiteral("About"),
    };
    for (int i = 0; i < names.size(); ++i) {
        auto* item = new NavItem(names[i]);
        group->addButton(item, i);
        nv->addWidget(item);
        m_nav << item;
        connect(item, &NavItem::clicked, this, [this, i] { showSection(Section(i)); });
    }
    nv->addStretch(1);
    row->addWidget(nav);

    for (QWidget* page : {buildConsole(), buildNetwork(), buildShaders(), buildOverlay(), buildGameFolders(),
                          buildInput(), buildGraphics(), buildLogs(), buildAbout()}) {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        page->setObjectName(QStringLiteral("scrollContents"));
        page->setMaximumWidth(760);
        scroll->setWidget(page);
        m_stack->addWidget(scroll);
    }
    row->addWidget(m_stack, 1);
    v->addLayout(row, 1);
    showSection(Section::Console);
}

void SettingsPage::showSection(Section section)
{
    m_nav[int(section)]->setChecked(true);
    m_stack->setCurrentIndex(int(section));
}

QWidget* SettingsPage::buildConsole()
{
    const SectionPage s = makeSection(QStringLiteral("Console"), QStringLiteral("The console model sets each game's memory budget and resolution targets. A game's own setting wins over this default."));
    auto* model = new SegmentedControl({QStringLiteral("Series S"), QStringLiteral("Series X")});
    model->setCurrentIndex(m_s.settings.defaultDeviceType() == QLatin1String("anaconda") ? 1 : 0);
    connect(model, &SegmentedControl::currentChanged, this, [this](int i) {
        m_s.settings.setDefaultDeviceType(i == 1 ? QStringLiteral("anaconda") : QStringLiteral("lockhart"));
    });
    s.body->addWidget(settingRow(QStringLiteral("Console model"), QStringLiteral("Series S: 8 GB title memory. Series X: 13.5 GB, needs much more RAM and VRAM."), model));
    const HardwareInfo hw = HardwareInfo::read();
    s.body->addWidget(new InfoNote(QStringLiteral("This PC has %1 of RAM and %2.").arg(Format::gigabytes(qint64(hw.totalRamBytes), 0), hw.gpuNameOrUnknown())));
    return s.page;
}

QWidget* SettingsPage::buildNetwork()
{
    const SectionPage s = makeSection(QStringLiteral("Network"), QStringLiteral("Offline keeps every game off the network. Store queries fail the way they do on a console without a connection."));
    auto* offline = new Toggle;
    offline->setChecked(m_s.settings.offline());
    connect(offline, &Toggle::toggled, &m_s.settings, &AppSettings::setOffline);
    s.body->addWidget(settingRow(QStringLiteral("Play offline"), QStringLiteral("Recommended. XSEmu starts games with networking off."), offline));
    return s.page;
}

QWidget* SettingsPage::buildShaders()
{
    const SectionPage s = makeSection(QStringLiteral("Shaders"), QStringLiteral("XSEmu keeps one shader cache for the whole PC. Warm-ups fill it ahead of time; games add to it as they play."));
    const ShaderCacheInfo cache = ShaderCacheInfo::readDefault();
    s.body->addWidget(settingRow(QStringLiteral("Cache folder"), QDir::toNativeSeparators(cache.dxilDir), nullptr));
    s.body->addWidget(settingRow(QStringLiteral("Recorded pipelines"), QStringLiteral("Distinct pipelines games have asked for."), valueLabel(countOrUnknown(cache.pipelines))));
    s.body->addWidget(settingRow(QStringLiteral("Driver blobs"), QStringLiteral("Compiled pipelines the GPU driver can load directly."),
                                 valueLabel(cache.blobs < 0 ? QStringLiteral("None yet") : QStringLiteral("%1 · %2").arg(Format::count(cache.blobs), Format::bytes(cache.blobBytes)))));
    auto* cap = new SegmentedControl({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")});
    cap->setCurrentIndex(qBound(1, m_s.settings.giantCompileCap(), 3) - 1);
    connect(cap, &SegmentedControl::currentChanged, this, [this](int i) { m_s.settings.setGiantCompileCap(i + 1); });
    s.body->addWidget(settingRow(QStringLiteral("Giant compiles at once"), QStringLiteral("Shaders over 1 MB take about 1.4 GB of RAM each in the driver."), cap));
    return s.page;
}

QWidget* SettingsPage::buildOverlay()
{
    const SectionPage s = makeSection(QStringLiteral("Overlay"), QStringLiteral("The performance overlay draws a frame-time graph over the game. F3 toggles it while you play."));
    auto* on = new Toggle;
    on->setChecked(m_s.settings.overlayDefault());
    connect(on, &Toggle::toggled, &m_s.settings, &AppSettings::setOverlayDefault);
    s.body->addWidget(settingRow(QStringLiteral("Show at start"), QStringLiteral("Turn the overlay on when a game starts."), on));
    auto* hide = new Toggle;
    hide->setChecked(m_s.settings.gameBarAutoHide());
    connect(hide, &Toggle::toggled, &m_s.settings, &AppSettings::setGameBarAutoHide);
    s.body->addWidget(settingRow(QStringLiteral("Hide the game bar in fullscreen"), QStringLiteral("Move the mouse to the top edge to show it again."), hide));
    return s.page;
}

QWidget* SettingsPage::buildGameFolders()
{
    const SectionPage s = makeSection(QStringLiteral("Game folders"), QStringLiteral("XSEmu looks for games with a MicrosoftGame.config up to three folders deep and watches these folders for changes."));
    auto* list = new QWidget;
    m_folders = new QVBoxLayout(list);
    m_folders->setContentsMargins(0, 0, 0, 0);
    m_folders->setSpacing(Metrics::S2);
    s.body->addWidget(list);
    auto* add = new Button(QStringLiteral("Add folder"), Button::Variant::Secondary, Button::Size::Small, QStringLiteral("plus"));
    s.body->addWidget(add, 0, Qt::AlignLeft);
    connect(add, &Button::clicked, this, &SettingsPage::addGamesRequested);
    connect(&m_s.settings, &AppSettings::changed, this, [this](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::GameFolders))
            refreshFolders();
    });
    refreshFolders();
    return s.page;
}

void SettingsPage::refreshFolders()
{
    while (QLayoutItem* item = m_folders->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QStringList folders = m_s.settings.gameFolders();
    if (folders.isEmpty())
        m_folders->addWidget(new Label(QStringLiteral("No folders yet."), Label::Style::Small, Theme::Role::Text3));
    for (const QString& folder : folders) {
        auto* remove = new Button(QStringLiteral("Remove"), Button::Variant::Ghost, Button::Size::Tiny);
        connect(remove, &Button::clicked, this, [this, folder] {
            QStringList f = m_s.settings.gameFolders();
            f.removeAll(folder);
            m_s.settings.setGameFolders(f);
        });
        m_folders->addWidget(settingRow(QDir::toNativeSeparators(folder), {}, remove));
    }
}

QWidget* SettingsPage::buildInput()
{
    const SectionPage s = makeSection(QStringLiteral("Input"), QStringLiteral("Controllers and the keyboard go straight to the game window while it has focus."));
    s.body->addWidget(new InfoNote(QStringLiteral("XInput controllers work as the first player. In the game window, F1 shows the keys, F3 toggles the overlay, F11 switches fullscreen and Esc returns to the launcher."), Theme::Tone::Neutral, QStringLiteral("gamepad")));
    return s.page;
}

QWidget* SettingsPage::buildGraphics()
{
    const SectionPage s = makeSection(QStringLiteral("Graphics"), QStringLiteral("How the launcher looks. Games always render with Direct3D 12 on the GPU Windows picks for XSEmu."));
    auto* theme = new SegmentedControl({QStringLiteral("System"), QStringLiteral("Light"), QStringLiteral("Dark")});
    theme->setCurrentIndex(int(m_s.settings.theme()));
    connect(theme, &SegmentedControl::currentChanged, this, [this](int i) { m_s.settings.setTheme(AppSettings::ThemeMode(i)); });
    s.body->addWidget(settingRow(QStringLiteral("Launcher theme"), QStringLiteral("System follows the Windows app mode."), theme));
    return s.page;
}

QWidget* SettingsPage::buildLogs()
{
    const SectionPage s = makeSection(QStringLiteral("Logs & crash reports"), QStringLiteral("Each game keeps its log and crash report in its own folder under the data root. Nothing is uploaded."));
    auto* open = new Button(QStringLiteral("Open folder"), Button::Variant::Secondary, Button::Size::Small, QStringLiteral("external"));
    connect(open, &Button::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::dataRoot())); });
    s.body->addWidget(settingRow(QStringLiteral("Data root"), QDir::toNativeSeparators(Paths::dataRoot()), open));
    return s.page;
}

QWidget* SettingsPage::buildAbout()
{
    const SectionPage s = makeSection(QStringLiteral("About"), QStringLiteral("XSEmu runs Xbox Series games you own on a Windows PC. The launcher starts XSEmu.exe as a separate process."));
    s.body->addWidget(settingRow(QStringLiteral("Launcher"), {}, valueLabel(QStringLiteral(XSEMU_VERSION))));
    s.body->addWidget(settingRow(QStringLiteral("Emulator"), QDir::toNativeSeparators(m_s.settings.emulatorPath()), nullptr));
    s.body->addWidget(settingRow(QStringLiteral("Qt"), {}, valueLabel(QString::fromLatin1(qVersion()))));
    s.body->addWidget(settingRow(QStringLiteral("Settings file"), QDir::toNativeSeparators(m_s.settings.fileName()), nullptr));
    return s.page;
}
