#pragma once

#include "core/EmulatorController.h"

#include <functional>

class QWidget;
class Services;

// The error dialogs (Error states): why a launch was refused, and how a run ended when it ended
// badly. Each explains the cause in a sentence and offers the one action that helps.
namespace LaunchErrors {
void showLaunchProblem(QWidget* window, Services& services, const Game& game,
                       const EmulatorController::LaunchCheck& check, std::function<void()> retry);
void showExit(QWidget* window, const EmulatorController::ExitInfo& info, std::function<void()> playAgain);
}
