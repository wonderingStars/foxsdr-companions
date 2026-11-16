# Checks the constants of upstream/frame.c against the text of PAS 0001-2 v3.0.0 (TETRAPOL Forum, public):
#   python -I check_constants.py <path to upstream/frame.c> <pas.txt made by pdf_text.py>
# The PDF is NOT part of this repository (copyright TETRAPOL Forum); download it yourself, see ../../../UPSTREAM.md.
import re
import sys

src = open(sys.argv[1], encoding='utf-8').read()
pas = open(sys.argv[2], 'rb').read().decode('latin-1')


def table(name):
    m = re.search(re.escape(name) + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', src, re.S)
    return [int(x) for x in re.findall(r'\d+', m.group(1))]


def ok(label, cond):
    print('%-70s %s' % (label, 'OK' if cond else 'DIFFERENT'))


# scrambling sequence: s(k)=1 for k<7, s(k)=s(k-1)^s(k-7)
s = [1] * 7
for k in range(7, 127):
    s.append(s[k - 1] ^ s[k - 7])
ok('scramb_table[127] == s(k) of PAS 6.1.5.1', table('scramb_table') == s)

# VHF interleave: k(j) = 19 p(j mod 8) + (3 floor(j/8) mod 19)
p = {0: 0, 1: 4, 2: 2, 3: 6, 4: 1, 5: 5, 6: 3, 7: 7}
vhf = [19 * p[j % 8] + (3 * (j // 8)) % 19 for j in range(152)]
ok('interleave_voice_VHF == PAS 6.1.3.1 formula', table('interleave_voice_VHF') == vhf)
ok('PAS example k(123) = 121', vhf[123] == 121)

# PRE_COD
pre = [7, 10, 13, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 52, 55, 58, 61, 64, 67, 70, 73, 76,
       83, 86, 89, 92, 95, 98, 101, 104, 107, 110, 113, 116, 119, 122, 125, 128, 131, 134, 137, 140, 143, 146, 149]
ok('diff_precod_UHF == 1 + (j in PRE_COD) of PAS 6.1.4.2', table('diff_precod_UHF') == [1 + (j in pre) for j in range(152)])

# UHF tables as printed in the PAS: the numbers after "K =  [" up to "]"
cands = [m.start() for m in re.finditer(r'K =\s*\[', pas)]
print('K tables in the PAS text:', len(cands))
for i, st in enumerate(cands):
    end = pas.index(']', st)
    nums = [int(x) for x in re.findall(r'\d+', pas[st + 3:end])]
    nums = nums[:152] if len(nums) >= 152 else nums
    which = []
    for name in ('interleave_voice_UHF', 'interleave_data_UHF'):
        if table(name) == nums:
            which.append(name)
    print(' K table', i, 'length', len(nums), 'matches', which or 'none of the UHF tables')
