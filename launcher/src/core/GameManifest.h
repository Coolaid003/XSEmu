#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// MicrosoftGame.config, read the way the emulator reads it (crates/xcl/src/launcher.rs): UTF-8, or
// UTF-16 with a BOM; comments removed; attribute values found by plain text search on <Tag ...>.
namespace GameManifest {

// The text without <!-- --> comments, or a null string when it cannot be decoded.
QString decode(const QByteArray& raw);

// Values of attr on every <tag ...> element ("<Executable" does not match "<ExecutableList"; "Name="
// does not match "DisplayName="), XML character references replaced.
QStringList attrs(const QString& xml, const QString& tag, const QString& attr);
QString attr(const QString& xml, const QString& tag, const QString& attr);

// The file name used for the manifest in a folder (case-insensitive lookup), or empty.
QString findIn(const QString& folder);

}
