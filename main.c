#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define mode32 (mode == 32)
#define mode64 (mode == 64)

#define warn(x) printf(x); \
    if (werror) {          \
        return 1;          \
    }

// generates nasm code
int main(int argc, char **argv) {
    int werror = 0;

    int used_out_syscall = 0;
    int used_in_syscall = 0;

    if (argc < 3) {
        printf("brainfuck compiller written by alex_s168\n\n");
        printf("Usage: bfc (-m(32|64)) (-s[mem size]) (-p[amount of pp passes]) (-o[cell starting offset]) (-S) (-P) [infile] [outfile]\n\n");
        printf(" -m[mode]          set the mode of the architecture\n");
        printf("    32                32 bit\n");
        printf("    64                64 bit\n");
        printf(" -s[mem size]      sets the target memory size (=amount of cells)\n");
        printf(" -p[amount]        sets the amount of preprocessor passes\n");
        printf(" -o[offset]        sets the starting position of the pointer\n");
        printf(" -S                only outputs the nasm code\n");
        printf(" -P                only runs the preprocessor\n");
        return 1;
    }

    int memsize = 100;
    int mode = 32;
    int gen_sources = 0;
    int only_pp = 0;
    int cell_off = -1;
    int pp_passes = 1;

    for (int i = 0; i < argc-2; i ++) {
        char *x = argv[i+1];
        char c = x[1];

        if (c == 'm') {
            mode = atoi(x+2);
        }
        else if (c == 's') {
            memsize = atoi(x+2);
        }
        else if (c == 'S') {
            gen_sources = 1;
        }
        else if (c == 'P') {
            only_pp = 1;
        }
        else if (c == 'o') {
            cell_off = atoi(x+2);
        }
        else if (c == 'p') {
            pp_passes = atoi(x+2);
        }
    }

    if (pp_passes < 0) {
        printf("preprocessor passes cant be negative!\n");
        return 1;
    }

    if (only_pp && gen_sources) {
        printf("invalid combination of arguments: -P -S!\n");
        return 1;
    }

    if (cell_off < 0) {
        cell_off = memsize / 2;
    }

    FILE *fp;

    if (mode != 32) {
        printf("Unsupported mode! Supported: 32\n");
        return 1;
    }

    fp = fopen(argv[argc-2], "r");
    fseek(fp, 0L, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);
    char *buff = malloc(size);
    fread(buff, size, size, fp);
    fclose(fp);

    if (pp_passes == 0)
        warn("preprocessor disabled!\n");

    char fn[100];
    if (!(gen_sources || only_pp)) {
        strcpy(fn, argv[argc-2]);
        strcat(fn, ".nasm");
    }
    else {
        strcpy(fn, argv[argc-1]);
    }
    fp = fopen(fn, "w");

    // preprocessor
    for (int pass = 0; pass < pp_passes; pass ++) {
        char *buff2 = malloc(size+1);
        int y = 0;
        for (size_t i = 0; i < size; i++) {
            char c = buff[i];
            if (!(c == '>' || c == '<' || c == '+' || c == '-' || c == '.' || c == ',' || c == '[' || c == ']')) {
                continue;
            }
            // remove contradicting shit
            if (i+1 < size) {
                char next = buff[i+1];
                if ((c == '>' && next == '<') || (c == '+' && next == '-') || (c == '-' && next == '+')) { continue; }
            }
            buff2[y] = c;
            y ++;
        }
        free(buff);
        buff = realloc(buff2, y);
        buff[y] = 0;
        size = y;
    }

    if (only_pp) {
        fwrite(buff, 1, size, fp);
        fclose(fp);
        return 0;
    }

    int localid = 0;

    // nasm header
    fputs("section .data\ncells:\n", fp);
    fprintf(fp, "    times %i db 0\n\n", memsize);
    fprintf(fp, "section .text\n    global _start\n\n_start:\n    mov ecx, cells+%i\n\n", cell_off);
    for (size_t i = 0; i < size; i++) {
        char c = buff[i];
        if (c == '\n') {
            continue;
        }
        if (mode32) {
            if (c == '>') {         // increment pt
                int am = (int)strspn(buff + i, ">");
                if (am > 1) {
                    fprintf(fp, "    add ecx, %i\n", am);
                    i += am-1;
                } else {
                    fputs("    inc ecx\n", fp);
                }
            }
            else if (c == '<') {    // decrement pt
                int am = (int)strspn(buff + i, "<");
                if (am > 1) {
                    fprintf(fp, "    sub ecx, %i\n", am);
                    i += am-1;
                } else {
                    fputs("    dec ecx\n", fp);
                }
            }
            else if (c == '+') {    // inc cell at pt
                int am = (int)strspn(buff + i, "+");
                if (am > 1) {
                    fprintf(fp, "    add byte [ecx], %i\n", am);
                    i += am-1;
                    continue;
                } else {
                    fputs("    inc byte [ecx]\n", fp);
                }
            }
            else if (c == '-') {    // dec cell at pt
                int am = (int)strspn(buff + i, "-");
                if (am > 1) {
                    fprintf(fp, "    sub byte [ecx], %i\n", am);
                    i += am-1;
                    continue;
                } else {
                    fputs("    dec byte [ecx]\n", fp);
                }
            }
            else if (c == '.') {    // output char cell at pt
                int it = 0;
                while ((it++) + i < size) {
                    if (it % 2 == 0 && buff[it+i] != '.')
                        break;
                    if (it % 2 == 1 && buff[it+i] != '>')
                        break;
                    it++;
                }
                if (it % 2 == 1)
                    it--;
                it = it / 2;

                // TODO: decide if its worth it (for now if it is 3 or more prints chained)
                if (it > 2) {
                    // generate write(1, pt, x) syscall
                    fputs("    mov eax, 4\n", fp);
                    fputs("    mov ebx, 1\n", fp);
                    fprintf(fp, "    mov edx, %i\n", it+1);
                    fputs("    push ecx\n", fp);
                    fputs("    int 0x80\n", fp);
                    fputs("    pop ecx\n", fp);
                    i += it * 2;
                    continue;
                }
                used_out_syscall = 1;
                fputs("    call putchar\n", fp);
            }
            else if (c == ',') {    // input char cell at pt
                int it = 0;
                while ((it++) + i < size) {
                    if (it % 2 == 0 && buff[it+i] != ',')
                        break;
                    if (it % 2 == 1 && buff[it+i] != '>')
                        break;
                    it++;
                }
                if (it % 2 == 1)
                    it--;
                it = it / 2;

                // TODO: decide if its worth it (for now if it is 3 or more prints chained)
                if (it > 2) {
                    // generate read(0, pt, x) syscall
                    fputs("    mov eax, 3\n", fp);
                    fputs("    mov ebx, 0\n", fp);
                    fprintf(fp, "    mov edx, %i\n", it+1);
                    fputs("    push ecx\n", fp);
                    fputs("    int 0x80\n", fp);
                    fputs("    pop ecx\n", fp);
                    fputs("    test eax, eax\n", fp);
                    fprintf(fp, "    jnz .L%i\n", localid);
                    fputs("    mov byte [ecx], 0\n", fp);
                    fprintf(fp, ".L%i:\n", localid);
                    localid ++;
                    i += it * 2;
                    continue;
                }
                used_in_syscall = 1;
                fputs("    call getchar\n", fp);
            }
            else if (c == '[') {    // jumps after the corresponding ']' instruction if the cell val is 0
                // optimization for "[-]" (= set 0):
                if (i+2 < size && buff[i+1] == '-' && buff[i+2] == ']') {
                    int after = (int)strspn(buff + i + 3, "+");
                    fprintf(fp, "    mov byte [ecx], %i\n", after);
                    i += 2 + after;

                    continue;
                }

                int corr = -1;
                int ind = 0;
                for (size_t x = i + 1; x < size; x ++) {
                    if (buff[x] == '[') {
                        ind ++;
                    }
                    if (buff[x] == ']') {
                        if (!ind) {
                            corr = (int) x;
                            break;
                        }
                        else {
                            ind --;
                        }
                    }
                }

                if (corr > 0) {
                    fputs("    xor eax, eax\n", fp);
                    fputs("    mov al, [ecx]\n", fp);
                    fputs("    test eax, eax\n", fp);
                    fprintf(fp, "    jz close_%i\n", corr);
                    fprintf(fp, "open_%i:\n", corr);
                }
                else {
                    warn("warning: unclosed square brackets!\n");
                }
            }
            else if (c == ']') {    // jumps after the corresponding '[' instruction if the cell val is not 0
                fputs("    xor eax, eax\n", fp);
                fputs("    mov al, [ecx]\n", fp);
                fputs("    test eax, eax\n", fp);
                fprintf(fp, "    jnz open_%i\n", (int)i);
                fprintf(fp, "close_%i:\n", (int)i);
            }
            else {
                continue;
            }
        }
    }
    // exit syscall
    if (mode32) {
        fputs("end:\n    mov eax,1\n    mov ebx,0\n    int 0x80\n\n", fp);
        if (used_in_syscall) {
            // generate read(0, pt, 1) syscall
            fputs("getchar:\n", fp);
            fputs("    mov eax, 3\n", fp);
            fputs("    mov ebx, 0\n", fp);
            fputs("    mov edx, 1\n", fp);
            fputs("    push ecx\n", fp);
            fputs("    int 0x80\n", fp);
            fputs("    pop ecx\n", fp);
            fputs("    test eax, eax\n", fp);
            fprintf(fp, "    jnz .L%i\n", localid);
            fputs("    mov byte [ecx], 0\n", fp);
            fprintf(fp, ".L%i:\n", localid);
            fputs("    ret\n\n", fp);
            localid ++;
        }
        if (used_out_syscall) {
            // generate write(1, pt, 1) syscall
            fputs("putchar:\n", fp);
            fputs("    mov eax, 4\n", fp);
            fputs("    mov ebx, 1\n", fp);
            fputs("    mov edx, 1\n", fp);
            fputs("    push ecx\n", fp);
            fputs("    int 0x80\n", fp);
            fputs("    pop ecx\n", fp);
            fputs("    ret\n\n", fp);
        }
    }
    fclose(fp);
    free(buff);

    size_t ona = strlen(argv[argc-1])+1;
    char *on = malloc(ona);
    memcpy(on, argv[argc-1], ona);
    if (!gen_sources) {
        char txt[255] = "nasm -f elf32 ";
        strcat(txt, fn);
        strcat(txt, " -o ");
        strcat(txt, "temp.o");
        strcat(txt, "; gold temp.o -b elf -n -nostdlib -O 32 --gc-sections -o ");
        strcat(txt, on);
        strcat(txt, "; rm temp.o; rm ");
        strcat(txt, fn);
        strcat(txt, "; strip ");
        strcat(txt, on);
        system(txt);
    }
    free(on);
    return 0;
}
