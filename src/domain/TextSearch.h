#pragma once
#include <QString>
#include <QStringList>
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
        if (haystack.startsWith(m_needle))
            return 1;
        if (haystack.contains(m_needle))
            return 2;
        if (m_needle.size() < 4)
            return -1;
        const auto match = [&](const QString &candidate) {
            if (candidate.size() < m_needle.size() ||
                candidate.size() > m_needle.size() + qMax(2, m_needle.size() / 4))
                return false;
            int i = 0;
            for (const auto ch : candidate)
                if (ch == m_needle[i] && ++i == m_needle.size())
                    return true;
            return false;
        };
        if (match(haystack))
            return 3;
        QString word;
        for (const auto ch : text) {
            if (ch.isLetterOrNumber())
                word += ch;
            else if (!word.isEmpty()) {
                if (match(normalizedSearch(word)))
                    return 3;
                word.clear();
            }
        }
        return !word.isEmpty() && match(normalizedSearch(word)) ? 3 : -1;
    }
    bool matches(const QString &text) const {
        return rank(text) >= 0;
    }

  private:
    QString m_needle;
};
inline bool textMatches(const QString &text, const QString &query) {
    return TextQuery(query).matches(text);
}
} // namespace studio
