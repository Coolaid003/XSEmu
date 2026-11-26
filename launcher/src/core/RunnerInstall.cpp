#include "RunnerInstall.h"

#include "Paths.h"
#include "ProcessTree.h"
#include "ProgressParsers.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>

namespace {

QString toolchainsDir() { return QDir(Paths::runnersDir()).filePath(QStringLiteral("_toolchains")); }

QString exe(const QString& name)
{
#ifdef Q_OS_WIN
    return name + QStringLiteral(".exe");
#else
    return name;
#endif
}

}

RunnerInstall::RunnerInstall(const RunnerSpec& spec, QObject* parent)
    : QObject(parent)
    , m_spec(spec)
    , m_dir(Paths::runnerDir(spec.id))
    , m_net(new QNetworkAccessManager(this))
    , m_env(QProcessEnvironment::systemEnvironment())
{
    const QString staging = QDir(m_dir).filePath(QStringLiteral("staging"));
    if (spec.install == RunnerSpec::Install::ReleaseAsset) {
        const QString archive = QDir(m_dir).filePath(QStringLiteral("download/") + spec.assetName);
        m_steps << Step{QStringLiteral("Downloading"), [this, archive] { download(m_spec.downloadUrl(), archive, m_spec.assetSha256); }};
        m_steps << Step{QStringLiteral("Unpacking"), [this, archive, staging] {
                            QDir(staging).removeRecursively();
                            QDir().mkpath(staging);
                            runProcess(unpackCommand(archive, staging), m_dir, {});
                        }};
        m_steps << Step{QStringLiteral("Finishing"), [this, staging] { finishInstall(staging); }};
    } else {
        for (const QString& tool : spec.toolchains) {
            if (tool == QLatin1String("git"))
                m_steps << Step{QStringLiteral("Checking Git"), [this] { ensureGit(); }};
            else if (tool == QLatin1String("msvc"))
                m_steps << Step{QStringLiteral("Checking the C++ build tools"), [this] { ensureMsvc(); }};
            else if (tool == QLatin1String("rust"))
                m_steps << Step{QStringLiteral("Setting up Rust"), [this] { ensureRust(); }};
        }
        const QString src = m_spec.checkoutDir();
        m_steps << Step{QStringLiteral("Fetching the source"), [this, src] {
                            QDir().mkpath(src);
                            if (QFileInfo::exists(QDir(src).filePath(QStringLiteral(".git")))) {
                                next();
                                return;
                            }
                            runProcess({m_git, QStringLiteral("-c"), QStringLiteral("init.defaultBranch=main"), QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral(".")}, src, {});
                        }};
        m_steps << Step{QStringLiteral("Fetching the source"), [this, src] {
                            runProcess({m_git, QStringLiteral("fetch"), QStringLiteral("--progress"), QStringLiteral("--depth"), QStringLiteral("1"), m_spec.cloneUrl(), m_spec.commit},
                                       src, QStringLiteral("git"));
                        }};
        m_steps << Step{QStringLiteral("Fetching the source"), [this, src] {
                            runProcess({m_git, QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--force"), QStringLiteral("--detach"), m_spec.commit}, src, {});
                        }};
        for (const RunnerSpec::Fetch& fetch : spec.fetches)
            m_steps << Step{QStringLiteral("Fetching the source"), [this, src, fetch] { download(fetch.url, QDir(src).filePath(fetch.to), fetch.sha256); }};
        for (const QStringList& cmd : spec.buildSteps)
            m_steps << Step{QStringLiteral("Building"), [this, cmd, src] {
                                QDir().mkpath(m_spec.targetDir());
                                m_env.insert(QStringLiteral("CARGO_TARGET_DIR"), QDir::toNativeSeparators(m_spec.targetDir()));
                                runProcess(RunnerSpec::expand(cmd, {{QStringLiteral("targetDir"), m_spec.targetDir()}}), src, QStringLiteral("cargo"));
                            }};
        m_steps << Step{QStringLiteral("Finishing"), [this, staging] {
                            QDir(staging).removeRecursively();
                            QDir().mkpath(staging);
                            for (const auto& [built, name] : m_spec.artifacts) {
                                if (!QFile::copy(QDir(m_spec.targetDir()).filePath(built), QDir(staging).filePath(name))) {
                                    fail(QStringLiteral("The build finished but %1 is missing.").arg(built));
                                    return;
                                }
                            }
                            finishInstall(staging);
                        }};
    }
}

RunnerInstall::~RunnerInstall()
{
    if (m_reply)
        m_reply->abort();
}

QStringList RunnerInstall::unpackCommand(const QString& archive, const QString& intoDir)
{
#ifdef Q_OS_WIN
    const QString tar = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))).filePath(QStringLiteral("System32/tar.exe"));
    return {QDir::toNativeSeparators(tar), QStringLiteral("-xf"), QDir::toNativeSeparators(archive), QStringLiteral("-C"), QDir::toNativeSeparators(intoDir)};
