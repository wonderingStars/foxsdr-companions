/*
 * sondes_glue.h - force-included (/FI) in front of every translation unit taken from rs1729/RS.
 *
 * Part of the FoxSDR "sondes" GPL helper. Licence: GNU GPL v3 (see ../LICENSE).
 *
 * WHY. The upstream decoders are single-purpose C programs (each has its own main(), reads a WAV from a file or
 * from stdin and prints text). The helper runs several of them in ONE process on the SAME audio, each on its
 * own thread. This header lets the upstream sources stay almost byte-for-byte as they are:
 *   - they are compiled as C++ (MSVC has no C99 "float complex"); <complex> is replaced by a tiny C-compatible
 *     complex type (sondes::cplx) with the mixed float/double promotion rules of C99;
 *   - calloc/malloc/realloc may be assigned to any pointer (as in C);
 *   - reads from the sample stream go to sondes_fread()/sondes_fgetc() (a blocking queue fed by the helper),
 *     instead of a file; the WAV header is not parsed (the helper knows the rate, see demod_mod.c);
 *   - printf()/fprintf(stdout) are captured one line at a time and turned into JSON events by the helper.
 * Every other change to the upstream code is listed in ../UPSTREAM.md.
 */
#ifndef SONDES_GLUE_H
#define SONDES_GLUE_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

/* ------------------------------------------------------------------------------------------------------------ */
/* C99-style complex numbers                                                                                    */

namespace sondes {

template <class T> struct cplx {
    T re, im;
    cplx() : re(0), im(0) {}
    cplx(double r) : re((T)r), im(0) {}
    cplx(int r) : re((T)r), im(0) {}
    cplx(T r, T i) : re(r), im(i) {}
    template <class U> cplx(const cplx<U> &o) : re((T)o.re), im((T)o.im) {}
    template <class U> cplx &operator=(const cplx<U> &o) { re = (T)o.re; im = (T)o.im; return *this; }
    cplx &operator=(double r) { re = (T)r; im = 0; return *this; }
    template <class U> cplx &operator+=(const cplx<U> &o) { re += (T)o.re; im += (T)o.im; return *this; }
    template <class U> cplx &operator-=(const cplx<U> &o) { re -= (T)o.re; im -= (T)o.im; return *this; }
    template <class U> cplx &operator*=(const cplx<U> &o) {
        T r = (T)(re * o.re - im * o.im), i = (T)(re * o.im + im * o.re);
        re = r; im = i; return *this;
    }
    cplx &operator*=(double s) { re = (T)(re * s); im = (T)(im * s); return *this; }
    cplx &operator/=(double s) { re = (T)(re / s); im = (T)(im / s); return *this; }
    cplx &operator+=(double s) { re = (T)(re + s); return *this; }
    cplx &operator-=(double s) { re = (T)(re - s); return *this; }
};

template <class A, class B> struct cm { typedef typename std::common_type<A, B>::type type; };

template <class T> cplx<T> operator-(const cplx<T> &a) { return cplx<T>(-a.re, -a.im); }
template <class T> cplx<T> operator+(const cplx<T> &a) { return a; }

template <class A, class B> cplx<typename cm<A, B>::type> operator+(const cplx<A> &a, const cplx<B> &b) {
    typedef typename cm<A, B>::type R; return cplx<R>((R)(a.re + b.re), (R)(a.im + b.im)); }
template <class A, class B> cplx<typename cm<A, B>::type> operator-(const cplx<A> &a, const cplx<B> &b) {
    typedef typename cm<A, B>::type R; return cplx<R>((R)(a.re - b.re), (R)(a.im - b.im)); }
template <class A, class B> cplx<typename cm<A, B>::type> operator*(const cplx<A> &a, const cplx<B> &b) {
    typedef typename cm<A, B>::type R;
    return cplx<R>((R)(a.re * b.re - a.im * b.im), (R)(a.re * b.im + a.im * b.re)); }

/* complex with a real scalar (any arithmetic type) */
template <class A, class S, class = typename std::enable_if<std::is_arithmetic<S>::value>::type>
cplx<typename cm<A, S>::type> operator*(const cplx<A> &a, S s) {
    typedef typename cm<A, S>::type R; return cplx<R>((R)(a.re * s), (R)(a.im * s)); }
template <class A, class S, class = typename std::enable_if<std::is_arithmetic<S>::value>::type>
cplx<typename cm<A, S>::type> operator*(S s, const cplx<A> &a) {
    typedef typename cm<A, S>::type R; return cplx<R>((R)(s * a.re), (R)(s * a.im)); }
template <class A, class S, class = typename std::enable_if<std::is_arithmetic<S>::value>::type>
cplx<typename cm<A, S>::type> operator/(const cplx<A> &a, S s) {
    typedef typename cm<A, S>::type R; return cplx<R>((R)(a.re / s), (R)(a.im / s)); }
template <class A, class S, class = typename std::enable_if<std::is_arithmetic<S>::value>::type>
cplx<typename cm<A, S>::type> operator+(const cplx<A> &a, S s) {
    typedef typename cm<A, S>::type R; return cplx<R>((R)(a.re + s), (R)a.im); }
template <class A, class S, class = typename std::enable_if<std::is_arithmetic<S>::value>::type>
cplx<typename cm<A, S>::type> operator+(S s, const cplx<A> &a) {
    typedef typename cm<A, S>::type R; return cplx<R>((R)(s + a.re), (R)a.im); }
template <class A, class S, class = typename std::enable_if<std::is_arithmetic<S>::value>::type>
cplx<typename cm<A, S>::type> operator-(const cplx<A> &a, S s) {
    typedef typename cm<A, S>::type R; return cplx<R>((R)(a.re - s), (R)a.im); }
template <class A, class S, class = typename std::enable_if<std::is_arithmetic<S>::value>::type>
cplx<typename cm<A, S>::type> operator-(S s, const cplx<A> &a) {
    typedef typename cm<A, S>::type R; return cplx<R>((R)(s - a.re), (R)(-a.im)); }

typedef cplx<float> fcplx;
typedef cplx<double> dcplx;

template <class T> double creal(const cplx<T> &z) { return (double)z.re; }
template <class T> double cimag(const cplx<T> &z) { return (double)z.im; }
template <class T> double cabs(const cplx<T> &z) { return ::hypot((double)z.re, (double)z.im); }
template <class T> double carg(const cplx<T> &z) { return ::atan2((double)z.im, (double)z.re); }
template <class T> cplx<T> conj(const cplx<T> &z) { return cplx<T>(z.re, -z.im); }
/* C99 cexp() takes and returns double complex whatever the argument was */
template <class T> dcplx cexp(const cplx<T> &z) {
    double e = ::exp((double)z.re);
    return dcplx(e * ::cos((double)z.im), e * ::sin((double)z.im)); }

} /* namespace sondes */

