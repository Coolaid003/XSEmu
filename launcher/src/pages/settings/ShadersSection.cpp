#include "Sections.h"
#include "SettingsKit.h"

#include "app/Services.h"
#include "core/Format.h"
#include "core/LaunchConfig.h"
#include "core/Paths.h"
#include "core/ShaderCacheInfo.h"
#include "widgets/Button.h"
#include "widgets/Dialog.h"
#include "widgets/InfoNote.h"
#include "widgets/Text.h"
#include "widgets/Toggle.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// A folder the cache may move to: empty, or one that already holds the emulator's cache folders.
bool isShaderCacheFolder(const QDir& dir)
{
    if (!dir.exists())
        return false;
    if (dir.isRoot() || QDir::cleanPath(dir.absolutePath()) == QDir::cleanPath(QDir::homePath()))
        return false;
    if (dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
        return true;
    for (const char* sub : {"pipelines", "driver", "translated"}) {
        if (QFileInfo(dir.filePath(QLatin1String(sub))).isDir())
            return true;
    }
    return false;
}

qint64 folderBytes(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}


}

// Warm-up when the translator changes: XSEmu reports no translator id to the launcher (it is a
// build-time constant, XCL_TRANSLATOR_SOURCE_ID), so it is shown unavailable. Giant compiles: no knob
// (the admission rule is shown instead). Cache folder: XCL_DXIL_CACHE (LaunchConfig). Clear: deletes
// the emulator's own entries in it after the confirm dialog (ShaderCacheInfo::clear) and names the rest.
QWidget* SettingsSections::shaders(Services& s)
{
    const SettingsKit::Section sec = SettingsKit::makeSection(
        QStringLiteral("Shaders"), QStringLiteral("XSEmu translates console shaders to DXIL and keeps the result, so each one compiles once."));
    sec.caps(QStringLiteral("Warm-up"));
    auto* onChange = new Toggle;
    sec.row(QStringLiteral("Warm up when the translator changes"),
            QStringLiteral("Runs the blocking warm-up before launch after an XSEmu update. Skipped when nothing changed."), onChange)
        ->setUnavailable(QStringLiteral("XSEmu doesn't report its translator version to the launcher yet."));
    sec.row(QStringLiteral("RAM floor"), QStringLiteral("Compiles wait while less than this is free, in the warm-up and in games."),
            SettingsKit::status(QStringLiteral("1 GB free"), Theme::Role::Text));
    sec.add(new InfoNote(QStringLiteral("XSEmu uses as much memory as it needs. Each compile starts when its estimated peak fits in free RAM minus the 1 GB floor, and waits in a queue until memory frees otherwise. There is no fixed limit on giant compiles.")), 12);

    sec.caps(QStringLiteral("Cache"));
    auto* pathRow = new QWidget;
    auto* ph = new QHBoxLayout(pathRow);
    ph->setContentsMargins(0, 12, 0, 12);
    ph->setSpacing(8);
    auto* path = new SettingsKit::PathField;
    auto* change = new Button(QStringLiteral("Change..."), Button::Variant::Secondary, Button::Size::Small);
    auto* reset = new Button(QStringLiteral("Use default"), Button::Variant::Ghost, Button::Size::Small);
    ph->addWidget(path, 1);
    ph->addWidget(reset);
    ph->addWidget(change);
    sec.add(pathRow);
    auto* pathNote = new Label({}, Label::Style::Caption, Theme::Role::Text3);
    pathNote->setWordWrap(true);
    sec.add(pathNote);

    auto* size = new Label({}, Label::Style::SmallStrong);
    auto* open = new Button(QStringLiteral("Open folder"), Button::Variant::Secondary, Button::Size::Small);
    auto* clear = new Button(QStringLiteral("Clear cache..."), Button::Variant::DangerSecondary, Button::Size::Small);
    clear->setObjectName(QStringLiteral("clearShaderCache"));
    SettingsKit::Row* sizeRow = sec.row(QStringLiteral("Cache size"));
    sizeRow->addControl(size);
    sizeRow->addControl(open);
    sizeRow->addControl(clear);

    auto info = std::make_shared<ShaderCacheInfo>();
    auto bytes = std::make_shared<qint64>(-1);
    const QPointer<QWidget> page = sec.page;
    const auto refresh = [&s, info, bytes, page, path, pathNote, change, reset, size, sizeRow, clear] {
        *info = ShaderCacheInfo::readDefault();
        path->setPath(info->dxilDir);
        const bool fromEnv = LaunchConfig::shaderCacheDirFromEnvironment();
        change->setEnabled(!fromEnv);
        reset->setVisible(!fromEnv && !s.settings.shaderCacheDirOverride().isEmpty());
        pathNote->setText(fromEnv ? QStringLiteral("Set by XCL_DXIL_CACHE in the environment.") : QString());
        pathNote->setVisible(fromEnv);
        const QString pipelines = info->pipelines < 0 ? QStringLiteral("No pipelines recorded yet")
                                                      : QStringLiteral("%1 pipelines recorded").arg(Format::count(info->pipelines));
        const QString blobs = info->blobs < 0 ? QString() : QStringLiteral(", %1 driver blobs").arg(Format::count(info->blobs));
        sizeRow->setHelp(pipelines + blobs + QStringLiteral(", shared by all your games."));
        size->setText(info->exists ? QStringLiteral("Measuring...") : QStringLiteral("Empty"));
        const QString blocked = s.gate.isFree() ? QString() : s.gate.blockedReason(ProcessGate::Action::StartWarmup);
        clear->setEnabled(info->exists && blocked.isEmpty());
        clear->setToolTip(blocked);
        if (!info->exists)
            return;
        const QString dir = info->dxilDir;
        QThreadPool::globalInstance()->start([dir, page, size, bytes] {
            const qint64 n = folderBytes(dir);
            QMetaObject::invokeMethod(qApp, [page, size, bytes, n] {
                if (!page)
                    return;
                *bytes = n;
                size->setText(Format::bytes(n));
            });
        });
    };
    refresh();

    QObject::connect(open, &Button::clicked, sec.page, [info] {
        QDir().mkpath(info->dxilDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(info->dxilDir));
    });
    QObject::connect(change, &Button::clicked, sec.page, [&s, page, pathNote] {
        const QString dir = QFileDialog::getExistingDirectory(page->window(), QStringLiteral("Choose a folder for the shader cache"));
        if (dir.isEmpty())
            return;
        if (!isShaderCacheFolder(QDir(dir))) {
            pathNote->setText(QStringLiteral("Pick an empty folder or an existing XSEmu shader cache."));
            pathNote->show();
            return;
        }
        s.settings.setShaderCacheDirOverride(dir);
    });
    QObject::connect(reset, &Button::clicked, sec.page, [&s] { s.settings.setShaderCacheDirOverride({}); });
    QObject::connect(clear, &Button::clicked, sec.page, [&s, page, info, bytes, refresh] {
        auto* d = new Dialog(page->window(), 460);
        d->setIcon(QStringLiteral("trash"), int(Theme::Tone::Danger));
        d->setTitle(QStringLiteral("Clear the shader cache?"));
        const QString what = info->pipelines > 0 ? QStringLiteral("%1 pipelines").arg(Format::count(info->pipelines)) : QStringLiteral("every compiled shader");
        const QString amount = *bytes >= 0 ? QStringLiteral(" (%1)").arg(Format::bytes(*bytes)) : QString();
        auto* text = new Label(QStringLiteral("This deletes %1%2 for all your games. Each game stalls on its next run until you warm it up again.").arg(what, amount),
                               Label::Style::Body, Theme::Role::Text2);
        text->setWordWrap(true);
        d->body()->addWidget(text);
        auto* driver = new Label(QStringLiteral("The graphics driver's own cache isn't touched."), Label::Style::Small, Theme::Role::Text3);
        driver->setWordWrap(true);
        d->body()->addWidget(driver);
        auto* cancel = new Button(QStringLiteral("Cancel"), Button::Variant::Secondary, Button::Size::Large);
        auto* confirm = new Button(QStringLiteral("Clear cache"), Button::Variant::Danger, Button::Size::Large);
        d->addButton(cancel);
        d->addButton(confirm);
        QObject::connect(cancel, &Button::clicked, d, &Dialog::dismiss);
        QObject::connect(confirm, &Button::clicked, d, [&s, d, driver, info, refresh, confirm, cancel] {
            QString error;
            if (!s.gate.isFree())
                error = s.gate.blockedReason(ProcessGate::Action::StartWarmup);
            else if (const ProcessGate::External other = s.gate.external(); other.busy())
                error = other.reason();
            const ShaderCacheInfo::Cleared cleared = error.isEmpty() ? ShaderCacheInfo::clear(info->dxilDir) : ShaderCacheInfo::Cleared{};
            if (error.isEmpty())
                error = cleared.error;
            refresh();
            if (!error.isEmpty()) {
                driver->setText(error);
                driver->setRole(Theme::Role::Danger);
                return;
            }
            if (!cleared.kept.isEmpty()) {
                driver->setText(QStringLiteral("Cleared. These aren't XSEmu's, so they were kept: %1.").arg(cleared.kept.join(QStringLiteral(", "))));
                driver->setRole(Theme::Role::Text2);
                confirm->hide();
                cancel->setText(QStringLiteral("Close"));
                return;
            }
            d->dismiss();
        });
        d->open();
    });
    QObject::connect(&s.settings, &AppSettings::changed, sec.page, [refresh](const QString& key) {
        if (key == QLatin1String(AppSettings::Key::ShaderCacheDir) || key == QLatin1String(AppSettings::Key::RecentWarmups))
            refresh();
    });
    QObject::connect(&s.gate, &ProcessGate::changed, sec.page, refresh);
    SettingsKit::onShow(sec.page, refresh);
    sec.finish();
    return sec.page;
}
