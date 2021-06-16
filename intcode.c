#include <stdio.h>     // (f)printf, fscanf, fgetc, fgets
#include <stdlib.h>    // malloc, free, atoi
#include <stdint.h>    // int64_t
#include <inttypes.h>  // PRId64
#include <string.h>    // memcpy, getline
#include <unistd.h>    // isatty, STDIN_FILENO
#include <stdbool.h>   // bool

#define MAXPC   (3)  // max param count
#define STAGES  (5)  // number of amplifier stages (day 7)
#define VMCOUNT (STAGES + 1)  // maximum number of VMs

typedef enum errcode {
    ERR_OK,
    ERR_FILE_NOTFOUND,
    ERR_FILE_NOTCSV,
    ERR_FILE_INVALID,
    ERR_MEM_OUT,
    ERR_IP_LO,
    ERR_IP_HI,
    ERR_IP_INSTR,
    ERR_PAR_READ,
    ERR_PAR_WRITE,
} ErrCode;

typedef enum parmode {
    POS, IMM, REL
} ParMode;

typedef enum opcode {
    NOP, ADD, MUL, INP, OUT, JNZ, JPZ, LT, EQ, RBO,
    HLT = 99,
} OpCode;

typedef struct lang {
    OpCode op;
    int pc, ic, oc;  // total params, input (read) params, output (write) params
} Lang;

// Language definition
// pc = param count, ic = input (read) param count, oc = output (write) param count
static const Lang lang[] = {
    { .op = NOP, .pc = 0, .ic = 0, .oc = 0 },  // no operation
    { .op = ADD, .pc = 3, .ic = 2, .oc = 1 },  // add
    { .op = MUL, .pc = 3, .ic = 2, .oc = 1 },  // multiply
    { .op = INP, .pc = 1, .ic = 0, .oc = 1 },  // input
    { .op = OUT, .pc = 1, .ic = 1, .oc = 0 },  // output
    { .op = JNZ, .pc = 2, .ic = 2, .oc = 0 },  // jump if not zero
    { .op = JPZ, .pc = 2, .ic = 2, .oc = 0 },  // jump if zero
    { .op = LT , .pc = 3, .ic = 2, .oc = 1 },  // less than (1/0)
    { .op = EQ , .pc = 3, .ic = 2, .oc = 1 },  // equal (1/0)
    { .op = RBO, .pc = 1, .ic = 1, .oc = 0 },  // relative base offset
    // HLT=99 is not consecutive, so reuse NOP which has same params (i.e. none)
};
static const size_t langsize = sizeof lang / sizeof *lang;

typedef struct virtualmachine {
    int64_t *mem;
    size_t size;
    ssize_t ip, base;
    bool halted;
} VirtualMachine;

static VirtualMachine vm[VMCOUNT] = {0};

#define FIFOSIZE (100)
static int64_t fifobuf[FIFOSIZE] = {0};
static size_t fifohead = 0, fifotail = 0;

// Get number from stdin, either piped or on terminal
static int64_t input(void)
{
    int64_t val = 0;
    char *s = NULL;
    size_t t = 0;

    if (isatty(STDIN_FILENO)) {
        printf("? ");
    }
    if (getline(&s, &t, stdin) > 0) {
        val = atoll(s);
    }
    free(s);
    return val;
}

static void output(int64_t val)
{
    printf("%"PRId64"\n", val);
}

static int64_t fifopop(void)
{
    if (fifohead == fifotail) {
        return input();
    }
    int64_t val = fifobuf[fifotail++];
    fifotail %= FIFOSIZE;
    return val;
}

static void fifopush(int64_t val)
{
    size_t nexthead = (fifohead + 1) % FIFOSIZE;
    if (nexthead == fifotail) {
        output(val);
    }
    fifobuf[fifohead] = val;
    fifohead = nexthead;
}

static void fifoprint()
{
    while (fifohead != fifotail) {
        output(fifopop());
    }
}

static const Lang *getdef(OpCode op)
{
    if (op >= langsize) {
        return &lang[NOP];
    }
    if (lang[op].op == op) {
        return &lang[op];
    }
    for (size_t i = 0; i < langsize; ++i) {
        if (lang[i].op == op) {
            return &lang[i];
        }
    }
    return &lang[NOP];
}

static void clean(VirtualMachine *pv)
{
    if (pv != NULL) {
        free(pv->mem);
        memset(pv, 0, sizeof *pv);
    }
}

static void clean_all(void)
{
    for (size_t i = 0; i < VMCOUNT; ++i) {
        clean(&vm[i]);
    }
}

