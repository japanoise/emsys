#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void create_test_file(const char *filename, const char *content) {
    FILE *fp = fopen(filename, "w");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

static void create_test_dir(void) {
    mkdir("test_files", 0755);
}

static void cleanup_test_files(void) {
    unlink("test_files/test.txt");
    unlink("test_files/utf8.txt");
    unlink("test_files/large.txt");
    unlink("test_files/empty.txt");
    unlink("test_files/readonly.txt");
    unlink("test_files/new_save.txt");
    unlink("test_files/utf8_save.txt");
    unlink("test_files/large_save.txt");
    unlink("test_files/nonexistent.txt");
    unlink("test_files/binary.bin");
    rmdir("test_files");
}

#endif