#else
    if (archive.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        return {QStringLiteral("unzip"), QStringLiteral("-q"), QStringLiteral("-o"), archive, QStringLiteral("-d"), intoDir};
    return {QStringLiteral("tar"), QStringLiteral("-xf"), archive, QStringLiteral("-C"), intoDir};
#endif
}

void RunnerInstall::start()
{
    QDir().mkpath(m_dir);
    next();
}

void RunnerInstall::cancel()
{
    if (m_done)
        return;
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();
    if (m_process)
        m_process->killTree();
    fail(QStringLiteral("Cancelled."));
}

void RunnerInstall::next()
{
    if (m_done || m_cancelled)
        return;
    ++m_index;
    if (m_index >= m_steps.size()) {
        m_done = true;
        emit finished(true, {});
        return;
    }
    m_progress = {};
    m_progress.step = m_steps[m_index].name;
    m_progress.stepIndex = m_index + 1;
    m_progress.stepCount = int(m_steps.size());
    emit progressChanged();
    m_steps[m_index].run();
}

void RunnerInstall::fail(const QString& error)
{
    if (m_done)
        return;
    m_done = true;
    if (m_process && m_process->state() != QProcess::NotRunning)
        m_process->killTree();
    m_file.close();
    QDir(QDir(m_dir).filePath(QStringLiteral("staging"))).removeRecursively();
    emit finished(false, error);
}

void RunnerInstall::setStep(const QString& detail)
{
    m_progress.detail = detail;
    emit progressChanged();
}

void RunnerInstall::download(const QString& url, const QString& to, const QString& sha256)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    m_file.setFileName(to + QStringLiteral(".part"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(m_file.fileName()), m_file.errorString()));
        return;
    }
    m_hash.reset();
    m_progress.bytes = true;
    setStep(QUrl(url).fileName());
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("XSEmuLauncher"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = m_reply->readAll();
        m_hash.addData(chunk);
        m_file.write(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress.done = got;
        m_progress.total = total > 0 ? total : -1;
        emit progressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, to, sha256, url] {
        QNetworkReply* reply = m_reply;
        m_reply.clear();
        reply->deleteLater();
        if (m_cancelled || m_done)
            return;
        const QByteArray rest = reply->readAll();
        m_hash.addData(rest);
        m_file.write(rest);
        m_file.close();
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("The download from %1 failed: %2").arg(QUrl(url).host().isEmpty() ? url : QUrl(url).host(), reply->errorString()));
            return;
        }
        const QString got = QString::fromLatin1(m_hash.result().toHex());
        if (!sha256.isEmpty() && got != sha256.toLower()) {
            QFile::remove(m_file.fileName());
            fail(QStringLiteral("The download did not match the pinned SHA-256 (expected %1, got %2), so it was deleted.").arg(sha256.left(12), got.left(12)));
            return;
        }
        QFile::remove(to);
        if (!QFile::rename(m_file.fileName(), to)) {
            fail(QStringLiteral("Cannot move the download into place."));
            return;
        }
        next();
    });
}

void RunnerInstall::runProcess(const QStringList& command, const QString& workDir, const QString& parser)
{
    m_process = new ProcessTree(this);
    m_process->setWorkingDirectory(workDir);
    m_process->setProcessEnvironment(m_env);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_progress.bytes = false;
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this, parser] {
        // git and cargo redraw progress with \r; treat it as a line end
        const QString text = QString::fromUtf8(m_process->readAllStandardOutput()).replace(QLatin1Char('\r'), QLatin1Char('\n'));
        for (const QString& raw : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString line = raw.trimmed();
            if (line.isEmpty())
                continue;
            m_lastLine = line;
            if (!parser.isEmpty()) {
                if (const auto u = ProgressParsers::parse(parser, line)) {
                    if (u->total > 0) {
                        m_progress.done = u->done;
                        m_progress.total = u->total;
                    }
                    if (!u->detail.isEmpty())
                        m_progress.detail = u->detail;
                    else if (!u->phase.isEmpty())
                        m_progress.detail = u->phase;
                    emit progressChanged();
                    continue;
                }
            }
            setStep(line.left(120));
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, command](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(QStringLiteral("Could not start %1.").arg(QFileInfo(command.value(0)).fileName()));
    });
    connect(m_process, &QProcess::finished, this, [this, command](int code, QProcess::ExitStatus status) {
        m_process->deleteLater();
        if (m_cancelled || m_done)
            return;
        if (status != QProcess::NormalExit || code != 0) {
            fail(QStringLiteral("%1 stopped with exit code %2%3").arg(QFileInfo(command.value(0)).fileName()).arg(code)
                     .arg(m_lastLine.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + m_lastLine.left(200)));
            return;
        }
        next();
    });
    m_process->startTree(command.value(0), command.mid(1));
}

