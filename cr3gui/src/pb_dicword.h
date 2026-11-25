/*
 * The word of a dictionary of the firmware (.dic) that was asked for.
 *
 * A dictionary may have words that differ by characters other than letters: "-appointed" and
 * "appointed". They stand one after the other, and the lookup of the firmware, asked for the
 * second, gives the first (a tester: "the articles of the words without a prefix stand second in
 * the list and do not open from it"). pbDicFindWord() looks through the words around the one the
 * firmware gave for the word itself, as far as they differ from it by such characters only.
 *
 * No engine and no firmware here, so that a PC test covers it. The dictionary is a class with
 *     int lookup(const char * what, std::string & word, std::string & article);  // 0: nothing
 *     bool next(std::string & word, std::string & article);
 *     bool prev(std::string & word, std::string & article);
 * where next() and prev() go from the word given last. The rules are a class with
 *     bool same(const std::string & word);  // the word asked for
 *     bool alike(const std::string & word);  // differs from it by characters that are no letters
 */
#ifndef PB_DICWORD_H_INCLUDED
#define PB_DICWORD_H_INCLUDED

#include <string>

/// the words looked through on each side of the word the lookup gave
#define PB_DIC_WORDS_AROUND 8

/// Returns what the lookup of the dictionary returned, or 2 (the word itself) when the word was
/// found beside the one the lookup gave. The dictionary is left at the word returned, so that
/// next() and prev() go on from it.
template <class Dic, class Rules>
int pbDicFindWord(Dic & dic, const char * what, Rules & rules, std::string & word, std::string & article)
{
    int rc = dic.lookup(what, word, article);
    if (rc == 0 || rules.same(word) || !rules.alike(word))
        return rc;
    // the words after the one found, then the words before it
    for (int back = 0; back < 2; back++) {
        std::string w, a;
        for (int i = 0; i < PB_DIC_WORDS_AROUND; i++) {
            if (!(back ? dic.prev(w, a) : dic.next(w, a)))
                break;
            if (rules.same(w)) {
                word = w;
                article = a;
                return 2;
            }
            if (!rules.alike(w))
                break;
        }
        // back to the word the lookup gave: the other side starts from it, and it is the word
        // returned when the word asked for is on neither side
        if (dic.lookup(what, w, a) == 0)
            break;
    }
    return rc;
}

#endif // PB_DICWORD_H_INCLUDED
