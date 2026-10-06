#include "uilanguage.h"
#include "payloadencoding.h"

namespace {

int hexValue(QChar character)
{
    const char16_t value = character.unicode();
    if (value >= u'0' && value <= u'9') {
        return value - u'0';
    }
    if (value >= u'a' && value <= u'f') {
        return value - u'a' + 10;
    }
    if (value >= u'A' && value <= u'F') {
        return value - u'A' + 10;
    }
    return -1;
}

bool endsEscape(QChar character)
{
    return character == u'\\' || character == u'\n' || character == u'\r'
        || character == QChar::LineSeparator || character == QChar::ParagraphSeparator;
}

} // namespace

SocketPayload parseSocketPayload(QStringView text)
{
    SocketPayload result;
    qsizetype position = 0;
    qsizetype literalStart = 0;
    while (position < text.size()) {
        if (text[position] != u'\\' || position + 1 == text.size()) {
            ++position;
            continue;
        }
        const QChar next = text[position + 1];
        if (next != u'\\' && next != u'x' && next != u'X') {
            // Unknown sequences are literal text, including their backslash.
            ++position;
            continue;
        }

        // Convert whole literal runs so UTF-16 surrogate pairs stay together.
        result.bytes.append(text.sliced(literalStart, position - literalStart).toUtf8());
        if (next == u'\\') {
            result.escapes.append({static_cast<int>(position), 2, true});
            result.bytes.append('\\');
            position += 2;
        } else {
            qsizetype end = position + 2;
            while (end < text.size() && end < position + 4 && !endsEscape(text[end])) {
                ++end;
            }
            const bool complete = end - position == 4;
            const int high = complete ? hexValue(text[position + 2]) : -1;
            const int low = complete ? hexValue(text[position + 3]) : -1;
            const bool valid = high >= 0 && low >= 0;
            result.escapes.append({static_cast<int>(position),
                                   static_cast<int>(end - position), valid});
            if (valid) {
                result.bytes.append(static_cast<char>((high << 4) | low));
            } else if (result.error.isEmpty()) {
                result.error = UiLanguage::text("Некорректная запись байта в позиции %1: "
                                              "после \\x нужны две шестнадцатеричные цифры (00–FF)")
                                   .arg(position + 1);
            }
            position = end;
        }
        literalStart = position;
    }
    result.bytes.append(text.sliced(literalStart).toUtf8());
    if (!result.error.isEmpty()) {
        result.bytes.clear();
    }
    return result;
}
