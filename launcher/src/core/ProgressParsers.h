#pragma once

#include <QString>

#include <optional>

// Progress read from one line of a hidden process's output, for the install step and runner
// warm-ups. Each pre-cache step names its parser in RunnerSpec::PrecacheStep::parser.
//
//   if (auto u = ProgressParsers::parse(QStringLiteral("cargo"), line)) block->setValue(u->done, u->total);
namespace ProgressParsers {

struct Update {
    qint64 done = -1;  // -1: the line does not say
    qint64 total = -1;
    QString phase;     // "Compiling", "Receiving objects"; empty: unchanged
    QString detail;    // the crate, file or shader the line names; empty: none
    qint64 failed = -1; // failures so far, when the line says
    qint64 estimate = -1; // a likely total while the line gives none (the bar stays indeterminate)
};

// "git": git clone/fetch --progress ("Receiving objects:  45% (450/1000)").
// "cargo": cargo build ("   Compiling serde v1.0.200", "    Building [=====>  ] 120/300: x").
// "kelvaroq": "precompile-check N" (phase "check", total N) and "precompile done total failed".
// "victory-shaders", "victory-warm": VIctory 7900 XTX's two pre-cache steps.
// "coolfox-shaders": cool_fox's "shaders", as "victory-shaders" with its failure causes and an
// estimate of 25,985 (the title's shader count avatarmanz's README cites).
// "victory-dexnom": VIctory (DexNom)'s warm. Lines without a total give done with total -1.
// Any other id reads "123/456" or "45%" anywhere in the line.
std::optional<Update> parse(const QString& parser, const QString& line);

}
