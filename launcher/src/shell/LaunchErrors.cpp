#include "LaunchErrors.h"

#include "app/Services.h"
#include "core/Format.h"
#include "core/Paths.h"
#include "theme/Theme.h"
#include "widgets/Button.h"
#include "widgets/Card.h"
#include "widgets/Dialog.h"
#include "widgets/Text.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QVBoxLayout>

namespace {

Label* paragraph(const QString& text, Theme::Role role = Theme::Role::Text2, Label::Style style = Label::Style::Body)
{
    auto* l = new Label(text, style, role);
    l->setWordWrap(true);
    return l;
}

QWidget* pathBox(const QString& path)
{
    auto* box = new Card(Card::Kind::Sunken, Metrics::RadiusLg);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(12, 10, 12, 11);
    auto* l = new Label(QDir::toNativeSeparators(path), Label::Style::Mono, Theme::Role::Text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(l);
    return box;
}

Button* closeButton(Dialog* d, const QString& text = QStringLiteral("Close"))
{
    auto* b = new Button(text, Button::Variant::Secondary, Button::Size::Large);
    QObject::connect(b, &Button::clicked, d, &Dialog::dismiss);
    return b;
}

Button* actionButton(Dialog* d, const QString& text, std::function<void()> action, Button::Variant variant = Button::Variant::Primary)
{
    auto* b = new Button(text, variant, Button::Size::Large);
    QObject::connect(b, &Button::clicked, d, [d, action] {
        d->dismiss();
        if (action)
            action();
    });
    return b;
}

QString logsFolder(const Game& game)
{
    const QString name = game.identityName.isEmpty() ? QFileInfo(game.folder).fileName() : game.identityName;
    return Paths::logsDir(Paths::titleDirName(name));
}

}

void LaunchErrors::showLaunchProblem(QWidget* window, Services& s, const Game& game,
                                     const EmulatorController::LaunchCheck& check, std::function<void()> retry)
{
    using P = EmulatorController::LaunchCheck::Problem;
    auto* d = new Dialog(window);
    switch (check.problem) {
    case P::None:
        delete d;
        return;
    case P::GameFilesMissing:
        d->setIcon(QStringLiteral("folder"), int(Theme::Tone::Danger));
        d->setTitle(QStringLiteral("%1 can't start").arg(game.title()));
        d->body()->addWidget(paragraph(QStringLiteral("Its folder is gone or a drive is disconnected. XSEmu looked for the game here:")));
        d->body()->addWidget(pathBox(game.exe.isEmpty() ? game.folder : QDir(game.folder).filePath(game.exe)));
        d->body()->addWidget(paragraph(QStringLiteral("Reconnect the drive and try again. The shader cache stays either way."), Theme::Role::Text3, Label::Style::Small));
        d->addButton(closeButton(d));
        d->addButton(actionButton(d, QStringLiteral("Try again"), [&s, retry] {
            s.library.rescanNow();
            retry();
        }));
        break;
    case P::EmulatorMissing:
        d->setIcon(QStringLiteral("file"), int(Theme::Tone::Danger));
        d->setTitle(QStringLiteral("XSEmu.exe is missing"));
        d->body()->addWidget(paragraph(check.reason));
        d->body()->addWidget(pathBox(s.settings.emulatorPath()));
        d->body()->addWidget(paragraph(QStringLiteral("Keep XSEmu.exe next to the launcher, or reinstall XSEmu."), Theme::Role::Text3, Label::Style::Small));
        d->addButton(closeButton(d));
        break;
    case P::Busy:
        d->setIcon(QStringLiteral("lock"), int(Theme::Tone::Warning));
        d->setTitle(QStringLiteral("Stop %1 first").arg(s.gate.ownerTitle()));
        d->body()->addWidget(paragraph(check.reason));
        d->addButton(closeButton(d, QStringLiteral("Keep it")));
        d->addButton(actionButton(d, s.gate.stopActionLabel(), [&s] { s.gate.stopOwner(); }, Button::Variant::Danger));
        break;
    case P::OtherEmulatorRunning:
        d->setIcon(QStringLiteral("warning"), int(Theme::Tone::Warning));
        d->setTitle(QStringLiteral("Another XSEmu is running"));
        d->body()->addWidget(paragraph(check.reason));
        if (!check.processes.isEmpty())
            d->body()->addWidget(pathBox(check.processes.join(QLatin1Char('\n'))));
        d->body()->addWidget(paragraph(QStringLiteral("Only one XSEmu process runs at a time. Close it, then try again."), Theme::Role::Text3, Label::Style::Small));
        d->addButton(closeButton(d));
        d->addButton(actionButton(d, QStringLiteral("Try again"), retry));
        break;
    case P::NotEnoughRam:
        d->setIcon(QStringLiteral("chip"), int(Theme::Tone::Warning));
        d->setTitle(QStringLiteral("Not enough free RAM for %1").arg(game.title()));
        d->body()->addWidget(paragraph(check.reason));
        d->body()->addWidget(paragraph(QStringLiteral("Needs %1 · free now %2").arg(Format::gigabytesFromMb(check.needMb), Format::gigabytesFromMb(check.freeMb)), Theme::Role::Text, Label::Style::Number));
        d->body()->addWidget(paragraph(QStringLiteral("Close other programs, then try again."), Theme::Role::Text3, Label::Style::Small));
        d->addButton(closeButton(d));
        d->addButton(actionButton(d, QStringLiteral("Try again"), retry));
        break;
    }
    d->open();
}

void LaunchErrors::showExit(QWidget* window, const EmulatorController::ExitInfo& info, std::function<void()> playAgain)
{
    using K = EmulatorController::ExitInfo::Kind;
    if (info.kind == K::Stopped || info.kind == K::TitleExit)
        return;
    auto* d = new Dialog(window);
    const QString title = info.game.title();
    switch (info.kind) {
    case K::Crash:
        d->setIcon(QStringLiteral("warning"), int(Theme::Tone::Danger));
        d->setTitle(QStringLiteral("%1 crashed").arg(title));
        d->body()->addWidget(paragraph(info.message.isEmpty() ? QStringLiteral("XSEmu stopped unexpectedly.") : info.message));
        break;
    case K::DeviceRemoved:
        d->setIcon(QStringLiteral("monitor"), int(Theme::Tone::Danger));
        d->setTitle(QStringLiteral("The GPU stopped responding"));
        d->body()->addWidget(paragraph(QStringLiteral("Windows reset the graphics driver while %1 was running, so XSEmu had to stop.").arg(title)));
        if (!info.message.isEmpty())
            d->body()->addWidget(pathBox(info.message));
        break;
    case K::Error:
        d->setIcon(QStringLiteral("warning"), int(Theme::Tone::Danger));
        d->setTitle(QStringLiteral("%1 couldn't start").arg(title));
        d->body()->addWidget(paragraph(info.message.isEmpty() ? QStringLiteral("XSEmu reported an error while starting the game.") : info.message));
        break;
    case K::Stopped:
    case K::TitleExit:
        break;
    }
    d->body()->addWidget(paragraph(QStringLiteral("Exit code %1. The log and crash report are in the game's logs folder.").arg(Format::exitCode(info.exitCode)), Theme::Role::Text3, Label::Style::Small));
    auto* logs = new Button(QStringLiteral("Open logs"), Button::Variant::Ghost, Button::Size::Large, QStringLiteral("folder"));
    const QString folder = logsFolder(info.game);
    QObject::connect(logs, &Button::clicked, d, [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
    d->addButton(logs, true);
    d->addButton(closeButton(d));
    d->addButton(actionButton(d, QStringLiteral("Play again"), playAgain));
    d->open();
}
