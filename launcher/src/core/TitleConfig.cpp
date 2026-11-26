#include "TitleConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

#include <cmath>
#include <limits>

namespace {

bool isTableHeader(const QString& line)
{
    return line.trimmed().startsWith(QLatin1Char('['));
}

bool isBareKeyChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-');
}

// "key = value" on one line: the key (bare or quoted) and the raw value text, or false.
bool splitKeyValue(const QString& line, QString* key, QString* rawValue)
{
    const QString t = line.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1Char('#')))
        return false;
    qsizetype i = 0;
    QString k;
    if (t.at(0) == QLatin1Char('"') || t.at(0) == QLatin1Char('\'')) {
        const QChar q = t.at(0);
        const qsizetype end = t.indexOf(q, 1);
        if (end < 0)
            return false;
        k = t.mid(1, end - 1);
        i = end + 1;
    } else {
        while (i < t.size() && isBareKeyChar(t.at(i)))
            ++i;
        k = t.left(i);
    }
    if (k.isEmpty())
        return false;
    while (i < t.size() && t.at(i).isSpace())
        ++i;
    if (i >= t.size() || t.at(i) != QLatin1Char('='))
        return false;
    *key = k;
    *rawValue = t.mid(i + 1).trimmed();
    return true;
}

QString stripComment(const QString& raw)
{
    bool inBasic = false, inLiteral = false;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (inBasic) {
            if (c == QLatin1Char('\\')) ++i;
            else if (c == QLatin1Char('"')) inBasic = false;
        } else if (inLiteral) {
            if (c == QLatin1Char('\'')) inLiteral = false;
        } else if (c == QLatin1Char('"')) inBasic = true;
        else if (c == QLatin1Char('\'')) inLiteral = true;
        else if (c == QLatin1Char('#')) return raw.left(i).trimmed();
    }
    return raw.trimmed();
}

bool parseBasicString(const QString& v, QString* out)
{
    if (v.size() < 2 || !v.startsWith(QLatin1Char('"')) || !v.endsWith(QLatin1Char('"')) || v.startsWith(QLatin1String("\"\"\"")))
        return false;
    QString s;
    for (qsizetype i = 1; i < v.size() - 1; ++i) {
        const QChar c = v.at(i);
        if (c != QLatin1Char('\\')) {
            s.append(c);
            continue;
        }
        if (++i >= v.size() - 1)
            return false;
        const QChar e = v.at(i);
        switch (e.unicode()) {
        case 'n': s.append(QLatin1Char('\n')); break;
        case 't': s.append(QLatin1Char('\t')); break;
        case 'r': s.append(QLatin1Char('\r')); break;
        case 'b': s.append(QLatin1Char('\b')); break;
        case 'f': s.append(QLatin1Char('\f')); break;
        case '"': s.append(QLatin1Char('"')); break;
        case '\\': s.append(QLatin1Char('\\')); break;
        case 'u':
        case 'U': {
            const int len = e == QLatin1Char('u') ? 4 : 8;
            bool ok = false;
            const uint code = v.mid(i + 1, len).toUInt(&ok, 16);
            if (!ok)
                return false;
            const char32_t ch = code;
            s.append(QString::fromUcs4(&ch, 1));
            i += len;
            break;
        }
        default:
            return false;
        }
    }
    *out = s;
    return true;
}

QVariant parseScalar(const QString& raw)
{
    const QString v = stripComment(raw);
    QString s;
    if (parseBasicString(v, &s))
        return s;
    if (v.size() >= 2 && v.startsWith(QLatin1Char('\'')) && v.endsWith(QLatin1Char('\'')) && !v.startsWith(QLatin1String("'''")))
        return v.mid(1, v.size() - 2);
    if (v == QLatin1String("true"))
        return true;
    if (v == QLatin1String("false"))
        return false;
    QString n = v;
    n.remove(QLatin1Char('_'));
    bool ok = false;
    if (n.startsWith(QLatin1String("0x"))) {
        const qlonglong h = n.mid(2).toLongLong(&ok, 16);
        if (ok) return h;
    }
    static const QRegularExpression intRe(QStringLiteral("^[+-]?\\d+$"));
    if (intRe.match(n).hasMatch()) {
        const qlonglong i = n.toLongLong(&ok);
        if (ok) return i;
    }
    if (n == QLatin1String("inf") || n == QLatin1String("+inf")) return std::numeric_limits<double>::infinity();
    if (n == QLatin1String("-inf")) return -std::numeric_limits<double>::infinity();
    if (n == QLatin1String("nan") || n == QLatin1String("+nan") || n == QLatin1String("-nan")) return std::nan("");
    static const QRegularExpression floatRe(QStringLiteral("^[+-]?\\d+(\\.\\d+)?([eE][+-]?\\d+)?$"));
    if (floatRe.match(n).hasMatch()) {
        const double d = n.toDouble(&ok);
        if (ok) return d;
    }
    return {};
}

QString formatKey(const QString& key)
{
    for (const QChar c : key) {
        if (!isBareKeyChar(c) || c.unicode() > 127)
            return TitleConfig::formatValue(key);
    }
    return key;
}

}

TitleConfig::TitleConfig(QString path)
    : m_path(std::move(path))
{
}

bool TitleConfig::exists() const
{
    return QFileInfo(m_path).isFile();
}

