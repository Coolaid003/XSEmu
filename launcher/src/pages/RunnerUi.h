#pragma once

#include "core/RunnerManager.h"

#include <QList>
#include <QString>

class Badge;
class QWidget;
class Services;

// Pieces every runner surface shares (Runners page, the "Runs with" picker, launch errors).
namespace RunnerUi {

QString owner(const RunnerSpec& spec);        // "avatarmanz"; "built in" for XSEmu
QString repoLine(const RunnerSpec& spec);     // "github.com/owner/name · pinned c9c932c · GPL-3.0"
QString provenance(const RunnerSpec& spec);   // "Community build, pinned version. Built from source on this PC."
Badge* vendorBadge(const RunnerSpec& spec);   // nullptr when the runner has no badge
Badge* statusBadge(const RunnerManager::Status& status, bool isDefault);
void updateStatusBadge(Badge* badge, const RunnerManager::Status& status);
// What the runner's card says under its status ("Installed 26 Nov · 186 MB", "Builds from source on this PC").
QString statusDetail(Services& s, const RunnerSpec& spec, const RunnerManager::Status& status);
// The recommendation for this PC, computed from the real GPU, RAM and install states.
Runners::Recommendation recommendation(Services& s);
// The curated runners in display order: `first` (an install in progress), XSEmu, the recommended one,
// installed ones, then the rest.
QList<const RunnerSpec*> displayOrder(Services& s, const QString& first = {});

// "Install Kelvaroq?": what will be downloaded or built, its pin, licence and size, a warning when
// it is tuned for another GPU vendor than this PC's, then Install.
void confirmInstall(QWidget* window, Services& s, const QString& runnerId);

// "Change where VIctory is kept": where it is now, the folder it moves to (a folder picker) and the
// free space there with a warning when it is low, Move or Rebuild there for an installed runner, and
// why it can't move now (installing, building, running). proposed pre-fills the new folder.
void changeLocation(QWidget* window, Services& s, const QString& runnerId, const QString& proposed = {});

}
