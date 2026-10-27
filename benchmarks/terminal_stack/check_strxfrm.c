/* Checks the C library's promise that strcmp () on strxfrm () output orders
 * strings like strcoll (). nnn compares names with strcoll (), and the
 * benchmark's Bucketization key for nnn is a prefix of strxfrm () output, so
 * the key is only order-preserving where the promise holds.
 *
 * Usage: speedup_check_strxfrm LOCALE LISTING.tsv
 * Compares every pair of names (the second column) and prints the pairs on
 * which the two orders disagree. Exit status 1 when any pair does. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAMES 4096
#define MAX_NAME 256
#define MAX_KEY 2048

static char names[MAX_NAMES][MAX_NAME];
static char keys[MAX_NAMES][MAX_KEY];

static int sign(int x) { return (x > 0) - (x < 0); }

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s LOCALE LISTING.tsv\n", argv[0]);
        return 2;
    }
    if (!setlocale(LC_ALL, argv[1])) {
        fprintf(stderr, "locale %s not available\n", argv[1]);
        return 2;
    }
    FILE* f = fopen(argv[2], "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[2]);
        return 2;
    }
    char line[8192];
    int n = 0;
    if (!fgets(line, sizeof line, f)) return 2; /* header */
    while (n < MAX_NAMES && fgets(line, sizeof line, f)) {
        char* name = strchr(line, '\t');
        if (!name) continue;
        name++;
        char* end = strchr(name, '\t');
        if (end) *end = '\0';
        strncpy(names[n], name, MAX_NAME - 1);
        if (strxfrm(keys[n], names[n], MAX_KEY) >= MAX_KEY) {
            fprintf(stderr, "sort key too long for %s\n", names[n]);
            return 2;
        }
        n++;
    }
    fclose(f);

    long long pairs = 0, disagree = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            pairs++;
            int c = sign(strcoll(names[i], names[j]));
            int k = sign(strcmp(keys[i], keys[j]));
            if (c != k) {
                if (disagree < 10) printf("strcoll %+d, strxfrm %+d: \"%s\" \"%s\"\n", c, k, names[i], names[j]);
                disagree++;
            }
        }
    }
    printf("%s, locale %s: %d names, %lld pairs, %lld disagree\n", argv[2], argv[1], n, pairs, disagree);
    return disagree ? 1 : 0;
}