void RunnerInstall::ensureGit()
{
    m_git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (!m_git.isEmpty()) {
        next();
        return;
    }
#ifdef Q_OS_WIN
    const QString winget = QStandardPaths::findExecutable(QStringLiteral("winget"));
    if (!winget.isEmpty()) {
        m_steps.insert(m_index + 1, Step{QStringLiteral("Checking Git"), [this] {
                                            const QString programs = qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files"));
                                            m_git = QDir(programs).filePath(QStringLiteral("Git/cmd/git.exe"));
                                            if (QFileInfo::exists(m_git))
                                                next();
                                            else
                                                fail(QStringLiteral("Git was installed but cannot be found; restart the launcher and try again."));
                                        }});
        setStep(QStringLiteral("Installing Git with winget"));
        runProcess({winget, QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("Git.Git"), QStringLiteral("-e"), QStringLiteral("--silent"),
                    QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")},
                   m_dir, {});
        return;
    }
#endif
    fail(QStringLiteral("%1 is built from source and needs Git. Install Git, then try again.").arg(m_spec.name));
}

void RunnerInstall::ensureMsvc()
{
#ifdef Q_OS_WIN
    const QString programsX86 = qEnvironmentVariable("ProgramFiles(x86)", QStringLiteral("C:/Program Files (x86)"));
    const QString vswhere = QDir(programsX86).filePath(QStringLiteral("Microsoft Visual Studio/Installer/vswhere.exe"));
    auto installed = [vswhere] {
        if (!QFileInfo::exists(vswhere))
            return false;
        QProcess p;
        p.start(vswhere, {QStringLiteral("-products"), QStringLiteral("*"), QStringLiteral("-requires"),
                          QStringLiteral("Microsoft.VisualStudio.Component.VC.Tools.x86.x64"), QStringLiteral("-property"), QStringLiteral("installationPath")});
        return p.waitForFinished(15000) && !p.readAllStandardOutput().trimmed().isEmpty();
    };
    if (installed()) {
        next();
        return;
    }
    const QString winget = QStandardPaths::findExecutable(QStringLiteral("winget"));
    if (winget.isEmpty()) {
        fail(QStringLiteral("%1 needs the Visual Studio C++ build tools. Install \"Desktop development with C++\" from Visual Studio Build Tools, then try again.").arg(m_spec.name));
        return;
    }
    // Windows may ask once for permission to install them
    setStep(QStringLiteral("Installing the Visual Studio C++ build tools (Windows may ask for permission)"));
    runProcess({winget, QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("Microsoft.VisualStudio.2022.BuildTools"), QStringLiteral("-e"), QStringLiteral("--silent"),
                QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements"), QStringLiteral("--override"),
                QStringLiteral("--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended")},
               m_dir, {});
#else
    next();
#endif
}

void RunnerInstall::ensureRust()
{
    const QString home = toolchainsDir();
    const QString cargoHome = QDir(home).filePath(QStringLiteral("cargo"));
    m_env.insert(QStringLiteral("CARGO_HOME"), QDir::toNativeSeparators(cargoHome));
    m_env.insert(QStringLiteral("RUSTUP_HOME"), QDir::toNativeSeparators(QDir(home).filePath(QStringLiteral("rustup"))));
    const QString bin = QDir(cargoHome).filePath(QStringLiteral("bin"));
    m_env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(bin) + QDir::listSeparator() + m_env.value(QStringLiteral("PATH")));
    if (QFileInfo::exists(QDir(bin).filePath(exe(QStringLiteral("cargo")))) || !QStandardPaths::findExecutable(QStringLiteral("cargo")).isEmpty()) {
        next();
        return;
    }
#ifdef Q_OS_WIN
    const QString triple = QStringLiteral("x86_64-pc-windows-msvc");
#else
    const QString triple = QStringLiteral("x86_64-unknown-linux-gnu");
#endif
    const QString url = QStringLiteral("https://static.rust-lang.org/rustup/dist/%1/%2").arg(triple, exe(QStringLiteral("rustup-init")));
    const QString to = QDir(home).filePath(exe(QStringLiteral("rustup-init")));
    // rustup publishes the SHA-256 of rustup-init next to it
    QNetworkReply* sum = m_net->get(QNetworkRequest(QUrl(url + QStringLiteral(".sha256"))));
    connect(sum, &QNetworkReply::finished, this, [this, sum, url, to] {
        sum->deleteLater();
        if (m_cancelled || m_done)
            return;
        const QString sha = QString::fromLatin1(sum->readAll()).section(QLatin1Char(' '), 0, 0).trimmed();
        if (sum->error() != QNetworkReply::NoError || sha.size() != 64) {
            fail(QStringLiteral("Could not read the checksum of the Rust installer: %1").arg(sum->errorString()));
            return;
        }
        // run rustup-init once the download is in place: insert it as the next step
        m_steps.insert(m_index + 1, Step{QStringLiteral("Setting up Rust"), [this, to] {
                                            QFile::setPermissions(to, QFile::permissions(to) | QFile::ExeOwner);
                                            runProcess({to, QStringLiteral("-y"), QStringLiteral("--no-modify-path"), QStringLiteral("--profile"), QStringLiteral("minimal"),
                                                        QStringLiteral("--default-toolchain"), QStringLiteral("stable")},
                                                       toolchainsDir(), {});
                                        }});
        download(url, to, sha);
    });
}

