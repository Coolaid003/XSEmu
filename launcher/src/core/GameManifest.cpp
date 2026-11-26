#include "GameManifest.h"

#include <QDir>
#include <QStringDecoder>

namespace {

QString withoutComments(const QString& xml)
{
    QString out;
    out.reserve(xml.size());
    qsizetype from = 0;
    while (true) {
        const qsizetype i = xml.indexOf(QLatin1String("<!--"), from);
        if (i < 0)
            break;
        out.append(QStringView(xml).mid(from, i - from));
        const qsizetype j = xml.indexOf(QLatin1String("-->"), i + 4);
        if (j < 0)
            return out;
        from = j + 3;
    }
    out.append(QStringView(xml).mid(from));
    return out;
}

QString unescape(QStringView v)
{
    QString out;
    out.reserve(v.size());
    qsizetype i = 0;
    while (i < v.size()) {
        const QChar c = v.at(i);
        if (c != QLatin1Char('&')) {
            out.append(c);
            ++i;
            continue;
        }
        const qsizetype end = v.indexOf(QLatin1Char(';'), i);
        bool decoded = false;
        if (end > i) {
            const QStringView name = v.mid(i + 1, end - i - 1);
            char32_t ch = 0;
            if (name == u"amp") ch = '&';
            else if (name == u"lt") ch = '<';
            else if (name == u"gt") ch = '>';
            else if (name == u"quot") ch = '"';
            else if (name == u"apos") ch = '\'';
            else if (name.startsWith(u"#x")) {
                bool ok = false;
                const uint n = name.mid(2).toUInt(&ok, 16);
                if (ok) ch = n;
            } else if (name.startsWith(u"#")) {
                bool ok = false;
                const uint n = name.mid(1).toUInt(&ok, 10);
                if (ok) ch = n;
            }
            if (ch != 0 && ch <= 0x10FFFF && !(ch >= 0xD800 && ch <= 0xDFFF)) {
                out.append(QString::fromUcs4(&ch, 1));
                i = end + 1;
                decoded = true;
            }
        }
        if (!decoded) {
            out.append(QLatin1Char('&'));
            ++i;
        }
    }
    return out;
}

}

namespace GameManifest {

QString decode(const QByteArray& raw)
{
    QString text;
    if (raw.startsWith("\xFF\xFE") || raw.startsWith("\xFE\xFF")) {
        const bool le = static_cast<unsigned char>(raw.at(0)) == 0xFF;
        const QByteArray body = raw.mid(2);
        if (body.size() % 2 != 0)
            return {};
        QStringDecoder dec(le ? QStringDecoder::Utf16LE : QStringDecoder::Utf16BE);
        text = dec.decode(body);
        if (dec.hasError())
            return {};
    } else {
        QStringDecoder dec(QStringDecoder::Utf8);
        text = dec.decode(raw);
        if (dec.hasError())
            return {};
    }
    if (text.isNull())
        text = QStringLiteral("");
    return withoutComments(text);
}

QStringList attrs(const QString& xml, const QString& tag, const QString& attr)
{
    const QString open = QLatin1Char('<') + tag;
    const QString key = attr + QLatin1String("=\"");
    QStringList out;
    qsizetype from = 0;
    while (true) {
        const qsizetype i = xml.indexOf(open, from);
        if (i < 0)
            break;
        from = i + open.size();
        if (from >= xml.size())
            break;
        const QChar next = xml.at(from);
        if (!(next.isSpace() || next == QLatin1Char('/') || next == QLatin1Char('>')))
            continue;
        const qsizetype end = xml.indexOf(QLatin1Char('>'), from);
        if (end < 0)
            break;
        const QStringView t = QStringView(xml).mid(from, end - from);
        qsizetype pos = 0;
        while (true) {
            const qsizetype k = t.indexOf(key, pos);
            if (k < 0)
                break;
            if (k == 0 || t.at(k - 1).isSpace()) {
                const qsizetype a = k + key.size();
                const qsizetype b = t.indexOf(QLatin1Char('"'), a);
                if (b >= 0)
                    out.append(unescape(t.mid(a, b - a)));
                break;
            }
            pos = k + key.size();
        }
    }
    return out;
}

QString attr(const QString& xml, const QString& tag, const QString& a)
{
    const QStringList all = attrs(xml, tag, a);
    return all.isEmpty() ? QString() : all.first();
}

QString findIn(const QString& folder)
{
    const QDir dir(folder);
    const QStringList files = dir.entryList({QStringLiteral("*.config"), QStringLiteral("*.Config"), QStringLiteral("*.CONFIG")}, QDir::Files);
    for (const QString& f : files) {
        if (f.compare(QLatin1String("MicrosoftGame.config"), Qt::CaseInsensitive) == 0)
            return dir.filePath(f);
    }
    return {};
}

}
