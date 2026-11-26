#include "Format.h"

#include <QLocale>

namespace Format {

QString gigabytes(qint64 b, int decimals)
{
    return QStringLiteral("%1 GB").arg(QString::number(double(b) / (1024.0 * 1024.0 * 1024.0), 'f', decimals));
}

QString gigabytesFromMb(qint64 mb, int decimals)
{
    return QStringLiteral("%1 GB").arg(QString::number(double(mb) / 1024.0, 'f', decimals));
}

QString bytes(qint64 b)
{
    if (b < 0)
        return QStringLiteral("Unknown");
    const double kb = double(b) / 1024.0;
    if (kb < 1024.0)
        return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 0));
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 0));
    return QStringLiteral("%1 GB").arg(QString::number(mb / 1024.0, 'f', 1));
}

QString count(qint64 n)
{
    QLocale l(QLocale::English, QLocale::UnitedStates);
    return l.toString(n);
}

QString duration(qint64 s)
{
    if (s < 0)
        return QStringLiteral("Unknown");
    if (s < 60)
        return QStringLiteral("%1 s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
}

QString shortDuration(double seconds)
{
    const qint64 s = qint64(seconds + 0.5);
    if (s < 60)
        return QStringLiteral("%1s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
}

QString playTime(qint64 s)
{
    if (s < 3600)
        return QStringLiteral("%1 min").arg(qMax<qint64>(s / 60, s > 0 ? 1 : 0));
    return QStringLiteral("%1 h").arg(s / 3600);
}

QString ago(const QDateTime& when, const QDateTime& now)
{
    if (!when.isValid())
        return QStringLiteral("Never");
    const qint64 s = when.secsTo(now);
    if (s < 60)
        return QStringLiteral("Just now");
    if (s < 3600)
        return QStringLiteral("%1 min ago").arg(s / 60);
    if (s < 24 * 3600 && when.date() == now.date())
        return QStringLiteral("%1 h ago").arg(s / 3600);
    const qint64 days = when.date().daysTo(now.date());
    if (days <= 1)
        return QStringLiteral("Yesterday");
    if (days < 30)
        return QStringLiteral("%1 days ago").arg(days);
    return QLocale(QLocale::English).toString(when.date(), QStringLiteral("d MMM yyyy"));
}

QString exitCode(int code)
{
    const quint32 u = static_cast<quint32>(code);
    if (u >= 0x80000000u) // NTSTATUS and HRESULT codes (0xC0000005, 0x887A0006) read in hex
        return QStringLiteral("0x%1").arg(u, 8, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x"));
    return QString::number(code);
}

QString exitCodeName(int code)
{
    switch (static_cast<quint32>(code)) {
    case 0xC0000005u: return QStringLiteral("access violation");
    case 0xC000001Du: return QStringLiteral("illegal instruction");
    case 0xC0000094u: return QStringLiteral("integer divide by zero");
    case 0xC00000FDu: return QStringLiteral("stack overflow");
    case 0xC0000409u: return QStringLiteral("stack buffer overrun");
    case 0xC0000374u: return QStringLiteral("heap corruption");
    case 0xC0000017u: return QStringLiteral("out of memory");
    case 0x887A0005u: return QStringLiteral("GPU device removed");
    case 0x887A0006u: return QStringLiteral("GPU device hung");
    case 0x887A0007u: return QStringLiteral("GPU device reset");
    case 0x887A0020u: return QStringLiteral("GPU driver internal error");
    default: return {};
    }
}

}
