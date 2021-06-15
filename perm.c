#include <stdio.h>
#include <stdlib.h>

#define N (5)

/* print a list of ints */
static void show(int *x, int len)
{
	for (int i = 0; i < len; i++)
		printf("%d%c", x[i], i == len - 1 ? '\n' : ' ');
	// return 1;
}

/* next lexicographical permutation */
static int next_lex_perm(int *a, int n) {
	int k, l, t;

	/* 1. Find the largest index k such that a[k] < a[k + 1]. If no such
	      index exists, the permutation is the last permutation. */
	for (k = n - 1; k && a[k - 1] >= a[k]; k--)
        ;
	if (!k--)
        return 0;

	/* 2. Find the largest index l such that a[k] < a[l]. Since k + 1 is
	   such an index, l is well defined */
	for (l = n - 1; a[l] <= a[k]; l--)
        ;

	/* 3. Swap a[k] with a[l] */
    t = a[k]; a[k] = a[l]; a[l] = t;

	/* 4. Reverse the sequence from a[k + 1] to the end */
	for (k++, l = n - 1; l > k; l--, k++) {
        t = a[k]; a[k] = a[l]; a[l] = t;
    }
	return 1;
}

static void perm1(int *x, int n, void callback(int *, int))
{
	do {
        callback(x, n);
	} while (next_lex_perm(x, n));
}

int main(void)
{
	int i, x[N];
	for (i = 0; i < N; i++)
        x[i] = i + 1;

	perm1(x, N, show);

	return 0;
}
