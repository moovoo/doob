#include "metatransform.h"

#include <algorithm>
#include <array>
#include <functional>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

// ---------------------------------------------------------------------------
// Parsing
//

struct OpCall
{
    QString name;
    QStringList args;
};

struct Parsed
{
    QString macro;
    QList<OpCall> ops;
    // A literal value in place of a macro ("My album" / My album).
    bool isLiteral = false;
    QString literalValue;
};

void skipSpace(const QString &s, int &pos)
{
    while (pos < s.size() && (s.at(pos) == QLatin1Char(' ') || s.at(pos) == QLatin1Char('\t')))
        ++pos;
}

// A macro: %name% (the name may be empty — rejected by the caller).
bool parseMacro(const QString &s, int &pos, QString &name, QString &error)
{
    if (pos >= s.size() || s.at(pos) != QLatin1Char('%'))
    {
        error = QStringLiteral("expected a macro (%name) at the start");
        return false;
    }
    ++pos;
    const int end = static_cast<int>(s.indexOf(QLatin1Char('%'), pos));
    if (end < 0)
    {
        error = QStringLiteral("unterminated macro (missing a closing %)");
        return false;
    }
    name = s.mid(pos, end - pos);
    pos = end + 1;
    return true;
}

// An identifier: [A-Za-z_][A-Za-z0-9_]*
bool parseName(const QString &s, int &pos, QString &name, QString &error)
{
    if (pos >= s.size() || !(s.at(pos).isLetterOrNumber() || s.at(pos) == QLatin1Char('_')))
    {
        error = QStringLiteral("expected an op name");
        return false;
    }
    int end = pos;
    while (end < s.size() && (s.at(end).isLetterOrNumber() || s.at(end) == QLatin1Char('_')))
        ++end;
    name = s.mid(pos, end - pos);
    pos = end;
    return true;
}

// One argument: a double-quoted string (\" and \\ are the escapes) or a
// bare token (up to a comma or the closing paren, as-is).
bool parseArg(const QString &s, int &pos, QString &out, QString &error)
{
    skipSpace(s, pos);
    if (pos >= s.size())
    {
        error = QStringLiteral("unterminated argument list");
        return false;
    }
    if (s.at(pos) == QLatin1Char('"'))
    {
        ++pos;
        QString value;
        bool closed = false;
        while (pos < s.size())
        {
            const QChar c = s.at(pos);
            if (c == QLatin1Char('\\') && pos + 1 < s.size())
            {
                value.append(s.at(pos + 1));
                pos += 2;
                continue;
            }
            if (c == QLatin1Char('"'))
            {
                closed = true;
                ++pos;
                break;
            }
            value.append(c);
            ++pos;
        }
        if (!closed)
        {
            error = QStringLiteral("unterminated quoted argument (missing a \")");
            return false;
        }
        out = std::move(value);
        return true;
    }
    int end = pos;
    while (end < s.size() && s.at(end) != QLatin1Char(',') && s.at(end) != QLatin1Char(')'))
        ++end;
    out = s.mid(pos, end - pos);
    pos = end;
    return true;
}

// op(args): the name, the opening paren, the comma-separated args, the
// closing paren.
bool parseOpCall(const QString &s, int &pos, OpCall &out, QString &error)
{
    skipSpace(s, pos);
    if (!parseName(s, pos, out.name, error))
        return false;
    skipSpace(s, pos);
    if (pos >= s.size() || s.at(pos) != QLatin1Char('('))
    {
        error = QStringLiteral("expected ( after the op name %1").arg(out.name);
        return false;
    }
    ++pos;
    // A zero-arg op: op()
    skipSpace(s, pos);
    if (pos < s.size() && s.at(pos) == QLatin1Char(')'))
    {
        ++pos;
        return true;
    }
    for (;;)
    {
        QString arg;
        if (!parseArg(s, pos, arg, error))
            return false;
        out.args.append(arg);
        skipSpace(s, pos);
        if (pos >= s.size())
        {
            error = QStringLiteral("unterminated argument list of %1").arg(out.name);
            return false;
        }
        if (s.at(pos) == QLatin1Char(','))
        {
            ++pos;
            continue;
        }
        if (s.at(pos) == QLatin1Char(')'))
        {
            ++pos;
            return true;
        }
        error = QStringLiteral("expected , or ) in the arguments of %1").arg(out.name);
        return false;
    }
}

