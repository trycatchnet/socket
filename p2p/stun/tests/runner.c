#define _DEFAULT_SOURCE

/* Corpus round-trip runner.
 *
 * Every file in tests/corpus is a real STUN message captured off the wire.
 * For each one: decode it, encode it again, compare byte for byte. A pass
 * means your code understood every bit that was actually there - there is no
 * partial credit and no "looks right".
 *
 * Build and run:  make test */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../stun.h"

#define RED   "\033[31m"
#define GREEN "\033[32m"
#define DIM   "\033[2m"
#define BOLD  "\033[1m"
#define OFF   "\033[0m"

#define CORPUS_DIR "tests/corpus"

/* Print 16 bytes per line, marking one offset in red. Pass mark = -1 for none. */
static void hexdump(const char *label, const uint8_t *buf, size_t len, long mark)
{
    printf("    %s (%zu bytes)\n", label, len);
    for (size_t i = 0; i < len; i += 16) {
        printf("    " DIM "%04zx" OFF "  ", i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                const char *color = ((long)(i + j) == mark) ? RED : "";
                const char *end   = ((long)(i + j) == mark) ? OFF : "";
                printf("%s%02x%s ", color, buf[i + j], end);
            } else {
                printf("   ");
            }
            if (j == 7) putchar(' ');
        }
        printf(" |");
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            unsigned char c = buf[i + j];
            putchar((c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}

static long first_diff(const uint8_t *a, const uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (a[i] != b[i]) return (long)i;
    return -1;
}

static int name_cmp(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

static int is_bin(const struct dirent *e)
{
    const char *dot = strrchr(e->d_name, '.');
    return dot && strcmp(dot, ".bin") == 0;
}

/* Returns 1 if the file round-tripped cleanly. */
static int run_one(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);

    FILE *fh = fopen(path, "rb");
    if (!fh) { printf("  " RED "OPEN " OFF "%-34s cannot read file\n", name); return 0; }

    uint8_t want[STUN_MAX_MSG];
    size_t want_len = fread(want, 1, sizeof want, fh);
    fclose(fh);

    stun_msg_t msg;
    memset(&msg, 0, sizeof msg);
    int rc = stun_decode(want, want_len, &msg);
    if (rc < 0) {
        printf("  " RED "DECODE" OFF " %-34s stun_decode returned %d\n", name, rc);
        hexdump("on the wire", want, want_len, -1);
        return 0;
    }

    uint8_t got[STUN_MAX_MSG];
    memset(got, 0, sizeof got);
    int got_len = stun_encode(&msg, got, sizeof got);
    if (got_len < 0) {
        printf("  " RED "ENCODE" OFF " %-34s stun_encode returned %d\n", name, got_len);
        return 0;
    }

    /* Never trust the returned length: a buggy encoder can claim more than the
     * buffer holds, and printing that many bytes would crash the runner
     * instead of reporting the bug. */
    if ((size_t)got_len > sizeof got) {
        printf("  " RED "BOUNDS" OFF " %-34s stun_encode claims %d bytes, "
               "buffer is only %zu\n", name, got_len, sizeof got);
        return 0;
    }

    if ((size_t)got_len != want_len) {
        printf("  " RED "LENGTH" OFF " %-34s wrote %d bytes, wire had %zu\n",
               name, got_len, want_len);
        hexdump("on the wire", want, want_len, -1);
        hexdump("your encoder", got, (size_t)got_len, -1);
        return 0;
    }

    long at = first_diff(want, got, want_len);
    if (at >= 0) {
        printf("  " RED "DIFF  " OFF " %-34s first mismatch at offset %ld: "
               "wire %02x, yours %02x\n", name, at, want[at], got[at]);
        hexdump("on the wire", want, want_len, at);
        hexdump("your encoder", got, (size_t)got_len, at);
        return 0;
    }

    printf("  " GREEN "ok    " OFF " %-34s %zu bytes\n", name, want_len);
    return 1;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : CORPUS_DIR;

    struct dirent **entries;
    int count = scandir(dir, &entries, is_bin, name_cmp);
    if (count < 0) {
        fprintf(stderr, "runner: cannot open %s\n", dir);
        return 2;
    }
    if (count == 0) {
        printf("corpus is empty - capture some packets first:\n"
               "  cd ../lab && sudo ./natlab.sh up cone cone\n"
               "  sudo ./natlab.sh stunserver && sudo ./natlab.sh capture\n"
               "  sudo ./natlab.sh refclient a && sudo ./natlab.sh stop-capture\n"
               "  ./carve.py captures/<file>.pcap ../stun/tests/corpus\n");
        free(entries);
        return 2;
    }

    int passed = 0;
    for (int i = 0; i < count; i++) {
        passed += run_one(dir, entries[i]->d_name);
        free(entries[i]);
    }
    free(entries);

    printf("\n" BOLD "corpus: %d/%d packets round-trip clean" OFF "\n", passed, count);
    return (passed == count) ? 0 : 1;
}