void RunnerInstall::finishInstall(const QString& stagingDir)
{
    const QString app = m_spec.installDir();
    QString executable = m_spec.executable;
    QString root = stagingDir;
    if (!executable.isEmpty() && !QFileInfo::exists(QDir(stagingDir).filePath(executable))) {
        // a release zip often wraps everything in one top-level folder: look one level down
        const QStringList subdirs = QDir(stagingDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (subdirs.size() == 1 && QFileInfo::exists(QDir(stagingDir).filePath(subdirs.first() + QLatin1Char('/') + executable))) {
            root = QDir(stagingDir).filePath(subdirs.first());
            executable = subdirs.first() + QLatin1Char('/') + executable;
        } else {
            fail(QStringLiteral("%1 was unpacked but %2 is not in it.").arg(m_spec.name, executable));
            return;
        }
    }
    for (const auto& [file, sha] : m_spec.fileSha256) {
        QFile f(QDir(root).filePath(file));
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (!f.open(QIODevice::ReadOnly) || !h.addData(&f) || QString::fromLatin1(h.result().toHex()) != sha) {
            fail(QStringLiteral("%1 in the download does not match the pinned SHA-256, so it was not installed.").arg(file));
            return;
        }
    }
    for (const auto& [from, to] : m_spec.copyFiles) {
        const QString dst = QDir(root).filePath(to);
        QFile::remove(dst);
        if (!QFile::copy(QDir(root).filePath(from), dst)) {
            fail(QStringLiteral("Cannot copy %1 to %2 inside %3.").arg(from, to, m_spec.name));
            return;
        }
    }
    const QString old = app + QStringLiteral(".old");
    QDir(old).removeRecursively();
    if (QFileInfo::exists(app) && !QDir().rename(app, old)) {
        fail(QStringLiteral("The installed %1 is in use; stop it and try again.").arg(m_spec.name));
        return;
    }
    if (!QDir().rename(stagingDir, app)) {
        QDir().rename(old, app);
        fail(QStringLiteral("Cannot move %1 into place.").arg(m_spec.name));
        return;
    }
    QDir(old).removeRecursively();
    QFile::setPermissions(QDir(app).filePath(executable), QFile::permissions(QDir(app).filePath(executable)) | QFile::ExeOwner);
    QFile f(QDir(m_dir).filePath(QStringLiteral("runner.json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("id"), m_spec.id},
                                          {QStringLiteral("version"), m_spec.pinnedVersion()},
                                          {QStringLiteral("executable"), executable},
                                          {QStringLiteral("installedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}})
                    .toJson());
    }
    QDir(QDir(m_dir).filePath(QStringLiteral("download"))).removeRecursively();
    next();
}