bool parse(const QString &expr, Parsed &out, QString &error)
{
    int pos = 0;
    skipSpace(expr, pos);
    if (pos >= expr.size())
    {
        error = QStringLiteral("empty expression");
        return false;
    }
    if (expr.at(pos) == QLatin1Char('%'))
    {
        if (!parseMacro(expr, pos, out.macro, error))
            return false;
        if (out.macro.isEmpty())
        {
            error = QStringLiteral("empty macro name");
            return false;
        }
    }
    else if (expr.at(pos) == QLatin1Char('"'))
    {
        // A quoted literal value: starts the pipeline, may chain ops.
        if (!parseArg(expr, pos, out.literalValue, error))
            return false;
        out.isLiteral = true;
    }
    else if (expr.indexOf(QLatin1Char('(')) < 0)
    {
        // A bare string: a plain constant (no ops — a ( would name an op).
        out.isLiteral = true;
        out.literalValue = expr.trimmed();
        return true;
    }
    else
    {
        error = QStringLiteral(
            "a value must be a macro (%name) or a quoted string (\\\"text\\\"): %1")
            .arg(expr);
        return false;
    }
    for (;;)
    {
        skipSpace(expr, pos);
        if (pos >= expr.size())
            return true;
        OpCall call;
        if (!parseOpCall(expr, pos, call, error))
            return false;
        out.ops.append(call);
    }
}

// ---------------------------------------------------------------------------
// Ops
//

struct OpResult
{
    bool ok = false;
    QString value;
    QString error;
};

OpResult fail(const QString &message)
{
    return { false, QString(), message };
}

int toIntArg(const QString &text, const QString &what, QString &error)
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    if (!ok)
        error = QStringLiteral("%1: %2 is not a number").arg(what, text);
    return value;
}

// Qt's QString has no toTitleCase: the first letter of every
// whitespace-separated word is uppercased, the rest is kept as-is.
QString titlecase(const QString &input)
{
    QString out;
    out.reserve(input.size());
    bool nextUpper = true;
    for (const QChar c : input) {
        if (c.isSpace()) {
            out.append(c);
            nextUpper = true;
            continue;
        }
        out.append(nextUpper ? c.toUpper() : c);
        nextUpper = false;
    }
    return out;
}

QString stripChars(const QString &input, const QString &chars)
{
    int first = 0;
    int last = static_cast<int>(input.size());
    while (first < last && chars.contains(input.at(first)))
        ++first;
    while (last > first && chars.contains(input.at(last - 1)))
        --last;
    return input.mid(first, last - first);
}

// One op: consumes the pipeline value, produces the next (or an error).
using OpFunc = std::function<OpResult(const QStringList &args, const QString &input)>;

OpResult opMatch(const QStringList &args, const QString &input)
{
    // match(pattern, [group=0]) — capture group of the first match;
    // no match yields an empty string (the pipeline continues).
    if (args.size() < 1 || args.size() > 2)
        return fail(QStringLiteral("match takes 1 or 2 arguments"));
    const QRegularExpression re(args.at(0), QRegularExpression::NoPatternOption);
    if (!re.isValid())
        return fail(QStringLiteral("invalid regex: %1").arg(re.errorString()));
    QString error;
    const int group = args.size() > 1 ? toIntArg(args.at(1), "group", error) : 0;
    if (!error.isEmpty())
        return fail(error);
    if (group < 0)
        return fail(QStringLiteral("match: the group must be >= 0"));
    const QRegularExpressionMatch m = re.match(input);
    if (!m.hasMatch() || group >= m.capturedTexts().size())
        return { true, QString(), QString() };
    return { true, m.captured(group), QString() };
}