QVariantMap TitleConfig::values() const
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parse(QString::fromUtf8(f.readAll()));
}

QVariant TitleConfig::value(const QString& key, const QVariant& fallback) const
{
    return values().value(key, fallback);
}

bool TitleConfig::contains(const QString& key) const
{
    return values().contains(key);
}

bool TitleConfig::set(const QString& key, const QVariant& value)
{
    return set(QVariantMap{{key, value}});
}

bool TitleConfig::set(const QVariantMap& values)
{
    QString text;
    QFile in(m_path);
    if (in.exists()) {
        if (!in.open(QIODevice::ReadOnly))
            return false;
        text = QString::fromUtf8(in.readAll());
        in.close();
    }
    const QString out = apply(text, values);
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile save(m_path);
    if (!save.open(QIODevice::WriteOnly))
        return false;
    save.write(out.toUtf8());
    return save.commit();
}

bool TitleConfig::remove(const QStringList& keys)
{
    QFile in(m_path);
    if (!in.exists())
        return true;
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QString text = QString::fromUtf8(in.readAll());
    in.close();
    const QString out = without(text, keys);
    if (out == text)
        return true;
    QSaveFile save(m_path);
    if (!save.open(QIODevice::WriteOnly))
        return false;
    save.write(out.toUtf8());
    return save.commit();
}

QString TitleConfig::without(const QString& text, const QStringList& keys)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (isTableHeader(lines[i]))
            break;
        QString key, raw;
        if (splitKeyValue(lines[i], &key, &raw) && keys.contains(key))
            lines.removeAt(i--);
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantMap TitleConfig::parse(const QString& text)
{
    QVariantMap out;
    const QStringList lines = QString(text).replace(QLatin1String("\r\n"), QLatin1String("\n")).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (isTableHeader(line))
            break;
        QString key, raw;
        if (!splitKeyValue(line, &key, &raw))
            continue;
        const QVariant v = parseScalar(raw);
        if (v.isValid())
            out.insert(key, v);
    }
    return out;
}

QString TitleConfig::formatValue(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
        return value.toString();
    case QMetaType::Double:
    case QMetaType::Float: {
        const double d = value.toDouble();
        if (std::isnan(d)) return QStringLiteral("nan");
        if (std::isinf(d)) return d > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
        QString s = QString::number(d, 'g', 17);
        // shortest text that reads back as the same double
        for (int p = 1; p <= 17; ++p) {
            const QString t = QString::number(d, 'g', p);
            if (t.toDouble() == d) {
                s = t;
                break;
            }
        }
        if (!s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e')) && !s.contains(QLatin1Char('E')))
            s += QLatin1String(".0");
        return s;
    }
    default:
        break;
    }
    const QString s = value.toString();
    QString out = QStringLiteral("\"");
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '"': out += QLatin1String("\\\""); break;
        case '\\': out += QLatin1String("\\\\"); break;
        case '\n': out += QLatin1String("\\n"); break;
        case '\t': out += QLatin1String("\\t"); break;
        case '\r': out += QLatin1String("\\r"); break;
        default:
            if (c.unicode() < 0x20 || c.unicode() == 0x7F)
                out += QLatin1String("\\u") + QStringLiteral("%1").arg(c.unicode(), 4, 16, QLatin1Char('0')).toUpper();
            else
                out += c;
        }
    }
    out += QLatin1Char('"');
    return out;
}

QString TitleConfig::apply(const QString& input, const QVariantMap& values)
{
    // A file saved with Windows line endings keeps them: edit with \n, write back with \r\n.
    const bool crlf = input.contains(QLatin1String("\r\n"));
    QString text = input;
    if (crlf)
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    QStringList lines = text.isEmpty() ? QStringList() : text.split(QLatin1Char('\n'));
    const bool trailingNewline = text.isEmpty() || text.endsWith(QLatin1Char('\n'));
    if (trailingNewline && !lines.isEmpty())
        lines.removeLast();

    qsizetype firstTable = lines.size();
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (isTableHeader(lines[i])) {
            firstTable = i;
            break;
        }
    }

    QVariantMap pending = values;
    qsizetype lastKeyLine = -1;
    for (qsizetype i = 0; i < firstTable; ++i) {
        QString key, raw;
        if (!splitKeyValue(lines[i], &key, &raw))
            continue;
        lastKeyLine = i;
        const auto it = pending.find(key);
        if (it == pending.end())
            continue;
        const QString& line = lines[i];
        const QString lead = line.left(line.indexOf(line.trimmed()));
        lines[i] = lead + formatKey(key) + QLatin1String(" = ") + formatValue(it.value());
        pending.erase(it);
    }

    QStringList added;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        added << formatKey(it.key()) + QLatin1String(" = ") + formatValue(it.value());
    if (!added.isEmpty()) {
        qsizetype at;
        if (lastKeyLine >= 0) {
            at = lastKeyLine + 1;
        } else {
            at = firstTable;
            // keep a blank line between the new keys and a following table
            if (at < lines.size() && (at == 0 || !lines[at - 1].trimmed().isEmpty()))
                added << QString();
        }
        for (qsizetype k = 0; k < added.size(); ++k)
            lines.insert(at + k, added[k]);
    }

    const QString eol = crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QString out = lines.join(eol);
    if (trailingNewline)
        out += eol;
    return out;
}
