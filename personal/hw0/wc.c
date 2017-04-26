#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

typedef unsigned long long ull;
void wc(int fd, ull* line_cnt_p, ull* word_cnt_p, ull* char_cnt_p) {
    char c;
    short begin = 0;
    while (read(fd, &c, 1)==1) {
        (*char_cnt_p)++;
        if (!isspace(c)) {
            begin = 1;
        }
        else if (isspace(c)) {
            if (begin) {
                (*word_cnt_p)++;
                begin = 0;
            }
            if (c=='\n') {
                (*line_cnt_p)++;
            }
        }
    }
}

void print_info(ull line_cnt, ull word_cnt, ull char_cnt, char* file_name) {
    printf("%4llu %4llu %4llu %s\n", line_cnt, word_cnt, char_cnt, file_name);
}

void wc_and_print(int fd, char* name) {
    ull line_cnt = 0;
    ull word_cnt = 0;
    ull char_cnt = 0;
    wc(fd, &line_cnt, &word_cnt, &char_cnt);
    print_info(line_cnt, word_cnt, char_cnt, name);
}

int main(int argc, char *argv[]) {
    int fd;
    int rval = 0;
    if (argc == 1) {
        fd = STDIN_FILENO;
        wc_and_print(fd, "");
    } else {
        ull total_line = 0;
        ull total_word = 0;
        ull total_char = 0;
        for (int i=1; i<argc; i++) {
            char* file_name = argv[i];
            if ((fd=open(file_name, O_RDONLY, 0))<0) {
                fprintf(stderr, "wc: %s: No such file or directory\n", file_name);
                rval = 1;
            }
            ull line_cnt = 0;
            ull word_cnt = 0;
            ull char_cnt = 0;
            wc(fd, &line_cnt, &word_cnt, &char_cnt);
            close(fd);
            print_info(line_cnt, word_cnt, char_cnt, file_name);       
            total_line += line_cnt;
            total_word += word_cnt;
            total_char += char_cnt;
        }
        if (argc>2) {
            print_info(total_line, total_word, total_char, "total");
        }
    }
    return rval;
}

