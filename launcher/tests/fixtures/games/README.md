Invented game folders for tests and screenshots. No real game, art, logo or trademark is used.

The repository's PR check refuses files named like game data (MicrosoftGame.config, *.exe), so every
such file is kept as text with a `.fixture` suffix and `DevFixtures::materialize()` copies the tree
under the real names before a test or a screenshot state uses it. The "executables" are one line of text.

- SignalGarden: Northwind Interactive, UTF-8.
- Coastline Drive: Harborlight Games, executable in a subfolder, a commented-out executable before it.
- Archive/Quiet: two levels down; publisher name with an XML entity.
- NoExe: the config names an executable that is not there (the "game files missing" path).
