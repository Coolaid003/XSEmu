#pragma once

#include "RunnerManager.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>

#include <functional>

class ProcessTree;
class QNetworkAccessManager;
class QNetworkReply;

// One install or update of a third-party runner, step by step, every process hidden:
//   release asset: download (SHA-256 checked) -> unpack -> swap into place
//   source:        toolchain bootstrap -> fetch the pinned commit -> build steps -> swap into place
// The new copy is assembled in <runner>\staging and only replaces <runner>\app when every step
// succeeded, so a cancelled or failed update leaves the installed version working.
class RunnerInstall : public QObject {
    Q_OBJECT
public:
    RunnerInstall(const RunnerSpec& spec, QObject* parent = nullptr);
    ~RunnerInstall() override;

    void start();
    void cancel();
    RunnerManager::Progress progress() const { return m_progress; }

    // The unpack command for an archive (Windows: the system tar.exe, which reads zip; elsewhere
    // unzip for .zip and tar for the rest), tested.
    static QStringList unpackCommand(const QString& archive, const QString& intoDir);

signals:
    void progressChanged();
    void finished(bool ok, const QString& error);

private:
    struct Step {
        QString name;
        std::function<void()> run;
    };
    void next();
    void fail(const QString& error);
    void setStep(const QString& detail = {});
    void download(const QString& url, const QString& to, const QString& sha256);
    void runProcess(const QStringList& command, const QString& workDir, const QString& parser);
    void ensureGit();
    void ensureMsvc();
    void ensureRust();
    void finishInstall(const QString& appDir);

    RunnerSpec m_spec;
    QString m_dir;
    QList<Step> m_steps;
    int m_index = -1;
    RunnerManager::Progress m_progress;
    QNetworkAccessManager* m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QPointer<ProcessTree> m_process;
    QProcessEnvironment m_env;
    QString m_lastLine;
    QString m_git = QStringLiteral("git");
    bool m_cancelled = false;
    bool m_done = false;
};
