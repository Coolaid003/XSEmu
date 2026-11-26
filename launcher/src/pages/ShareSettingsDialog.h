#pragma once

#include <QString>

class QWidget;
class Services;

namespace SettingsKit {
struct Section;
}

// "Share settings": a dialog with the profile's name and note, what it carries, the share code to
// copy and a Save file... button (SettingsProfile). The GPU line comes from the hardware snapshot
// Services already holds; nothing is read from the machine here.
namespace SettingsShare {

void openForGame(QWidget* window, Services& s, const QString& titleId);
void openForDefaults(QWidget* window, Services& s);

// Settings rows: "Share settings" (the launcher defaults) and "Import settings" (paste a code, open a file).
void addSettingsRows(const SettingsKit::Section& section, Services& s);

}
