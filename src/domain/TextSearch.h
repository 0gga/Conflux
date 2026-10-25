#pragma once
#include <QString>
namespace studio {
inline QString normalizedSearch(QString value) {
    value = value.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString result;
    result.reserve(value.size());
    for (const auto ch : value)
        if (ch.isLetterOrNumber())
            result += ch;
    return result;
}
class TextQuery final {
  public:
    explicit TextQuery(const QString &query) : m_needle(normalizedSearch(query)) {}
    int rank(const QString &text) const {
        if (m_needle.isEmpty())
            return 0;
        const auto haystack = normalizedSearch(text);
        if (haystack == m_needle)
            return 0;
        if (!haystack.contains(m_needle))
            return -1;
        // Whole words precede word prefixes, then other literal substrings.
        bool prefix = haystack.startsWith(m_needle);
        QString word;
        const auto folded = text.normalized(QString::NormalizationForm_KD).toCaseFolded();
        for (const auto ch : folded) {
            if (ch.isLetterOrNumber())
                word += ch;
            else if (ch.isMark())
                continue;
            else if (!word.isEmpty()) {
                if (word == m_needle)
                    return 1;
                prefix |= word.startsWith(m_needle);
                word.clear();
            }
        }
        if (word == m_needle)
            return 1;
        return prefix || word.startsWith(m_needle) ? 2 : 3;
    }
    bool matchesPrepared(const QString &normalized) const {
        return m_needle.isEmpty() || normalized.contains(m_needle);
    }
    bool matches(const QString &text) const {
        return matchesPrepared(normalizedSearch(text));
    }

  private:
    QString m_needle;
};
inline bool textMatches(const QString &text, const QString &query) {
    return TextQuery(query).matches(text);
}
} // namespace studio
