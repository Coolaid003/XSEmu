#include "Sections.h"
#include "SettingsKit.h"

#include "app/Services.h"
#include "core/Paths.h"
#include "widgets/Button.h"
#include "widgets/Icon.h"
#include "widgets/Text.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QProcess>
#include <QUrl>
#include <QVBoxLayout>

// The launcher's version (XSEMU_VERSION), XSEmu.exe's own answer to --version, and the credits and
// licences: XCL by the xcl-dev contributors (GPL-2.0), the launcher (GPL-2.0), Qt (LGPL-3.0, linked
// dynamically), cover art from SteamGridDB with the user's own key.
QWidget* SettingsSections::about(Services& s)
{
    const SettingsKit::Section sec = SettingsKit::makeSection({}, {});
    auto* brand = new QWidget;
    auto* bh = new QHBoxLayout(brand);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->setSpacing(16);
    bh->addWidget(new BrandMark(56));
    auto* names = new QVBoxLayout;
    names->setSpacing(2);
    names->addStretch(1);
    names->addWidget(new Label(QStringLiteral("XSEmu"), Label::Style::SectionTitle));
    auto* build = new Label(QStringLiteral("launcher %1 · Qt %2").arg(QStringLiteral(XSEMU_VERSION), QString::fromLatin1(qVersion())), Label::Style::Mono, Theme::Role::Text2);
    build->setFont(Theme::mono(12));
    names->addWidget(build);
    names->addStretch(1);
    bh->addLayout(names, 1);
    sec.body->addWidget(brand);
    sec.body->addSpacing(4);

    sec.caps(QStringLiteral("Version"));
    auto* copy = new Button(QStringLiteral("Copy version"), Button::Variant::Secondary, Button::Size::Small);
    SettingsKit::Row* emu = sec.row(QStringLiteral("XSEmu.exe"), QStringLiteral("Asking XSEmu..."), copy);
    copy->setEnabled(false);
    const QString exe = s.settings.emulatorPath();
    if (!QFileInfo(exe).isExecutable()) {
        emu->setHelp(QStringLiteral("Not found at %1").arg(QDir::toNativeSeparators(exe)));
    } else {
        auto* p = new QProcess(emu);
        QObject::connect(p, &QProcess::finished, emu, [p, emu, copy](int code, QProcess::ExitStatus status) {
            const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
            const bool ok = status == QProcess::NormalExit && code == 0 && !out.isEmpty();
            emu->setHelp(ok ? out : QStringLiteral("XSEmu didn't report a version."));
            copy->setEnabled(ok);
            copy->setProperty("version", out);
            p->deleteLater();
        });
        QObject::connect(p, &QProcess::errorOccurred, emu, [emu](QProcess::ProcessError) { emu->setHelp(QStringLiteral("XSEmu didn't report a version.")); });
        p->start(exe, {QStringLiteral("--version")});
    }
    QObject::connect(copy, &Button::clicked, copy, [copy] {
        QGuiApplication::clipboard()->setText(QStringLiteral("%1, launcher %2").arg(copy->property("version").toString(), QStringLiteral(XSEMU_VERSION)));
    });
    sec.row(QStringLiteral("Launcher"), QStringLiteral("%1. New code under the GNU General Public License v2.").arg(QStringLiteral(XSEMU_VERSION)));

    sec.caps(QStringLiteral("Credits"));
    auto* credits = new Label(QStringLiteral("XSEmu is built on <b>XCL</b>, the Xbox Series compatibility layer by the xcl-dev contributors (GPL-2.0). "
                                             "The console model, system libraries and shader recompiler come from XCL."),
                              Label::Style::Body);
    credits->setTextFormat(Qt::RichText);
    credits->setWordWrap(true);
    sec.add(credits, 8);
    auto* licences = new Label(QStringLiteral("Like XCL, XSEmu and this launcher are licensed under the GNU General Public License v2. The launcher uses Qt under the "
                                              "GNU Lesser General Public License v3, linked dynamically. Cover art is fetched from SteamGridDB with your own key and "
                                              "cached on this PC; XSEmu ships no game art."),
                               Label::Style::Small, Theme::Role::Text2);
    licences->setWordWrap(true);
    sec.add(licences, 8);
    auto* links = new QWidget;
    auto* lh = new QHBoxLayout(links);
    lh->setContentsMargins(0, 16, 0, 0);
    lh->setSpacing(6);
    const QList<QPair<QString, QString>> urls = {
        {QStringLiteral("GPL-2.0 licence"), QStringLiteral("https://www.gnu.org/licenses/old-licenses/gpl-2.0.html")},
        {QStringLiteral("Qt licence (LGPL-3.0)"), QStringLiteral("https://www.gnu.org/licenses/lgpl-3.0.html")},
        {QStringLiteral("XCL source"), QStringLiteral("https://github.com/ivall/xcl")},
    };
    for (const auto& u : urls) {
        auto* b = new Button(u.first, Button::Variant::Secondary, Button::Size::Small, QStringLiteral("external"));
        const QUrl url(u.second);
        QObject::connect(b, &Button::clicked, b, [url] { QDesktopServices::openUrl(url); });
        lh->addWidget(b);
    }
    auto* logs = new Button(QStringLiteral("Open logs folder"), Button::Variant::Secondary, Button::Size::Small);
    QObject::connect(logs, &Button::clicked, logs, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::dataRoot())); });
    lh->addWidget(logs);
    lh->addStretch(1);
    sec.add(links);
    sec.finish();
    return sec.page;
}
