#include "wiki.h"

lString32 wiki::getWordDescription(const lString32 word) {
    lString32 url = lString32(WIKI_baseURL) + word;
    web::get();
}
