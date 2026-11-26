#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

class QWidget;
class Services;

// Importing a shared settings profile (SettingsProfile): parse the code or file, then a preview of
// exactly what would change (current value → new value) for the game or the launcher defaults, with
// the lines the launcher refuses and why, an Install button for a runner that isn't installed, and
// Apply to this game / Apply as defaults / Cancel. Malformed input gets an error dialog instead.
// titleId: the game the import was started from (preselected); empty picks the profile's own game
// when it is in the library.
namespace SettingsImport {

void fromText(QWidget* window, Services& s, const QByteArray& input, const QString& titleId = {});
void fromClipboard(QWidget* window, Services& s, const QString& titleId = {});
void fromFile(QWidget* window, Services& s, const QString& path, const QString& titleId = {});
void chooseFile(QWidget* window, Services& s, const QString& titleId = {});

bool isProfileFile(const QString& path); // *.xsemu-settings

// A *.xsemu-settings file dropped on the window, and Ctrl+Shift+V, import into currentTitleId()
// while enabled() says the launcher can take it (not in game, not in first launch).
void install(QWidget* window, Services& s, std::function<QString()> currentTitleId, std::function<bool()> enabled);

}