using sondes::creal;
using sondes::cimag;
using sondes::cabs;
using sondes::carg;
using sondes::conj;
using sondes::cexp;
using sondes::fcplx;
using sondes::dcplx;
static const sondes::dcplx sondes_I(0.0, 1.0);
#define I sondes_I

/* ------------------------------------------------------------------------------------------------------------ */
/* malloc/calloc/realloc usable without a cast, as in C                                                          */

struct sondes_anyptr {
    void *p;
    template <class T> operator T *() const { return (T *)p; }
};
static inline sondes_anyptr sondes_malloc(size_t n) { sondes_anyptr a; a.p = ::malloc(n); return a; }
static inline sondes_anyptr sondes_calloc(size_t n, size_t s) { sondes_anyptr a; a.p = ::calloc(n, s); return a; }
static inline sondes_anyptr sondes_realloc(void *q, size_t n) { sondes_anyptr a; a.p = ::realloc(q, n); return a; }
#define malloc sondes_malloc
#define calloc sondes_calloc
#define realloc sondes_realloc

/* ------------------------------------------------------------------------------------------------------------ */
/* the sample stream and the captured text output (sondes_glue.cpp)                                              */

FILE *sondes_stdin(void);                                            /* the calling thread's stream */
size_t sondes_fread(void *ptr, size_t size, size_t n, FILE *f);
int sondes_fgetc(FILE *f);
int sondes_fclose(FILE *f);
int sondes_printf(const char *fmt, ...);
int sondes_fprintf(FILE *f, const char *fmt, ...);
int sondes_decoder_rate(void);                                       /* sample rate of the calling thread's stream */

#undef stdin
#define stdin   sondes_stdin()
#define fread   sondes_fread
#define fgetc   sondes_fgetc
#define fclose  sondes_fclose
#define printf  sondes_printf
#define fprintf sondes_fprintf

#endif /* SONDES_GLUE_H */
