#pragma once

#include <QDateTime>
#include <QString>

// Number and time formats shared by every page, so "8.6 GB", "1 min 00 s" and "2 h ago" read the
// same everywhere.
namespace Format {

QString gigabytes(qint64 bytes, int decimals = 1);     // "8.6 GB"
QString gigabytesFromMb(qint64 mb, int decimals = 1);  // "12.0 GB"
QString bytes(qint64 bytes);                           // "512 MB", "2.4 GB", "880 KB"
QString count(qint64 n);                               // "41,906"
QString duration(qint64 seconds);                      // "1 min 00 s", "3m 12s" style: see .cpp
QString shortDuration(double seconds);                 // "3m 12s", "22s", "1h 04m"
QString playTime(qint64 seconds);                      // "41 h played"-ready: "41 h", "25 min"
QString ago(const QDateTime& when, const QDateTime& now = QDateTime::currentDateTime()); // "2 h ago", "Yesterday"
QString exitCode(int code);                            // "3", "0xC0000005" for NTSTATUS-like codes

}