static __attribute__((noreturn)) void fatal(ErrCode e)
{
    switch (e) {
        case ERR_OK            : break;
        case ERR_FILE_NOTFOUND : fprintf(stderr, "File not found.\n");       break;
        case ERR_FILE_NOTCSV   : fprintf(stderr, "Not a CSV file.\n");       break;
        case ERR_FILE_INVALID  : fprintf(stderr, "Invalid file format.\n");  break;
        case ERR_MEM_OUT       : fprintf(stderr, "Out of memory.\n");        break;
        case ERR_IP_LO         : fprintf(stderr, "IP segfault (under).\n");  break;
        case ERR_IP_HI         : fprintf(stderr, "IP segfault (over).\n");   break;
        case ERR_IP_INSTR      : fprintf(stderr, "Instr segfault.\n");       break;
        case ERR_PAR_READ      : fprintf(stderr, "Par segfault (read).\n");  break;
        case ERR_PAR_WRITE     : fprintf(stderr, "Par segfault (write).\n"); break;
    }
    clean_all();
    exit((int)e);
}

static void setsize(VirtualMachine *pv, const size_t newsize)
{
    if (pv != NULL && newsize > pv->size) {
        int64_t *try = realloc(pv->mem, newsize * sizeof *(pv->mem));
        if (try == NULL) {
            fatal(ERR_MEM_OUT);
        }
        memset(try + pv->size, 0, (newsize - pv->size) * sizeof *(pv->mem));
        pv->mem = try;
        pv->size = newsize;
    }
}

static void addsize(VirtualMachine *pv, const ssize_t extra)
{
    if (pv != NULL && extra > 0) {
        setsize(pv, pv->size + (size_t)extra);
    }
}

static void copyvm(VirtualMachine *dst, const VirtualMachine *src)
{
    if (dst != NULL && src != NULL) {
        setsize(dst, src->size);  // new minimal size (could still be bigger as a left-over)
        memcpy(dst->mem, src->mem, src->size * sizeof *(src->mem));  // copy memory from source
        if (dst->size > src->size) {  // erase the rest
            memset(dst->mem + src->size, 0, (dst->size - src->size) * sizeof *(dst->mem));
        }
        dst->ip     = src->ip;
        dst->base   = src->base;
        dst->halted = src->halted;
    }
}

static void load(VirtualMachine *pv, const char *filename)
{
    // Open file
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        fatal(ERR_FILE_NOTFOUND);
    }

    // Check number of commas
    size_t commas = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        commas += c == ',';
    }
    if (!commas) {
        // TODO: single number "99" or even "0" should probably be a valid file
        fatal(ERR_FILE_NOTCSV);
    }

    // Prepare VM & memory
    clean(pv); // reset everything to zero
    setsize(pv, commas + 1);

    // Read file into VM memory
    rewind(f);
    int n;
    size_t i = 0;
    if (fscanf(f, "%d", &n) == 1) {  // first value has no leading comma
        pv->mem[i++] = n;
    }
    while (i < pv->size && fscanf(f, ",%d", &n) == 1) {  // all other values
        pv->mem[i++] = n;
    }
    fclose(f);
    if (i != pv->size) {
        fatal(ERR_FILE_INVALID);
    }
}

static void print(VirtualMachine *pv)
{
    printf("%"PRId64, pv->mem[0]);
    for (size_t i = 1; i < pv->size; ++i) {
        printf(",%"PRId64, pv->mem[i]);
    }
    printf("\n");
}