OpResult opReplaceRegex(const QStringList &args, const QString &input)
{
    // replaceRegex(pattern, replacement) — global, \1 backrefs.
    if (args.size() != 2)
        return fail(QStringLiteral("replaceRegex takes 2 arguments"));
    const QRegularExpression re(args.at(0), QRegularExpression::NoPatternOption);
    if (!re.isValid())
        return fail(QStringLiteral("invalid regex: %1").arg(re.errorString()));
    QString out = input;
    out.replace(re, args.at(1));
    return { true, out, QString() };
}

OpResult opReplace(const QStringList &args, const QString &input)
{
    // replace(from, to) — literal, all occurrences.
    if (args.size() != 2)
        return fail(QStringLiteral("replace takes 2 arguments"));
    QString out = input;
    out.replace(args.at(0), args.at(1));
    return { true, out, QString() };
}

OpResult opSplit(const QStringList &args, const QString &input)
{
    // split(sep, [part=1]) — 1-based, negative counts from the end;
    // out of range yields an empty string.
    if (args.size() < 1 || args.size() > 2)
        return fail(QStringLiteral("split takes 1 or 2 arguments"));
    QString error;
    const int part = args.size() > 1 ? toIntArg(args.at(1), "part", error) : 1;
    if (!error.isEmpty())
        return fail(error);
    if (part == 0)
        return fail(QStringLiteral("split: the part must be non-zero"));
    const QStringList parts = input.split(args.at(0), Qt::KeepEmptyParts,
                                          Qt::CaseSensitive);
    const int index = part > 0 ? part - 1 : static_cast<int>(parts.size()) + part;
    if (index < 0 || index >= parts.size())
        return { true, QString(), QString() };
    return { true, parts.at(index), QString() };
}

OpResult opLeft(const QStringList &args, const QString &input)
{
    if (args.size() != 1)
        return fail(QStringLiteral("left takes 1 argument"));
    QString error;
    const int n = toIntArg(args.at(0), "length", error);
    if (!error.isEmpty())
        return fail(error);
    return { true, n <= 0 ? QString() : input.left(n), QString() };
}

OpResult opRight(const QStringList &args, const QString &input)
{
    if (args.size() != 1)
        return fail(QStringLiteral("right takes 1 argument"));
    QString error;
    const int n = toIntArg(args.at(0), "length", error);
    if (!error.isEmpty())
        return fail(error);
    return { true, n <= 0 ? QString() : input.right(n), QString() };
}

OpResult opMid(const QStringList &args, const QString &input)
{
    // mid(start, [length]) — start is 1-based; no length takes the rest.
    if (args.size() < 1 || args.size() > 2)
        return fail(QStringLiteral("mid takes 1 or 2 arguments"));
    QString error;
    const int start = std::max(toIntArg(args.at(0), "start", error), 1);
    if (!error.isEmpty())
        return fail(error);
    int length = -1;
    if (args.size() > 1)
    {
        length = toIntArg(args.at(1), "length", error);
        if (!error.isEmpty())
            return fail(error);
    }
    return { true, input.mid(start - 1, length), QString() };
}

OpResult opTrim(const QStringList &args, const QString &input)
{
    if (!args.isEmpty())
        return fail(QStringLiteral("trim takes no arguments"));
    return { true, input.trimmed(), QString() };
}

OpResult opStrip(const QStringList &args, const QString &input)
{
    // strip(chars) — removes the leading/trailing characters in chars.
    if (args.size() != 1)
        return fail(QStringLiteral("strip takes 1 argument"));
    return { true, stripChars(input, args.at(0)), QString() };
}

OpResult opUpper(const QStringList &args, const QString &input)
{
    if (!args.isEmpty())
        return fail(QStringLiteral("upper takes no arguments"));
    return { true, input.toUpper(), QString() };
}

OpResult opLower(const QStringList &args, const QString &input)
{
    if (!args.isEmpty())
        return fail(QStringLiteral("lower takes no arguments"));
    return { true, input.toLower(), QString() };
}

