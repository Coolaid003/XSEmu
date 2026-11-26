#include "ProgressParsers.h"

#include <QRegularExpression>

namespace ProgressParsers {

namespace {

std::optional<Update> generic(const QString& line)
{
    static const QRegularExpression count(QStringLiteral("(\\d+)\\s*/\\s*(\\d+)"));
    static const QRegularExpression percent(QStringLiteral("(\\d{1,3})(?:\\.\\d+)?\\s*%"));
    if (const auto m = count.match(line); m.hasMatch() && m.captured(2).toLongLong() > 0)
        return Update{m.captured(1).toLongLong(), m.captured(2).toLongLong(), {}, {}};
    if (const auto m = percent.match(line); m.hasMatch())
        return Update{qMin<qint64>(100, m.captured(1).toLongLong()), 100, {}, {}};
    return std::nullopt;
}

std::optional<Update> git(const QString& line)
{
    static const QRegularExpression re(QStringLiteral("^(?:remote: )?([A-Za-z][A-Za-z ]+):\\s+\\d+% \\((\\d+)/(\\d+)\\)"));
    if (const auto m = re.match(line.trimmed()); m.hasMatch())
        return Update{m.captured(2).toLongLong(), m.captured(3).toLongLong(), m.captured(1), {}};
    return std::nullopt;
}

std::optional<Update> cargo(const QString& line)
{
    static const QRegularExpression building(QStringLiteral("^Building \\[[ =>-]*\\]\\s+(\\d+)/(\\d+)(?::\\s*(.*))?$"));
    static const QRegularExpression step(QStringLiteral("^(Compiling|Downloaded|Downloading|Finished|Updating|Fresh)\\s+(.*)$"));
    const QString t = line.trimmed();
    if (const auto m = building.match(t); m.hasMatch())
        return Update{m.captured(1).toLongLong(), m.captured(2).toLongLong(), QStringLiteral("Compiling"), m.captured(3).trimmed()};
    if (const auto m = step.match(t); m.hasMatch())
        return Update{-1, -1, m.captured(1), m.captured(2).section(QLatin1Char(' '), 0, 1)};
    return std::nullopt;
}

// VIctory 7900 XTX "shaders": "N shaders in the cache, S s" every 1000, then
// "N shaders in the cache, B do not translate, S s". There is no total.
std::optional<Update> victoryShaders(const QString& line)
{
    static const QRegularExpression tick(QStringLiteral("^(\\d+) shaders in the cache, (\\d+) s$"));
    static const QRegularExpression done(QStringLiteral("^(\\d+) shaders in the cache, (\\d+) do not translate, (\\d+) s$"));
    const QString t = line.trimmed();
    if (const auto m = tick.match(t); m.hasMatch())
        return Update{m.captured(1).toLongLong(), -1, QStringLiteral("Translating shaders"), {}};
    if (const auto m = done.match(t); m.hasMatch())
        return Update{m.captured(1).toLongLong(), m.captured(1).toLongLong(), QStringLiteral("Translating shaders"),
                      m.captured(2).toLongLong() > 0 ? QStringLiteral("%1 do not translate").arg(m.captured(2)) : QString()};
    return std::nullopt;
}

// VIctory 7900 XTX "warm --used": a header with how many pipelines are to make, "D of T, S s",
// and "pipeline N HASH: reason" for each failure.
std::optional<Update> victoryWarm(const QString& line)
{
    static const QRegularExpression header(QStringLiteral("^(\\d+) pipelines known, (\\d+) made before \\(([^)]*)\\), (\\d+) bad; (\\d+) to make, (\\d+) processes$"));
    static const QRegularExpression tick(QStringLiteral("^(\\d+) of (\\d+), (\\d+) s$"));
    static const QRegularExpression failed(QStringLiteral("^pipeline (\\d+) (\\S+): (.+)$"));
    const QString t = line.trimmed();
    if (const auto m = header.match(t); m.hasMatch())
        return Update{0, m.captured(5).toLongLong(), QStringLiteral("Building pipelines"), QStringLiteral("%1 made before").arg(m.captured(2))};
    if (const auto m = tick.match(t); m.hasMatch())
        return Update{m.captured(1).toLongLong(), m.captured(2).toLongLong(), QStringLiteral("Building pipelines"), {}};
    if (const auto m = failed.match(t); m.hasMatch())
        return Update{-1, -1, QStringLiteral("Building pipelines"), QStringLiteral("Pipeline %1 failed: %2").arg(m.captured(1), m.captured(3))};
    return std::nullopt;
}

// VIctory (DexNom) "warm": "  S s  N shaders" every 2000, then a summary line. There is no total.
std::optional<Update> victoryDexnom(const QString& line)
{
    static const QRegularExpression tick(QStringLiteral("^ *(\\d+) s  (\\d+) shaders$"));
    static const QRegularExpression summary(QStringLiteral("^warm {9}(\\d+) translated, (\\d+) refused; (\\d+) compute pipelines, (\\d+) refused; (\\d+) omitted by ray-tracing mode; (\\d+) s$"));
    if (const auto m = tick.match(line); m.hasMatch())
        return Update{m.captured(2).toLongLong(), -1, QStringLiteral("Translating shaders"), {}};
    if (const auto m = summary.match(line); m.hasMatch())
        return Update{m.captured(1).toLongLong(), m.captured(1).toLongLong(), QStringLiteral("Translating shaders"),
                      QStringLiteral("%1 translated, %2 compute pipelines").arg(m.captured(1), m.captured(3))};
    return std::nullopt;
}

// Kelvaroq: "precompile-check N" (pipelines waiting), then "precompile done total failed".
std::optional<Update> kelvaroq(const QString& line)
{
    static const QRegularExpression check(QStringLiteral("^precompile-check (\\d+)$"));
    static const QRegularExpression step(QStringLiteral("^precompile (\\d+) (\\d+) (\\d+)$"));
    const QString t = line.trimmed();
    if (const auto m = check.match(t); m.hasMatch())
        return Update{0, m.captured(1).toLongLong(), QStringLiteral("check"), {}};
    if (const auto m = step.match(t); m.hasMatch()) {
        const qint64 failed = m.captured(3).toLongLong();
        return Update{m.captured(1).toLongLong(), m.captured(2).toLongLong(), QStringLiteral("Compiling pipelines"),
                      failed > 0 ? QStringLiteral("%1 failed").arg(failed) : QString(), failed};
    }
    return std::nullopt;
}

}

std::optional<Update> parse(const QString& parser, const QString& line)
{
    if (parser == QLatin1String("kelvaroq"))
        return kelvaroq(line);
    if (parser == QLatin1String("victory-shaders"))
        return victoryShaders(line);
    if (parser == QLatin1String("victory-warm"))
        return victoryWarm(line);
    if (parser == QLatin1String("victory-dexnom"))
        return victoryDexnom(line);
    if (parser == QLatin1String("git")) {
        if (auto u = git(line))
            return u;
        return std::nullopt;
    }
    if (parser == QLatin1String("cargo")) {
        if (auto u = cargo(line))
            return u;
        return std::nullopt;
    }
    return generic(line);
}

}
