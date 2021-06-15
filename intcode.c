#include <stdio.h>     // (f)printf, fscanf, fgetc, fgets
#include <stdlib.h>    // malloc, free, atoi
#include <stdint.h>    // int64_t
#include <inttypes.h>  // PRId64
#include <string.h>    // memcpy, getline
#include <unistd.h>    // isatty, STDIN_FILENO
#include <stdbool.h>   // bool

#define MAXPC  (3)  // max param count
#define STAGES (5)  // number of amplifier stages

typedef enum opcode {
    NOP, ADD, MUL, INP, OUT, JNZ, JPZ, LT, EQ,
    HLT = 99,
} OpCode;

typedef struct lang {
    OpCode op;
    int ic, oc;  // input param count, output param count
} Lang;

typedef struct virtualmachine {
    int64_t *mem;
    size_t size;
    ssize_t ip;
    int phase;
    bool phased;
    bool halted;
} VirtualMachine;

static const Lang lang[] = {
    { .op = NOP, .ic = 0, .oc = 0 },
    { .op = ADD, .ic = 2, .oc = 1 },
    { .op = MUL, .ic = 2, .oc = 1 },
    { .op = INP, .ic = 0, .oc = 1 },
    { .op = OUT, .ic = 1, .oc = 0 },
    { .op = JNZ, .ic = 2, .oc = 0 },
    { .op = JPZ, .ic = 2, .oc = 0 },
    { .op = LT , .ic = 2, .oc = 1 },
    { .op = EQ , .ic = 2, .oc = 1 },
    // HLT=99 is not consecutive, so reuse NOP which has same params (i.e. none)
};
static const size_t langsize = sizeof lang / sizeof *lang;

static int64_t *code = NULL;
static size_t codesize = 0;
static VirtualMachine vm[STAGES];

static void cleanup(void)
{
    free(code);
    for (int i = 0; i < STAGES; ++i) {
        free(vm[i].mem);
    }
}

// static void print(void)
// {
//     if (code && codesize) {
//         printf("%"PRId64, code[0]);
//         for (size_t i = 1; i < codesize; ++i) {
//             printf(",%"PRId64, code[i]);
//         }
//         printf("\n");
//     }
// }

static void load(const char *filename)
{
    // Open file
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "File not found.\n");
        exit(1);
    }

    // Check number of commas, allocate memory
    codesize = 1;  // n commas = (n+1) numbers
    int c;
    while ((c = fgetc(f)) != EOF) {
        codesize += c == ',';
    }
    code = malloc(codesize * sizeof *code);
    if (code == NULL) {
        fclose(f);
        fprintf(stderr, "Out of memory.\n");
        exit(2);
    }

    // Read file
    rewind(f);
    size_t i = 0;
    int n;
    if (fscanf(f, "%d", &n) == 1) {
        code[i++] = n;
    }
    while (i < codesize && fscanf(f, ",%d", &n) == 1) {
        code[i++] = n;
    }
    fclose(f);
    if (i != codesize) {
        free(code);
        fprintf(stderr, "Invalid file.\n");
        exit(3);
    }
}

static void reset(VirtualMachine *pvm, int phase)
{
    if (pvm->size < codesize) {
        pvm->mem = realloc(pvm->mem, codesize * sizeof *(pvm->mem));
        if (pvm->mem == NULL) {
            cleanup();
            fprintf(stderr, "Out of memory.\n");
            exit(4);
        }
    }
    pvm->size = codesize;
    memcpy(pvm->mem, code, pvm->size * sizeof *(pvm->mem));
    pvm->ip = 0;
    pvm->phase = phase;
    pvm->phased = false;
    pvm->halted = false;
}

static int64_t input(void)
{
    int64_t n = 0;
    char *s = NULL;
    size_t t = 0;

    if (isatty(STDIN_FILENO)) {
        printf("? ");
    }
    if (getline(&s, &t, stdin) > 0)
        n = atoll(s);
    free(s);
    return n;
}

static int64_t run(VirtualMachine *pvm, int64_t inputval)
{
    int64_t p[MAXPC];    // parameter values (positional or immediate)
    bool empty = false;  // input fifo (of length 1...)

    while (!pvm->halted) {
        int64_t instr = pvm->mem[pvm->ip++];  // instruction code
        OpCode op = instr % 100;
        instr /= 100;  // param modes
        int pc = 0;    // param count
        const Lang *def = op < langsize ? &lang[op] : &lang[NOP];  // HLT=NOP
        while (pc < def->ic) {
            p[pc++] = instr % 10 ? pvm->mem[pvm->ip++] : pvm->mem[pvm->mem[pvm->ip++]];
            instr /= 10;
        }
        if (def->oc) {  // there is only ever one output param max
            p[pc] = pvm->mem[pvm->ip++];  // ouput param always positional but use immediate value as index in vm
        }
        switch (op) {
            case NOP: break;
            case ADD: pvm->mem[p[2]] = p[0] + p[1];  break;
            case MUL: pvm->mem[p[2]] = p[0] * p[1];  break;
            case INP:
                pvm->mem[p[0]] = pvm->phased ? (empty ? input() : inputval) : pvm->phase;
                empty = pvm->phased;
                pvm->phased = true;
                break;
            case OUT: return p[0];
            case JNZ: if ( p[0]) pvm->ip = p[1];     break;
            case JPZ: if (!p[0]) pvm->ip = p[1];     break;
            case LT : pvm->mem[p[2]] = p[0] <  p[1]; break;
            case EQ : pvm->mem[p[2]] = p[0] == p[1]; break;
            case HLT: pvm->halted = true;            break;
        }
    }
    return inputval;
}

static int next_perm(int *a, int n)
{
	int k, l, t;

	for (k = n - 1; k && a[k - 1] >= a[k]; --k)
        ;
	if (!k--)
        return 0;
	for (l = n - 1; a[l] <= a[k]; l--)
        ;
    t = a[k]; a[k] = a[l]; a[l] = t;
	for (k++, l = n - 1; l > k; l--, k++) {
        t = a[k]; a[k] = a[l]; a[l] = t;
    }
	return 1;
}

static int64_t amplify(int64_t val)
{
	for (int i = 0; i < STAGES; ++i) {
        val = run(&vm[i], val);
    }
    return val;
}

static int64_t maxamp(const int part)
{
    int64_t ma = -1;
    int phase[STAGES] = {0};

    // Initial phases
	for (int i = 0; i < STAGES; ++i) {
        phase[i] = STAGES * (part - 1) + i;
    }

    // Permutations of phases
	do {
        // Start every permutation with fresh amps
        for (int i = 0; i < STAGES; ++i) {
            reset(&vm[i], phase[i]);
        }
        // First run of all the stages
        int64_t a = amplify(0);
        if (part == 2) {
            // Multiple runs until halted
            while (!vm[STAGES - 1].halted) {
                a = amplify(a);
            }
        }
        if (a > ma) {
            ma = a;
        }
	} while (next_perm(phase, STAGES));
    return ma;
}

int main(void)
{
    load("input07.txt");
    printf("Day 7 part 1: %"PRId64"\n", maxamp(1));
    printf("Day 7 part 2: %"PRId64"\n", maxamp(2));
    cleanup();
    return 0;
}