OpResult opTitlecase(const QStringList &args, const QString &input)
{
    if (!args.isEmpty())
        return fail(QStringLiteral("titlecase takes no arguments"));
    return { true, titlecase(input), QString() };
}

OpResult opPad(const QStringList &args, const QString &input)
{
    // pad(width, [char=0]) — left-pads to the width (never truncates).
    if (args.size() < 1 || args.size() > 2)
        return fail(QStringLiteral("pad takes 1 or 2 arguments"));
    QString error;
    const int width = toIntArg(args.at(0), "width", error);
    if (!error.isEmpty())
        return fail(error);
    if (width < 0)
        return fail(QStringLiteral("pad: the width must be >= 0"));
    QString fill = args.size() > 1 ? args.at(1) : QStringLiteral("0");
    if (fill.isEmpty())
        fill = QStringLiteral("0");
    if (fill.size() > 1)
        fill = fill.left(1);
    if (input.size() >= width)
        return { true, input, QString() };
    return { true, fill.repeated(width - input.size()) + input, QString() };
}

OpResult opOr(const QStringList &args, const QString &input)
{
    // or(fallback) — the fallback when the value is empty.
    if (args.size() != 1)
        return fail(QStringLiteral("or takes 1 argument"));
    return { true, input.isEmpty() ? args.at(0) : input, QString() };
}

// The op table: name -> handler (built once).
const QHash<QString, OpFunc>& opTable()
{
    static const QHash<QString, OpFunc> table = {
        { "match", opMatch },
        { "replaceRegex", opReplaceRegex },
        { "replace", opReplace },
        { "split", opSplit },
        { "left", opLeft },
        { "right", opRight },
        { "mid", opMid },
        { "trim", opTrim },
        { "strip", opStrip },
        { "upper", opUpper },
        { "lower", opLower },
        { "titlecase", opTitlecase },
        { "pad", opPad },
        { "or", opOr },
    };
    return table;
}

OpResult applyOp(const QString &name, const QStringList &args, const QString &input)
{
    const QHash<QString, OpFunc> &table = opTable();
    const auto it = table.constFind(name);
    if (it == table.constEnd())
        return fail(QStringLiteral("unknown op: %1").arg(name));
    return it.value()(args, input);
}

// ---------------------------------------------------------------------------
// The reference (the transform dialog's Help)
//

struct OpDoc
{
    const char* name;
    const char* sig;
    const char* doc;
};

const std::array<OpDoc, 14> kOpDocs = { {
    { "match", "match(pattern, [group=0])",
      "the capture group of the first regex match (0 = the whole match); no match gives an empty string", },
    { "replaceRegex", "replaceRegex(pattern, replacement)",
      "replaces every regex match (\\1..\\9 name the capture groups)", },
    { "replace", "replace(from, to)", "replaces every literal occurrence" },
    { "split", "split(sep, [part=1])",
      "the part of the string split on sep (1-based, negative counts from the end)", },
    { "left", "left(n)", "the first n characters" },
    { "right", "right(n)", "the last n characters" },
    { "mid", "mid(start, [length])",
      "the substring from the 1-based start (to the end without a length)", },
    { "trim", "trim()", "removes leading/trailing whitespace" },
    { "strip", "strip(chars)", "removes the leading/trailing characters in chars" },
    { "upper", "upper()", "uppercases" },
    { "lower", "lower()", "lowercases" },
    { "titlecase", "titlecase()", "capitalizes the first letter of every word" },
    { "pad", "pad(width, [char=0])", "left-pads to the width (never truncates)" },
    { "or", "or(fallback)", "the fallback when the value is empty" },
    },
};

struct MacroDoc
{
    const char* name;
    const char* doc;
};

