#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

// Reads and writes the top-level scalar keys of a title's xcl.toml (crates/xcl/src/config.rs) and
// keeps every other line, comment and [table] exactly as it was. The emulator writes the file with
// toml::to_string_pretty, so every scalar is one "key = value" line before the first table header.
//
//   TitleConfig cfg(Paths::xclToml(game.titleId()));
//   QString device = cfg.value("device_type", "anaconda").toString();
//   cfg.set("device_type", "lockhart");   // written at once, atomically
class TitleConfig {
public:
    explicit TitleConfig(QString path);

    QString path() const { return m_path; }
    bool exists() const;

    // Top-level scalars: QString, bool, qlonglong or double. Arrays, inline tables and tables are not read.
    QVariantMap values() const;
    QVariant value(const QString& key, const QVariant& fallback = {}) const;
    bool contains(const QString& key) const;

    // Set one or more keys (bool, integer, double, string) and write the file. A missing file is created
    // with only these keys. Returns false and leaves the file untouched when it cannot be written.
    bool set(const QString& key, const QVariant& value);
    bool set(const QVariantMap& values);

    // Pure text transforms behind the above (unit-tested).
    static QVariantMap parse(const QString& text);
    static QString apply(const QString& text, const QVariantMap& values);
    static QString formatValue(const QVariant& value);

private:
    QString m_path;
};
