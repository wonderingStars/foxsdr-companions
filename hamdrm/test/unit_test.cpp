// Unit tests for the parts of hamdrm that are not upstream code or that were ported to std types:
// SHA-256, the LU solver that replaces Numerical Recipes, and the Reed-Solomon file layer (.rs1 - .rs4).
// The source decoder (file names, segments, MOT headers) is covered by the end-to-end tests. Exit code 0 = all passed.
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "lu_solve.h"
#include "rs_file.h"
#include "sha256.h"

static int failures = 0;
#define CHECK(c)                                          \
    do                                                    \
    {                                                     \
        if (!(c))                                         \
        {                                                 \
            printf("FAIL line %d: %s\n", __LINE__, #c);   \
            failures++;                                   \
        }                                                 \
    } while (0)

static void test_sha()
{
    CHECK(sha256ns::sha256_hex((const unsigned char *)"", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256ns::sha256_hex((const unsigned char *)"abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha256ns::sha256_hex((const unsigned char *)m.data(), m.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void test_lu()
{
    srand(3);
    const int sizes[] = {1, 2, 5, 20, 100};
    for (int n : sizes)
    {
        float **a = matrix(1, n, 1, n), **orig = matrix(1, n, 1, n);
        float *x = fvector(1, n), *b = fvector(1, n);
        int *idx = ivector(1, n);
        for (int i = 1; i <= n; i++)
        {
            x[i] = (float)(rand() % 200 - 100) / 10.0f;
            for (int j = 1; j <= n; j++)
                orig[i][j] = a[i][j] = (float)(rand() % 200 - 100) / 10.0f + (i == j ? 30.0f : 0.0f);
        }
        for (int i = 1; i <= n; i++)
        {
            b[i] = 0;
            for (int j = 1; j <= n; j++)
                b[i] += orig[i][j] * x[j];
        }
        float d;
        ludcmp(a, n, idx, &d);
        lubksb(a, n, idx, b);
        double worst = 0;
        for (int i = 1; i <= n; i++)
            worst = std::fmax(worst, std::fabs(b[i] - x[i]));
        CHECK(worst < 1e-3);
        free_matrix(a, 1, n, 1, n);
        free_matrix(orig, 1, n, 1, n);
        free_fvector(x, 1, n);
        free_fvector(b, 1, n);
        free_ivector(idx, 1, n);
    }
    // a matrix that needs a row swap (zero on the diagonal)
    float **a = matrix(1, 2, 1, 2);
    float b[3] = {0, 3, 2};
    int idx[3];
    float d;
    a[1][1] = 0;
    a[1][2] = 1;
    a[2][1] = 1;
    a[2][2] = 0;
    ludcmp(a, 2, idx, &d);
    lubksb(a, 2, idx, b);
    CHECK(std::fabs(b[1] - 2) < 1e-6 && std::fabs(b[2] - 3) < 1e-6);
    free_matrix(a, 1, 2, 1, 2);
    // a singular matrix must not end the program
    float **s = matrix(1, 2, 1, 2);
    s[1][1] = 1;
    s[1][2] = 2;
    s[2][1] = 2;
    s[2][2] = 4;
    ludcmp(s, 2, idx, &d);
    free_matrix(s, 1, 2, 1, 2);
}

static Bytes make_file(size_t n)
{
    Bytes f(n);
    unsigned s = 12345;
    for (size_t i = 0; i < n; i++)
    {
        s = s * 1103515245u + 12345u;
        f[i] = (unsigned char)(s >> 16);
    }
    return f;
}

static void test_rs_one(eRSType t, const char *ext, size_t fileSize, int segLen, int missingCount)
{
    Bytes file = make_file(fileSize);
    Bytes coded = file;
    reedSolomonCoder enc;
    CHECK(enc.encode(coded, "jpg", t));
    const int total = (int)((coded.size() + segLen - 1) / segLen);
    std::vector<int> era = {total, segLen};
    Bytes rx = coded;
    for (int m = 0; m < missingCount; m++)
    {
        int seg = (m * 7 + 1) % total; // spread, deterministic
        bool dup = false;
        for (size_t k = 2; k < era.size(); k++)
            dup |= era[k] == seg;
        if (dup)
            continue;
        era.push_back(seg);
        for (int j = 0; j < segLen && (size_t)seg * segLen + j < rx.size(); j++)
            rx[(size_t)seg * segLen + j] = 0;
    }
    // the receiver sees exactly the coded length: missing segments zero filled, the last (short) segment as sent
    reedSolomonCoder dec;
    std::string name;
    Bytes out;
    bool ok = dec.decode(rx, std::string("pic.") + ext, name, out, ext, era);
    CHECK(ok);
    if (ok)
    {
        CHECK(name == "pic.jpg");
        CHECK(out == file);
    }
}

static void test_rs()
{
    test_rs_one(RST2, "rs2", 5000, 400, 0);
    test_rs_one(RST2, "rs2", 5000, 400, 3);
    test_rs_one(RST1, "rs1", 20000, 512, 4);
    test_rs_one(RST3, "rs3", 9000, 300, 6);
    test_rs_one(RST4, "rs4", 15000, 450, 8);
    // failure path: far too many missing segments cannot be repaired
    {
        Bytes file = make_file(5000), coded = file;
        reedSolomonCoder enc;
        enc.encode(coded, "jpg", RST4);
        const int segLen = 400, total = (int)((coded.size() + segLen - 1) / segLen);
        std::vector<int> era = {total, segLen};
        Bytes rx = coded;
        for (int s = 0; s < total; s += 2)
        {
            era.push_back(s);
            for (int j = 0; j < segLen && (size_t)s * segLen + j < rx.size(); j++)
                rx[(size_t)s * segLen + j] = 0;
        }
        reedSolomonCoder dec;
        std::string name;
        Bytes out;
        CHECK(!dec.decode(rx, "pic.rs4", name, out, "rs4", era));
    }
    // wrong extension, missing erasure info, garbage
    {
        reedSolomonCoder dec;
        std::string name;
        Bytes out;
        CHECK(!dec.decode(Bytes(1000, 1), "x.txt", name, out, "txt", {4, 250}));
        CHECK(!dec.decode(Bytes(1000, 1), "x.rs2", name, out, "rs2", {}));
        CHECK(!dec.decode(Bytes(1000, 1), "x.rs2", name, out, "rs2", {4, 250, 99})); // erasure outside the file
        CHECK(!dec.decode(Bytes(), "x.rs2", name, out, "rs2", {1, 1}));
        CHECK(!dec.decode(Bytes(3000, 0x55), "x.rs2", name, out, "rs2", {12, 250}));
    }
}

int main()
{
    test_sha();
    test_lu();
    test_rs();
    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("unit tests passed (sha256, lu, reed-solomon)\n");
    return 0;
}
