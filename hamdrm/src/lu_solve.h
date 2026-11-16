// lu_solve.h - dense LU solve and the 1-based vector / matrix helpers demodulator.cpp uses (hamdrm helper).
//
// NOT from QSSTV. QSSTV's drmrx folder contains nrutil.c, ludcmp.c and lubksb.c, which are routines from the book
// "Numerical Recipes in C" (no licence notice in QSSTV's copies; the book's code is not under the GPL). They are NOT taken.
// This file is a fresh implementation of the same small job with the same call signatures (1-based indexing):
// LU decomposition with partial pivoting (textbook Doolittle algorithm) and forward/back substitution.
// Licence: GNU GPL version 3 or later, like the helper.
#pragma once
#include <cmath>
#include <cstdlib>
#include <vector>

// float **m = matrix(1, n, 1, n): m[1..n][1..n]; freed with free_matrix
inline float **matrix(long nrl, long nrh, long ncl, long nch)
{
    const long rows = nrh - nrl + 1, cols = nch - ncl + 1;
    float *data = (float *)calloc((size_t)(rows * cols) + 1, sizeof(float));
    float **m = (float **)calloc((size_t)rows + 1, sizeof(float *)) - nrl; // m[nrl..nrh]
    for (long i = nrl; i <= nrh; i++)
        m[i] = data + (i - nrl) * cols - ncl; // row i, column j -> data[(i-nrl)*cols + (j-ncl)]
    return m;
}
inline void free_matrix(float **m, long nrl, long nrh, long ncl, long nch)
{
    (void)nrh;
    (void)nch;
    if (!m)
        return;
    free(m[nrl] + ncl);
    free(m + nrl);
}
inline int *ivector(long nl, long nh) { return (int *)calloc((size_t)(nh - nl + 1), sizeof(int)) - nl; }
inline float *fvector(long nl, long nh) { return (float *)calloc((size_t)(nh - nl + 1), sizeof(float)) - nl; }
inline void free_ivector(int *v, long nl, long) { free(v + nl); }
inline void free_fvector(float *v, long nl, long) { free(v + nl); }

// a[1..n][1..n] is replaced by its LU decomposition (row permutation in indx[1..n], d = +-1)
inline void ludcmp(float **a, int n, int *indx, float *d)
{
    const float TINY = 1.0e-20f;
    *d = 1.0f;
    for (int j = 1; j <= n; j++)
    {
        // pivot: largest |a[i][j]| for i >= j after eliminating with the rows above (Doolittle, column by column)
        for (int i = 1; i < j; i++)
        {
            float sum = a[i][j];
            for (int k = 1; k < i; k++)
                sum -= a[i][k] * a[k][j];
            a[i][j] = sum;
        }
        int imax = j;
        float big = 0.0f;
        for (int i = j; i <= n; i++)
        {
            float sum = a[i][j];
            for (int k = 1; k < j; k++)
                sum -= a[i][k] * a[k][j];
            a[i][j] = sum;
            if (std::fabs(sum) > big)
            {
                big = std::fabs(sum);
                imax = i;
            }
        }
        if (j != imax)
        {
            for (int k = 1; k <= n; k++)
            {
                float t = a[imax][k];
                a[imax][k] = a[j][k];
                a[j][k] = t;
            }
            *d = -(*d);
        }
        indx[j] = imax;
        if (a[j][j] == 0.0f)
            a[j][j] = TINY; // singular: keep going instead of exiting the program
        if (j != n)
        {
            float dum = 1.0f / a[j][j];
            for (int i = j + 1; i <= n; i++)
                a[i][j] *= dum;
        }
    }
}

// solves a x = b using the output of ludcmp; b[1..n] is replaced by x
inline void lubksb(float **a, int n, int *indx, float b[])
{
    for (int i = 1; i <= n; i++)
    {
        int ip = indx[i];
        float t = b[ip];
        b[ip] = b[i];
        b[i] = t;
        for (int j = 1; j < i; j++)
            b[i] -= a[i][j] * b[j];
    }
    for (int i = n; i >= 1; i--)
    {
        float sum = b[i];
        for (int j = i + 1; j <= n; j++)
            sum -= a[i][j] * b[j];
        b[i] = sum / a[i][i];
    }
}