static void run(VirtualMachine *pv)
{
    int64_t in, p[MAXPC], q;  // complete instruction, parameter values, temp param value
    OpCode  op;               // opcode from instruction
    ParMode mode;             // parameter mode for one parameter:
    int pc;                   // running parameter count

    while (!pv->halted) {
        if (pv->ip < 0) {
            fatal(ERR_IP_LO);
        }
        if ((size_t)(pv->ip) >= pv->size) {
            fatal(ERR_IP_HI);
        }

        in = pv->mem[pv->ip++];  // get instruction code, increment IP
        op = in % 100;
        const Lang *def = getdef(op);

        if (def->pc > 0 && (size_t)(pv->ip + def->pc) >= pv->size) {
            fatal(ERR_IP_INSTR);
        }

        in /= 100;  // parameter modes for all parameters
        pc = 0;     // param count
        while (pc < def->ic) {
            q = pv->mem[pv->ip++];  // get immediate parameter value, increment IP
            mode = in % 10;         // mode for this parameter (0=positional, 1=immediate, 2=relative)
            if (!(mode & IMM)) {    // if positional or relative
                if (mode & REL) {   // if relative
                    q += pv->base;
                }
                if (q < 0) {  // negative addresses are invalid
                    fatal(ERR_PAR_READ);
                }
                if ((size_t)q >= pv->size) {  // read beyond mem size?
                    setsize(pv, (size_t)(q + 1));
                }
                q = pv->mem[q];  // indirection for positional or relative parameter
            }
            p[pc++] = q;  // save & increment param count
            in /= 10;     // modes for remaining parameters
        }

        if (def->oc) {  // output param always last, never more than one, never immediate
            q = pv->mem[pv->ip++];  // get immediate parameter value, increment IP
            mode = in % 10;         // mode for this parameter (0=positional, 1=immediate, 2=relative)
            if (mode & REL) {       // if relative
                q += pv->base;
            }
            if (q < 0) {  // negative addresses are invalid
                fatal(ERR_PAR_WRITE);
            }
            if ((size_t)q >= pv->size) {  // write beyond mem size?
                setsize(pv, (size_t)(q + 1));
            }
            p[pc++] = q;  // no indirection yet, use as index in mem
        }

        switch (op) {
            case NOP: break;
            case ADD: pv->mem[p[2]] = p[0] + p[1];  break;
            case MUL: pv->mem[p[2]] = p[0] * p[1];  break;
            case INP: pv->mem[p[0]] = fifopop();    break;   // when fifo empty, ask
            case OUT: fifopush(p[0]);               return;  // TODO: keep running? But needs separate in/out fifos :(
            case JNZ: if ( p[0]) pv->ip = p[1];     break;
            case JPZ: if (!p[0]) pv->ip = p[1];     break;
            case LT : pv->mem[p[2]] = p[0] <  p[1]; break;
            case EQ : pv->mem[p[2]] = p[0] == p[1]; break;
            case RBO: pv->base += p[0];             break;
            case HLT: pv->halted = true;            break;
        }
    }
}

// Permutate in lexicographic order, adapted from "perm1()"
// at http://www.rosettacode.org/wiki/Permutations#version_4
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

// Maximum amplification for different phase permutations
// amp = VirtualMachines array of length STAGES
static int64_t maxamp(int part)
{
    int64_t amax = -1;
    int phase[STAGES];

    // Initial phase numbers: 0-4 for part 1, 5-9 for part 2
	for (int i = 0; i < STAGES; ++i) {
        phase[i] = STAGES * (part - 1) + i;
    }

    // All permutations of phase array
	do {
        // Start every permutation with fresh amps
        for (int i = 0; i < STAGES; ++i) {
            copyvm(&vm[i], &vm[STAGES]);
        }
        // First run requires two inputs for every stage
        int64_t a = 0;
        for (int i = 0; i < STAGES; ++i) {
            fifopush(phase[i]);
            fifopush(a);
            run(&vm[i]);
            a = fifopop();
        }
        if (part == 2) {
            // Multiple runs until halted
            fifopush(a);
            int i = 0;
            while (!vm[i].halted) {
                run(&vm[i++]);
                i %= STAGES;
            }
            a = fifopop();
        }
        if (a > amax) {
            amax = a;
        }
	} while (next_perm(phase, STAGES));
    return amax;
}

static int day2part2(VirtualMachine *app, VirtualMachine *ref)
{
    static const int magic = 19690720;
    for (int verb = 0; verb < 100; ++verb) {
        for (int noun = 0; noun < 100; ++noun) {
            copyvm(app, ref);
            app->mem[1] = noun;
            app->mem[2] = verb;
            run(app);
            if (app->mem[0] == magic) {
                return noun * 100 + verb;
            }
        }
    }
}

int main(void)
{
    VirtualMachine *ref, *app;

    // Day 2 part 1
    *ref = &vm[0];
    *app = &vm[1];
    load(ref, "input02.txt");  // load data into last VM, amps are numbered 0..STAGES-1
    copyvm(app, ref);
    app->mem[1] = 12;
    app->mem[2] = 2;
    run(app);
    printf("Day 2 part 1: %"PRId64"\n", app->mem[0]);  // right answer = 3085697

    // Day 2 part 2
    printf("Day 2 part 2: %d\n", day2part2(app, ref));  // right answer = 9425

    // Day 7
    ref = &vm[STAGES];
    load(ref, "input07.txt");  // load data into last VM, amps are numbered 0..STAGES-1
    printf("Day 7 part 1: %"PRId64"\n", maxamp(1));  // right answer = 929800
    printf("Day 7 part 2: %"PRId64"\n", maxamp(2));  // right answer = 15432220

    // Day 9 part 1
    ref = &vm[0];
    app = &vm[1];
    load(ref, "input09.txt");
    copyvm(app, ref);
    fifopush(1);
    run(app);
    printf("Day 9 part 1: %"PRId64"\n", fifopop());  // right answer = 4261108180

    // Day 9 part 2
    copyvm(app, ref);
    fifopush(2);
    run(app);
    printf("Day 9 part 2: %"PRId64"\n", fifopop());  // right answer = 77944

    clean_all();
    return 0;
}