const std::array<MacroDoc, 18> kMacroDocs = { {
    { "filename", "the file name with its extension (local items only)" },
    { "stem", "the file name without its extension (local items only)" },
    { "ext", "the file's extension (local items only)" },
    { "dir", "the name of the file's parent directory (local items only)" },
    { "path", "the file's full path (local items only)" },
    { "title", "the original title" },
    { "artist", "the original artist" },
    { "album", "the original album" },
    { "albumartist", "the original album artist" },
    { "composer", "the original composer" },
    { "genre", "the original genre" },
    { "comment", "the original comment" },
    { "track", "the original track number" },
    { "tracktotal", "the original track count" },
    { "disc", "the original disc number" },
    { "disctotal", "the original disc count" },
    { "year", "the original year" },
    { "current", "the cell's present value (chain transforms on it)" },
    },
};

} // namespace

namespace MetaTransform {

TransformResult evaluate(const QString &expr, const QHash<QString, QString> &vars)
{
    Parsed parsed;
    QString error;
    if (!parse(expr, parsed, error))
        return { false, QString(), error };

    // Macro names are case-insensitive.
    QString value;
    if (parsed.isLiteral)
    {
        value = parsed.literalValue;
    }
    else
    {
        const auto it = vars.constFind(parsed.macro.toLower());
        if (it == vars.constEnd())
        {
            const QString error = QStringLiteral("unknown macro: %%1%").arg(parsed.macro);
            return { false, QString(), error };
        }
        value = it.value();
    }
    for (const OpCall &call : parsed.ops)
    {
        const OpResult result = applyOp(call.name, call.args, value);
        if (!result.ok)
        {
            const QString error = QStringLiteral("%1: %2").arg(call.name, result.error);
            return { false, QString(), error };
        }
        value = result.value;
    }
    return { true, value, QString() };
}

QHash<QString, QString> variablesFor(const MediaItem &item, const QString &current)
{
    QHash<QString, QString> vars;
    if (item.source.isLocalFile())
    {
        const QFileInfo fi(item.source.toLocalFile());
        vars.insert(QStringLiteral("filename"), fi.fileName());
        vars.insert(QStringLiteral("stem"), fi.completeBaseName());
        vars.insert(QStringLiteral("ext"), fi.suffix());
        vars.insert(QStringLiteral("dir"), fi.dir().dirName());
        vars.insert(QStringLiteral("path"), fi.absoluteFilePath());
    }
    vars.insert(QStringLiteral("title"), item.title);
    vars.insert(QStringLiteral("artist"), item.artist);
    vars.insert(QStringLiteral("album"), item.album);
    vars.insert(QStringLiteral("albumartist"), item.albumArtist);
    vars.insert(QStringLiteral("composer"), item.composer);
    vars.insert(QStringLiteral("genre"), item.genre);
    vars.insert(QStringLiteral("comment"), item.comment);
    vars.insert(QStringLiteral("track"),
                item.trackNumber > 0 ? QString::number(item.trackNumber) : QString());
    vars.insert(QStringLiteral("tracktotal"),
                item.trackTotal > 0 ? QString::number(item.trackTotal) : QString());
    vars.insert(QStringLiteral("disc"),
                item.discNumber > 0 ? QString::number(item.discNumber) : QString());
    vars.insert(QStringLiteral("disctotal"),
                item.discTotal > 0 ? QString::number(item.discTotal) : QString());
    vars.insert(QStringLiteral("year"),
                item.year > 0 ? QString::number(item.year) : QString());
    vars.insert(QStringLiteral("current"), current);
    return vars;
}

QList<QPair<QString, QString>> opDocs()
{
    QList<QPair<QString, QString>> docs;
    for (const OpDoc &doc : kOpDocs)
        docs.append({ QString::fromUtf8(doc.sig), QString::fromUtf8(doc.doc) });
    return docs;
}

QList<QPair<QString, QString>> macroDocs()
{
    QList<QPair<QString, QString>> docs;
    for (const MacroDoc &doc : kMacroDocs)
        docs.append({ QStringLiteral("%%1%").arg(QLatin1String(doc.name)),
                      QString::fromUtf8(doc.doc) });
    return docs;
}

} // namespace MetaTransform
