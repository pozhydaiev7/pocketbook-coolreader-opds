// Desktop check of the kept index of XdxfDict (pb_xdxf.cpp):
//   xdxf_test <dict.xdxf> <cache dir>
// Loads the dictionary without a kept index, then twice with one (the first writes it, the
// second reads it), and compares every word, article position and a sample of the articles.
#include "pb_xdxf.h"
#include "pb_stardict.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <time.h>

static double nowMs()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

int main(int argc, char ** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: xdxf_test <dict.xdxf> <cache dir>\n");
        return 2;
    }
    XdxfDict plain;
    XdxfDict::setIndexCacheDir(NULL);
    if (!plain.readInfo(argv[1])) {
        fprintf(stderr, "readInfo: %s\n", plain.lastError().c_str());
        return 1;
    }
    double t0 = nowMs();
    if (!plain.load()) {
        fprintf(stderr, "load: %s\n", plain.lastError().c_str());
        return 1;
    }
    double tPlain = nowMs() - t0;
    printf("plain: %d words in %.1f ms, from the kept file: %d\n", plain.wordCount(), tPlain, plain.indexFromCache());

    XdxfDict::setIndexCacheDir(argv[2]);
    int failed = 0;
    for (int run = 0; run < 2; run++) {
        XdxfDict d;
        d.readInfo(argv[1]);
        t0 = nowMs();
        if (!d.load()) {
            fprintf(stderr, "load %d: %s\n", run, d.lastError().c_str());
            return 1;
        }
        double t = nowMs() - t0;
        int diff = d.wordCount() != plain.wordCount() ? 1 : 0;
        for (int i = 0; !diff && i < plain.wordCount(); i++) {
            if (strcmp(d.wordAt(i), plain.wordAt(i)) != 0)
                diff = 1;
        }
        int articles = 0;
        for (int i = 0; !diff && i < plain.wordCount(); i += (plain.wordCount() > 2000 ? plain.wordCount() / 2000 : 1)) {
            std::string a, b;
            if (!plain.articleAt(i, a) || !d.articleAt(i, b) || a != b)
                diff = 1;
            articles++;
        }
        printf("run %d: %d words in %.1f ms, from the kept file: %d, %s (%d articles compared)\n", run, d.wordCount(), t,
               d.indexFromCache(), diff ? "DIFFERENT" : "same", articles);
        if (diff || d.indexFromCache() != (run == 1))
            failed = 1;
    }
    return failed;
}
