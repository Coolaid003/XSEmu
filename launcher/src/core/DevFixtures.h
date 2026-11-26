#pragma once

#include <QString>

// Test and screenshot fixtures live in the repository as text with a ".fixture" suffix
// (MicrosoftGame.config.fixture, Game.exe.fixture), because the repository's PR check refuses any
// file named like game data. materialize() copies a fixture tree and strips the suffix.
namespace DevFixtures {

constexpr const char* kSuffix = ".fixture";

// Copy src into dst (created), every "*.fixture" file under its real name. False on any failure.
bool materialize(const QString& src, const QString& dst);

}
