#include <stdio.h>     // (f)printf, fscanf
#include <stdlib.h>    // malloc, free
#include <stdint.h>    // int64_t
#include <inttypes.h>  // PRId64
#include <string.h>    // memcpy
#include <stdbool.h>   // bool, true, false

typedef enum opcode {
    NOP,
    ADD,
    MUL,
    INP,
    OUT,
    JNZ,
    JPZ,
    CLT,
    CEQ,
    RET = 99,
} OpCode;

typedef struct lang {
    OpCode op;
    int ic, oc;  // total param count, input count, output count
} Lang;

static const Lang lang[] = {
    { .op = NOP, .ic = 0, .oc = 0 },
    { .op = ADD, .ic = 2, .oc = 1 },
    { .op = MUL, .ic = 2, .oc = 1 },
    { .op = INP, .ic = 0, .oc = 1 },
    { .op = OUT, .ic = 1, .oc = 0 },
    { .op = JNZ, .ic = 2, .oc = 0 },
    { .op = JPZ, .ic = 2, .oc = 0 },
    { .op = CLT, .ic = 2, .oc = 1 },
    { .op = CEQ, .ic = 2, .oc = 1 },
};
static const size_t langsize = sizeof lang / sizeof *lang;

static int64_t *mem = NULL, *vm = NULL;
static size_t memsize = 0, vmsize = 0;

static void load(char *filename)
{
    // Open file
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "File not found.\n");
        exit(2);
    }

    // Check number of commas, allocate memory
    memsize = 1;  // n commas = (n+1) numbers
    int c;
    while ((c = fgetc(f)) != EOF) {
        memsize += c == ',';
    }
    mem = malloc(memsize * sizeof *mem);
    if (mem == NULL) {
        fclose(f);
        fprintf(stderr, "Out of memory.\n");
        exit(3);
    }

    // Read file
    rewind(f);
    size_t i = 0;
    int n;
    if (fscanf(f, "%d", &n) == 1) {
        mem[i++] = n;
    }
    while (i < memsize && fscanf(f, ",%d", &n) == 1) {
        mem[i++] = n;
    }
    fclose(f);
    if (i != memsize) {
        free(mem);
        fprintf(stderr, "Invalid file.\n");
        exit(4);
    }
}

static void reset(void)
{
    if (vmsize < memsize) {
        vm = realloc(vm, memsize * sizeof *vm);
        if (vm == NULL) {
            free(mem);
            fprintf(stderr, "Out of memory.\n");
            exit(5);
        }
    }
    vmsize = memsize;
    memcpy(vm, mem, vmsize * sizeof *vm);
}

// Ask user input
static int input(void)
{
    char buf[100];

    printf("? ");
    if (fgets(buf, sizeof buf, stdin) != NULL) {
        return atoi(buf);
    }
    return 0;
}

// Give output value
static void output(int64_t a)
{
    printf("%"PRId64"\n", a);
}

static void run(void)
{
    int i;
    int64_t in, par[3];
    OpCode op;
    ssize_t ip = 0;
    bool running = true;

    while (running) {
        in = vm[ip++];
        op = in % 100;
        in /= 100;
        const Lang *def = op < langsize ? &lang[op] : &lang[NOP];
        for (i = 0; i < def->ic; ++i) {
            par[i] = in % 10 ? vm[ip++] : vm[vm[ip++]];
            in /= 10;
        }
        if (def->oc) {
            par[i] = vm[ip++];
        }
        switch (op) {
            case NOP:                                break;
            case ADD: vm[par[2]] = par[0] + par[1];  break;
            case MUL: vm[par[2]] = par[0] * par[1];  break;
            case INP: vm[par[0]] = input();          break;
            case OUT: output(par[0]);                break;
            case JNZ: if ( par[0]) ip = par[1];      break;
            case JPZ: if (!par[0]) ip = par[1];      break;
            case CLT: vm[par[2]] = par[0] <  par[1]; break;
            case CEQ: vm[par[2]] = par[0] == par[1]; break;
            case RET: running = false;               break;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        fprintf(stderr, "Missing argument.\n");
        exit(1);
    }
    load(argv[1]);

    // Day 05 part 1 (use input 1)
    reset();
    run();

    // Day 05 part 2 (use input 5)
    reset();
    run();

    free(mem);
    free(vm);
    return 0;
}
