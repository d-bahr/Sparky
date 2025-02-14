#ifndef WORD_H_
#define WORD_H_

#include "StringStruct.h"

struct Word_;

typedef struct Word_
{
    String string;
    struct Word_ * next;
    struct Word_ * prev;
} Word;

typedef struct
{
    Word * root;
    Word * end;
    size_t length;
} WordList;

typedef struct
{
    const WordList * wordList;
    const Word * current;
} WordIterator;

static inline void WordListInitialize(WordList * wordList)
{
    wordList->root = NULL;
    wordList->end = NULL;
    wordList->length = 0;
}

static inline void WordListClear(WordList * wordList)
{
    Word * w = wordList->root;
    while (w != NULL)
    {
        Word * next = w->next;
        StringDestroy(&w->string);
        free(w);
        w = next;
    }
    wordList->root = NULL;
    wordList->end = NULL;
    wordList->length = 0;
}

static inline void WordListDestroy(WordList * wordList)
{
    WordListClear(wordList);
}

static inline void WordListPushWord(WordList * wordList, Word * newWord)
{
    if (wordList->length == 0)
    {
        newWord->next = NULL;
        newWord->prev = NULL;
        wordList->root = newWord;
        wordList->end = newWord;
    }
    else
    {
        newWord->next = NULL;
        newWord->prev = wordList->end;
        wordList->end->next = newWord;
        wordList->end = newWord;
    }

    wordList->length++;
}

static inline bool WordListPushString(WordList * wordList, const String * word)
{
    Word * newWord = (Word *) malloc(sizeof(Word));
    if (newWord == NULL)
        return false;
    if (!StringInitialize(&newWord->string))
        goto err_init;
    if (!StringCopy(&newWord->string, word))
        goto err_set;

    WordListPushWord(wordList, newWord);
    return true;

err_set:
    StringDestroy(&newWord->string);
err_init:
    free(newWord);
    return false;
}

static inline bool WordListPush(WordList * wordList, const char * word)
{
    Word * newWord = (Word *) malloc(sizeof(Word));
    if (newWord == NULL)
        return false;
    if (!StringInitialize(&newWord->string))
        goto err_init;
    if (!StringSet(&newWord->string, word))
        goto err_set;

    WordListPushWord(wordList, newWord);
    return true;

err_set:
    StringDestroy(&newWord->string);
err_init:
    free(newWord);
    return false;
}

static inline bool WordListPushN(WordList * wordList, const char * word, size_t length)
{
    Word * newWord = (Word *) malloc(sizeof(Word));
    if (newWord == NULL)
        return false;
    if (!StringInitialize(&newWord->string))
        goto err_init;
    if (!StringSetN(&newWord->string, word, length))
        goto err_set;

    WordListPushWord(wordList, newWord);
    return true;

err_set:
    StringDestroy(&newWord->string);
err_init:
    free(newWord);
    return false;
}

static inline void WordListPop(WordList * wordList, const String * word)
{
    if (wordList->length == 1)
    {
        StringDestroy(&wordList->root->string);
        free(wordList->root);
        wordList->root = NULL;
        wordList->end = NULL;
        wordList->length = 0;
    }
    else if (wordList->length > 1)
    {
        Word * end = wordList->end;
        wordList->end = end->prev;
        StringDestroy(&end->string);
        free(end);
        wordList->length--;
    }
}

static inline String * WordListFirst(WordList * wordList)
{
    if (wordList->length == 0)
        return NULL;
    else
        return &wordList->root->string;
}

static inline String * WordListLast(WordList * wordList)
{
    if (wordList->length == 0)
        return NULL;
    else
        return &wordList->end->string;
}

static inline size_t WordListLength(const WordList * wordList)
{
    return wordList->length;
}

static inline WordIterator WordListBegin(const WordList * wordList)
{
    WordIterator iter;
    iter.current = wordList->root;
    iter.wordList = wordList;
    return iter;
}

static inline WordIterator WordListEnd(const WordList * wordList)
{
    WordIterator iter;
    iter.current = wordList->end;
    iter.wordList = wordList;
    return iter;
}

static inline void WordIteratorNext(WordIterator * iter)
{
    iter->current = iter->current->next;
}

static inline void WordIteratorPrev(WordIterator * iter)
{
    iter->current = iter->current->prev;
}

static inline const String * WordIteratorGet(WordIterator * iter)
{
    return &iter->current->string;
}

static inline bool WordIteratorValid(const WordIterator * iter)
{
    return iter->current != NULL;
}

static inline bool WordIteratorEquals(const WordIterator * a, const WordIterator * b)
{
    return a->current == b->current;
}

#endif // WORD_H_
