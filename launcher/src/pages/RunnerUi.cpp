#include "RunnerUi.h"

#include "app/Services.h"
#include "core/Format.h"
#include "core/HardwareInfo.h"
#include "widgets/Badge.h"
#include "widgets/Button.h"
#include "widgets/Card.h"
#include "widgets/Dialog.h"
#include "widgets/InfoNote.h"
#include "widgets/Text.h"

#include <QDir>
#include <algorithm>
#include <QGridLayout>
#include <QLocale>
#include <QVBoxLayout>

namespace RunnerUi {

QString owner(const RunnerSpec& spec)
{
    return spec.isBuiltIn() ? QStringLiteral("built in") : QStringLiteral("by %1").arg(spec.repo.section(QLatin1Char('/'), 0, 0));
}

QString repoLine(const RunnerSpec& spec)
{
    QStringList parts{QStringLiteral("github.com/%1").arg(spec.repo)};
    if (spec.isBuiltIn())
        parts << QStringLiteral("launcher %1").arg(QStringLiteral(XSEMU_VERSION));
    else if (!spec.pinnedVersion().isEmpty())
        parts << QStringLiteral("pinned %1").arg(spec.pinnedVersion());
    if (!spec.licence.isEmpty())
        parts << spec.licence;
    return parts.join(QStringLiteral(" \u00B7 "));
}

QString provenance(const RunnerSpec& spec)
{
    if (spec.install == RunnerSpec::Install::BuildFromSource)
        return QStringLiteral("Community build, pinned version. Built from source on this PC.");
    return QStringLiteral("Community build, pinned release, checked against its SHA-256.");
}

Badge* vendorBadge(const RunnerSpec& spec)
{
    if (spec.badge.isEmpty())
        return nullptr;
    auto* b = new Badge(spec.badge, Theme::Tone::Neutral, Badge::Kind::Outline);
    b->setIcon(QStringLiteral("chip"));
    return b;
}

void updateStatusBadge(Badge* b, const RunnerManager::Status& st)
{
    using S = RunnerManager::State;
    b->setDotVisible(true);
    switch (st.state) {
    case S::BuiltIn:
    case S::Installed: b->setText(QStringLiteral("Installed")); b->setTone(Theme::Tone::Success); break;
    case S::UpdateAvailable: b->setText(QStringLiteral("Update available")); b->setTone(Theme::Tone::Accent); break;
    case S::NotInstalled: b->setText(QStringLiteral("Not installed")); b->setTone(Theme::Tone::Neutral); break;
    case S::Unavailable: b->setText(QStringLiteral("Not available on this PC")); b->setTone(Theme::Tone::Neutral); break;
    case S::Failed: b->setText(QStringLiteral("Install failed")); b->setTone(Theme::Tone::Danger); break;
    case S::Installing: {
        const QString verb = st.progress.phase == 1 && st.progress.step == QLatin1String("Building") ? QStringLiteral("Building") : QStringLiteral("Installing");
        const bool known = st.progress.total > 0 && st.progress.done >= 0;
        b->setText(known ? QStringLiteral("%1 \u00B7 %2%").arg(verb).arg(qRound(100.0 * double(st.progress.done) / double(st.progress.total))) : verb);
        b->setTone(Theme::Tone::Accent);
        break;
    }
    }
}

Badge* statusBadge(const RunnerManager::Status& status, bool)
{
    auto* b = new Badge;
    updateStatusBadge(b, status);
    return b;
}

QString statusDetail(Services& s, const RunnerSpec& spec, const RunnerManager::Status& st)
{
    using S = RunnerManager::State;
    switch (st.state) {
    case S::BuiltIn: return QStringLiteral("Built in. Can't be uninstalled.");
    case S::Installed:
    case S::UpdateAvailable: {
        const QDateTime at = s.runners.installedAt(spec.id);
        const QString when = at.isValid() ? QStringLiteral("Installed %1").arg(QLocale().toString(at.date(), QStringLiteral("d MMM"))) : QStringLiteral("Installed");
        if (st.state == S::UpdateAvailable)
            return QStringLiteral("%1 \u2192 %2").arg(st.installedVersion, st.pinnedVersion);
        const qint64 bytes = s.runners.installedBytes(spec.id);
        return bytes < 0 ? when : QStringLiteral("%1 \u00B7 %2").arg(when, Format::bytes(bytes));
    }
    case S::Installing: return QStringLiteral("Since %1").arg(QLocale().toString(st.progress.startedAt.time(), QLocale::ShortFormat));
    case S::NotInstalled:
        return spec.install == RunnerSpec::Install::BuildFromSource ? QStringLiteral("Builds from source on this PC; build time not measured yet.")
                                                                    : QStringLiteral("Downloads %1").arg(spec.assetName);
    case S::Unavailable: return spec.windowsOnly ? QStringLiteral("Runs on Windows only.") : QStringLiteral("No version is pinned yet.");
    case S::Failed: return st.error;
    }
    return {};
}

Runners::Recommendation recommendation(Services& s)
{
    const HardwareInfo& hw = s.hardware.info();
    return Runners::recommend(hw.gpuVendorId, hw.gpuName, hw.totalRamBytes, [&s](const QString& id) { return s.runners.isInstallable(id); });
}

QList<const RunnerSpec*> displayOrder(Services& s, const QString& first)
{
    const QString recommended = recommendation(s).runnerId;
    QList<const RunnerSpec*> order;
    for (const RunnerSpec& r : Runners::curated())
        order << &r;
    auto rank = [&](const RunnerSpec* r) {
        if (!first.isEmpty() && r->id == first)
            return 0;
        if (r->isBuiltIn())
            return 1;
        if (r->id == recommended)
            return 2;
        return s.runners.isRunnable(r->id) ? 3 : 4;
    };
    std::stable_sort(order.begin(), order.end(), [&](const RunnerSpec* a, const RunnerSpec* b) { return rank(a) < rank(b); });
    return order;
}

void confirmInstall(QWidget* window, Services& s, const QString& runnerId)
{
    const RunnerSpec* spec = Runners::find(runnerId);
    if (!spec)
        return;
    const bool build = spec->install == RunnerSpec::Install::BuildFromSource;
    const bool update = s.runners.status(runnerId).state == RunnerManager::State::UpdateAvailable;
    auto* d = new Dialog(window, 520);
    d->setIcon(QStringLiteral("download"), int(Theme::Tone::Accent));
    d->setTitle(QStringLiteral("%1 %2?").arg(update ? QStringLiteral("Update") : QStringLiteral("Install"), spec->name));
    auto* lead = new Label(build ? QStringLiteral("XSEmu downloads the pinned source and builds it on this PC. Nothing runs until the build finishes.")
                                 : QStringLiteral("XSEmu downloads the pinned release and checks it against its SHA-256 before anything runs."),
                           Label::Style::Small, Theme::Role::Text2);
    lead->setWordWrap(true);
    d->body()->addWidget(lead);
    d->body()->addSpacing(14);

    auto* table = new Card(Card::Kind::Sunken, Metrics::RadiusLg);
    auto* g = new QGridLayout(table);
    g->setContentsMargins(14, 12, 14, 12);
    g->setHorizontalSpacing(16);
    g->setVerticalSpacing(8);
    QList<QPair<QString, QString>> rows{{QStringLiteral("Source"), QStringLiteral("github.com/%1").arg(spec->repo)},
                                        {build ? QStringLiteral("Pinned commit") : QStringLiteral("Pinned release"), build ? spec->commit.left(7) : spec->releaseTag},
                                        {QStringLiteral("Licence"), spec->licence}};
    if (build)
        rows << qMakePair(QStringLiteral("Build time"), QStringLiteral("Not measured yet"));
    else
        rows << qMakePair(QStringLiteral("SHA-256"), spec->assetSha256.left(16) + QStringLiteral("\u2026"));
    rows << qMakePair(QStringLiteral("Installs to"), QDir::toNativeSeparators(spec->dataDir()));
    for (int i = 0; i < rows.size(); ++i) {
        g->addWidget(new Label(rows[i].first, Label::Style::Small, Theme::Role::Text3), i, 0);
        const bool code = i != 2 && rows[i].first != QLatin1String("Build time");
        auto* v = new Label(rows[i].second, code ? Label::Style::Mono : Label::Style::Small, Theme::Role::Text);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        g->addWidget(v, i, 1, Qt::AlignRight);
    }
    d->body()->addWidget(table);

    const HardwareInfo& hw = s.hardware.info();
    const Runners::Recommendation rec = recommendation(s);
    const RunnerSpec::Vendor have = hw.gpuVendorId == Runners::kVendorAmd ? RunnerSpec::Vendor::Amd
                                    : hw.gpuVendorId == Runners::kVendorNvidia ? RunnerSpec::Vendor::Nvidia
                                                                               : RunnerSpec::Vendor::Any;
    if (spec->recommendFor != RunnerSpec::Vendor::Any && spec->recommendFor != have) {
        const RunnerSpec* better = Runners::find(rec.runnerId);
        d->body()->addSpacing(12);
        d->body()->addWidget(new InfoNote(QStringLiteral("This runner is tuned for %1 GPUs. This PC has %2, so %3 is the better fit.")
                                              .arg(spec->recommendFor == RunnerSpec::Vendor::Amd ? QStringLiteral("AMD") : QStringLiteral("NVIDIA"),
                                                   hw.gpuNameOrUnknown(), better ? better->name : QStringLiteral("XSEmu")),
                                          Theme::Tone::Warning, QStringLiteral("warning")));
    }
    for (const QString& note : {build ? spec->buildNote : QString(), spec->note}) {
        if (note.isEmpty())
            continue;
        d->body()->addSpacing(10);
        auto* l = new Label(note, Label::Style::Caption, Theme::Role::Text3);
        l->setWordWrap(true);
        d->body()->addWidget(l);
    }
    auto* cancel = new Button(QStringLiteral("Cancel"), Button::Variant::Secondary, Button::Size::Small);
    QObject::connect(cancel, &Button::clicked, d, &Dialog::dismiss);
    d->addButton(cancel);
    auto* go = new Button(build ? QStringLiteral("Build and install") : QStringLiteral("Download and install"), Button::Variant::Primary, Button::Size::Small, QStringLiteral("download"));
    go->setObjectName(QStringLiteral("confirmInstall"));
    QObject::connect(go, &Button::clicked, d, [d, &s, runnerId] {
        s.runners.install(runnerId);
        d->dismiss();
    });
    d->addButton(go);
    d->open();
}

}
