// SPDX-License-Identifier: Unicode-3.0

// Convert between row/column numbers, the Unicode combining marks
// used by image placeholders, and their UTF-8 byte strings.
//
// This file is generated from tools/rowcolumn-diacritics.txt,
// which is derived from UnicodeData.txt. Regenerate it with
// tools/generate-rowcolumn-helpers.py; do not edit it directly.

#include <stdint.h>

// clang-format off
static const uint32_t rowcolumn_diacritic_codes[] = {
    0x305,   0x30d,   0x30e,   0x310,   0x312,   0x33d,   0x33e,   0x33f,
    0x346,   0x34a,   0x34b,   0x34c,   0x350,   0x351,   0x352,   0x357,
    0x35b,   0x363,   0x364,   0x365,   0x366,   0x367,   0x368,   0x369,
    0x36a,   0x36b,   0x36c,   0x36d,   0x36e,   0x36f,   0x483,   0x484,
    0x485,   0x486,   0x487,   0x592,   0x593,   0x594,   0x595,   0x597,
    0x598,   0x599,   0x59c,   0x59d,   0x59e,   0x59f,   0x5a0,   0x5a1,
    0x5a8,   0x5a9,   0x5ab,   0x5ac,   0x5af,   0x5c4,   0x610,   0x611,
    0x612,   0x613,   0x614,   0x615,   0x616,   0x617,   0x657,   0x658,
    0x659,   0x65a,   0x65b,   0x65d,   0x65e,   0x6d6,   0x6d7,   0x6d8,
    0x6d9,   0x6da,   0x6db,   0x6dc,   0x6df,   0x6e0,   0x6e1,   0x6e2,
    0x6e4,   0x6e7,   0x6e8,   0x6eb,   0x6ec,   0x730,   0x732,   0x733,
    0x735,   0x736,   0x73a,   0x73d,   0x73f,   0x740,   0x741,   0x743,
    0x745,   0x747,   0x749,   0x74a,   0x7eb,   0x7ec,   0x7ed,   0x7ee,
    0x7ef,   0x7f0,   0x7f1,   0x7f3,   0x816,   0x817,   0x818,   0x819,
    0x81b,   0x81c,   0x81d,   0x81e,   0x81f,   0x820,   0x821,   0x822,
    0x823,   0x825,   0x826,   0x827,   0x829,   0x82a,   0x82b,   0x82c,
    0x82d,   0x951,   0x953,   0x954,   0xf82,   0xf83,   0xf86,   0xf87,
    0x135d,  0x135e,  0x135f,  0x17dd,  0x193a,  0x1a17,  0x1a75,  0x1a76,
    0x1a77,  0x1a78,  0x1a79,  0x1a7a,  0x1a7b,  0x1a7c,  0x1b6b,  0x1b6d,
    0x1b6e,  0x1b6f,  0x1b70,  0x1b71,  0x1b72,  0x1b73,  0x1cd0,  0x1cd1,
    0x1cd2,  0x1cda,  0x1cdb,  0x1ce0,  0x1dc0,  0x1dc1,  0x1dc3,  0x1dc4,
    0x1dc5,  0x1dc6,  0x1dc7,  0x1dc8,  0x1dc9,  0x1dcb,  0x1dcc,  0x1dd1,
    0x1dd2,  0x1dd3,  0x1dd4,  0x1dd5,  0x1dd6,  0x1dd7,  0x1dd8,  0x1dd9,
    0x1dda,  0x1ddb,  0x1ddc,  0x1ddd,  0x1dde,  0x1ddf,  0x1de0,  0x1de1,
    0x1de2,  0x1de3,  0x1de4,  0x1de5,  0x1de6,  0x1dfe,  0x20d0,  0x20d1,
    0x20d4,  0x20d5,  0x20d6,  0x20d7,  0x20db,  0x20dc,  0x20e1,  0x20e7,
    0x20e9,  0x20f0,  0x2cef,  0x2cf0,  0x2cf1,  0x2de0,  0x2de1,  0x2de2,
    0x2de3,  0x2de4,  0x2de5,  0x2de6,  0x2de7,  0x2de8,  0x2de9,  0x2dea,
    0x2deb,  0x2dec,  0x2ded,  0x2dee,  0x2def,  0x2df0,  0x2df1,  0x2df2,
    0x2df3,  0x2df4,  0x2df5,  0x2df6,  0x2df7,  0x2df8,  0x2df9,  0x2dfa,
    0x2dfb,  0x2dfc,  0x2dfd,  0x2dfe,  0x2dff,  0xa66f,  0xa67c,  0xa67d,
    0xa6f0,  0xa6f1,  0xa8e0,  0xa8e1,  0xa8e2,  0xa8e3,  0xa8e4,  0xa8e5,
    0xa8e6,  0xa8e7,  0xa8e8,  0xa8e9,  0xa8ea,  0xa8eb,  0xa8ec,  0xa8ed,
    0xa8ee,  0xa8ef,  0xa8f0,  0xa8f1,  0xaab0,  0xaab2,  0xaab3,  0xaab7,
    0xaab8,  0xaabe,  0xaabf,  0xaac1,  0xfe20,  0xfe21,  0xfe22,  0xfe23,
    0xfe24,  0xfe25,  0xfe26,  0x10a0f, 0x10a38, 0x1d185, 0x1d186, 0x1d187,
    0x1d188, 0x1d189, 0x1d1aa, 0x1d1ab, 0x1d1ac, 0x1d1ad, 0x1d242, 0x1d243,
    0x1d244,
};

static const char rowcolumn_diacritic_utf8[][5] = {
    "\xcc\x85",         "\xcc\x8d",         "\xcc\x8e",         "\xcc\x90",
    "\xcc\x92",         "\xcc\xbd",         "\xcc\xbe",         "\xcc\xbf",
    "\xcd\x86",         "\xcd\x8a",         "\xcd\x8b",         "\xcd\x8c",
    "\xcd\x90",         "\xcd\x91",         "\xcd\x92",         "\xcd\x97",
    "\xcd\x9b",         "\xcd\xa3",         "\xcd\xa4",         "\xcd\xa5",
    "\xcd\xa6",         "\xcd\xa7",         "\xcd\xa8",         "\xcd\xa9",
    "\xcd\xaa",         "\xcd\xab",         "\xcd\xac",         "\xcd\xad",
    "\xcd\xae",         "\xcd\xaf",         "\xd2\x83",         "\xd2\x84",
    "\xd2\x85",         "\xd2\x86",         "\xd2\x87",         "\xd6\x92",
    "\xd6\x93",         "\xd6\x94",         "\xd6\x95",         "\xd6\x97",
    "\xd6\x98",         "\xd6\x99",         "\xd6\x9c",         "\xd6\x9d",
    "\xd6\x9e",         "\xd6\x9f",         "\xd6\xa0",         "\xd6\xa1",
    "\xd6\xa8",         "\xd6\xa9",         "\xd6\xab",         "\xd6\xac",
    "\xd6\xaf",         "\xd7\x84",         "\xd8\x90",         "\xd8\x91",
    "\xd8\x92",         "\xd8\x93",         "\xd8\x94",         "\xd8\x95",
    "\xd8\x96",         "\xd8\x97",         "\xd9\x97",         "\xd9\x98",
    "\xd9\x99",         "\xd9\x9a",         "\xd9\x9b",         "\xd9\x9d",
    "\xd9\x9e",         "\xdb\x96",         "\xdb\x97",         "\xdb\x98",
    "\xdb\x99",         "\xdb\x9a",         "\xdb\x9b",         "\xdb\x9c",
    "\xdb\x9f",         "\xdb\xa0",         "\xdb\xa1",         "\xdb\xa2",
    "\xdb\xa4",         "\xdb\xa7",         "\xdb\xa8",         "\xdb\xab",
    "\xdb\xac",         "\xdc\xb0",         "\xdc\xb2",         "\xdc\xb3",
    "\xdc\xb5",         "\xdc\xb6",         "\xdc\xba",         "\xdc\xbd",
    "\xdc\xbf",         "\xdd\x80",         "\xdd\x81",         "\xdd\x83",
    "\xdd\x85",         "\xdd\x87",         "\xdd\x89",         "\xdd\x8a",
    "\xdf\xab",         "\xdf\xac",         "\xdf\xad",         "\xdf\xae",
    "\xdf\xaf",         "\xdf\xb0",         "\xdf\xb1",         "\xdf\xb3",
    "\xe0\xa0\x96",     "\xe0\xa0\x97",     "\xe0\xa0\x98",     "\xe0\xa0\x99",
    "\xe0\xa0\x9b",     "\xe0\xa0\x9c",     "\xe0\xa0\x9d",     "\xe0\xa0\x9e",
    "\xe0\xa0\x9f",     "\xe0\xa0\xa0",     "\xe0\xa0\xa1",     "\xe0\xa0\xa2",
    "\xe0\xa0\xa3",     "\xe0\xa0\xa5",     "\xe0\xa0\xa6",     "\xe0\xa0\xa7",
    "\xe0\xa0\xa9",     "\xe0\xa0\xaa",     "\xe0\xa0\xab",     "\xe0\xa0\xac",
    "\xe0\xa0\xad",     "\xe0\xa5\x91",     "\xe0\xa5\x93",     "\xe0\xa5\x94",
    "\xe0\xbe\x82",     "\xe0\xbe\x83",     "\xe0\xbe\x86",     "\xe0\xbe\x87",
    "\xe1\x8d\x9d",     "\xe1\x8d\x9e",     "\xe1\x8d\x9f",     "\xe1\x9f\x9d",
    "\xe1\xa4\xba",     "\xe1\xa8\x97",     "\xe1\xa9\xb5",     "\xe1\xa9\xb6",
    "\xe1\xa9\xb7",     "\xe1\xa9\xb8",     "\xe1\xa9\xb9",     "\xe1\xa9\xba",
    "\xe1\xa9\xbb",     "\xe1\xa9\xbc",     "\xe1\xad\xab",     "\xe1\xad\xad",
    "\xe1\xad\xae",     "\xe1\xad\xaf",     "\xe1\xad\xb0",     "\xe1\xad\xb1",
    "\xe1\xad\xb2",     "\xe1\xad\xb3",     "\xe1\xb3\x90",     "\xe1\xb3\x91",
    "\xe1\xb3\x92",     "\xe1\xb3\x9a",     "\xe1\xb3\x9b",     "\xe1\xb3\xa0",
    "\xe1\xb7\x80",     "\xe1\xb7\x81",     "\xe1\xb7\x83",     "\xe1\xb7\x84",
    "\xe1\xb7\x85",     "\xe1\xb7\x86",     "\xe1\xb7\x87",     "\xe1\xb7\x88",
    "\xe1\xb7\x89",     "\xe1\xb7\x8b",     "\xe1\xb7\x8c",     "\xe1\xb7\x91",
    "\xe1\xb7\x92",     "\xe1\xb7\x93",     "\xe1\xb7\x94",     "\xe1\xb7\x95",
    "\xe1\xb7\x96",     "\xe1\xb7\x97",     "\xe1\xb7\x98",     "\xe1\xb7\x99",
    "\xe1\xb7\x9a",     "\xe1\xb7\x9b",     "\xe1\xb7\x9c",     "\xe1\xb7\x9d",
    "\xe1\xb7\x9e",     "\xe1\xb7\x9f",     "\xe1\xb7\xa0",     "\xe1\xb7\xa1",
    "\xe1\xb7\xa2",     "\xe1\xb7\xa3",     "\xe1\xb7\xa4",     "\xe1\xb7\xa5",
    "\xe1\xb7\xa6",     "\xe1\xb7\xbe",     "\xe2\x83\x90",     "\xe2\x83\x91",
    "\xe2\x83\x94",     "\xe2\x83\x95",     "\xe2\x83\x96",     "\xe2\x83\x97",
    "\xe2\x83\x9b",     "\xe2\x83\x9c",     "\xe2\x83\xa1",     "\xe2\x83\xa7",
    "\xe2\x83\xa9",     "\xe2\x83\xb0",     "\xe2\xb3\xaf",     "\xe2\xb3\xb0",
    "\xe2\xb3\xb1",     "\xe2\xb7\xa0",     "\xe2\xb7\xa1",     "\xe2\xb7\xa2",
    "\xe2\xb7\xa3",     "\xe2\xb7\xa4",     "\xe2\xb7\xa5",     "\xe2\xb7\xa6",
    "\xe2\xb7\xa7",     "\xe2\xb7\xa8",     "\xe2\xb7\xa9",     "\xe2\xb7\xaa",
    "\xe2\xb7\xab",     "\xe2\xb7\xac",     "\xe2\xb7\xad",     "\xe2\xb7\xae",
    "\xe2\xb7\xaf",     "\xe2\xb7\xb0",     "\xe2\xb7\xb1",     "\xe2\xb7\xb2",
    "\xe2\xb7\xb3",     "\xe2\xb7\xb4",     "\xe2\xb7\xb5",     "\xe2\xb7\xb6",
    "\xe2\xb7\xb7",     "\xe2\xb7\xb8",     "\xe2\xb7\xb9",     "\xe2\xb7\xba",
    "\xe2\xb7\xbb",     "\xe2\xb7\xbc",     "\xe2\xb7\xbd",     "\xe2\xb7\xbe",
    "\xe2\xb7\xbf",     "\xea\x99\xaf",     "\xea\x99\xbc",     "\xea\x99\xbd",
    "\xea\x9b\xb0",     "\xea\x9b\xb1",     "\xea\xa3\xa0",     "\xea\xa3\xa1",
    "\xea\xa3\xa2",     "\xea\xa3\xa3",     "\xea\xa3\xa4",     "\xea\xa3\xa5",
    "\xea\xa3\xa6",     "\xea\xa3\xa7",     "\xea\xa3\xa8",     "\xea\xa3\xa9",
    "\xea\xa3\xaa",     "\xea\xa3\xab",     "\xea\xa3\xac",     "\xea\xa3\xad",
    "\xea\xa3\xae",     "\xea\xa3\xaf",     "\xea\xa3\xb0",     "\xea\xa3\xb1",
    "\xea\xaa\xb0",     "\xea\xaa\xb2",     "\xea\xaa\xb3",     "\xea\xaa\xb7",
    "\xea\xaa\xb8",     "\xea\xaa\xbe",     "\xea\xaa\xbf",     "\xea\xab\x81",
    "\xef\xb8\xa0",     "\xef\xb8\xa1",     "\xef\xb8\xa2",     "\xef\xb8\xa3",
    "\xef\xb8\xa4",     "\xef\xb8\xa5",     "\xef\xb8\xa6",     "\xf0\x90\xa8\x8f",
    "\xf0\x90\xa8\xb8", "\xf0\x9d\x86\x85", "\xf0\x9d\x86\x86", "\xf0\x9d\x86\x87",
    "\xf0\x9d\x86\x88", "\xf0\x9d\x86\x89", "\xf0\x9d\x86\xaa", "\xf0\x9d\x86\xab",
    "\xf0\x9d\x86\xac", "\xf0\x9d\x86\xad", "\xf0\x9d\x89\x82", "\xf0\x9d\x89\x83",
    "\xf0\x9d\x89\x84",
};

static const uint8_t rowcolumn_diacritic_utf8_lens[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4,
};
// clang-format on

static const uint16_t rowcolumn_diacritic_count =
    sizeof(rowcolumn_diacritic_codes) / sizeof(rowcolumn_diacritic_codes[0]);

uint16_t rowcolumn_diacritic_to_num(uint32_t code) {
    switch (code) {
    case 0x305:
        return code - 0x305 + 1;
    case 0x30d:
    case 0x30e:
        return code - 0x30d + 2;
    case 0x310:
        return code - 0x310 + 4;
    case 0x312:
        return code - 0x312 + 5;
    case 0x33d:
    case 0x33e:
    case 0x33f:
        return code - 0x33d + 6;
    case 0x346:
        return code - 0x346 + 9;
    case 0x34a:
    case 0x34b:
    case 0x34c:
        return code - 0x34a + 10;
    case 0x350:
    case 0x351:
    case 0x352:
        return code - 0x350 + 13;
    case 0x357:
        return code - 0x357 + 16;
    case 0x35b:
        return code - 0x35b + 17;
    case 0x363:
    case 0x364:
    case 0x365:
    case 0x366:
    case 0x367:
    case 0x368:
    case 0x369:
    case 0x36a:
    case 0x36b:
    case 0x36c:
    case 0x36d:
    case 0x36e:
    case 0x36f:
        return code - 0x363 + 18;
    case 0x483:
    case 0x484:
    case 0x485:
    case 0x486:
    case 0x487:
        return code - 0x483 + 31;
    case 0x592:
    case 0x593:
    case 0x594:
    case 0x595:
        return code - 0x592 + 36;
    case 0x597:
    case 0x598:
    case 0x599:
        return code - 0x597 + 40;
    case 0x59c:
    case 0x59d:
    case 0x59e:
    case 0x59f:
    case 0x5a0:
    case 0x5a1:
        return code - 0x59c + 43;
    case 0x5a8:
    case 0x5a9:
        return code - 0x5a8 + 49;
    case 0x5ab:
    case 0x5ac:
        return code - 0x5ab + 51;
    case 0x5af:
        return code - 0x5af + 53;
    case 0x5c4:
        return code - 0x5c4 + 54;
    case 0x610:
    case 0x611:
    case 0x612:
    case 0x613:
    case 0x614:
    case 0x615:
    case 0x616:
    case 0x617:
        return code - 0x610 + 55;
    case 0x657:
    case 0x658:
    case 0x659:
    case 0x65a:
    case 0x65b:
        return code - 0x657 + 63;
    case 0x65d:
    case 0x65e:
        return code - 0x65d + 68;
    case 0x6d6:
    case 0x6d7:
    case 0x6d8:
    case 0x6d9:
    case 0x6da:
    case 0x6db:
    case 0x6dc:
        return code - 0x6d6 + 70;
    case 0x6df:
    case 0x6e0:
    case 0x6e1:
    case 0x6e2:
        return code - 0x6df + 77;
    case 0x6e4:
        return code - 0x6e4 + 81;
    case 0x6e7:
    case 0x6e8:
        return code - 0x6e7 + 82;
    case 0x6eb:
    case 0x6ec:
        return code - 0x6eb + 84;
    case 0x730:
        return code - 0x730 + 86;
    case 0x732:
    case 0x733:
        return code - 0x732 + 87;
    case 0x735:
    case 0x736:
        return code - 0x735 + 89;
    case 0x73a:
        return code - 0x73a + 91;
    case 0x73d:
        return code - 0x73d + 92;
    case 0x73f:
    case 0x740:
    case 0x741:
        return code - 0x73f + 93;
    case 0x743:
        return code - 0x743 + 96;
    case 0x745:
        return code - 0x745 + 97;
    case 0x747:
        return code - 0x747 + 98;
    case 0x749:
    case 0x74a:
        return code - 0x749 + 99;
    case 0x7eb:
    case 0x7ec:
    case 0x7ed:
    case 0x7ee:
    case 0x7ef:
    case 0x7f0:
    case 0x7f1:
        return code - 0x7eb + 101;
    case 0x7f3:
        return code - 0x7f3 + 108;
    case 0x816:
    case 0x817:
    case 0x818:
    case 0x819:
        return code - 0x816 + 109;
    case 0x81b:
    case 0x81c:
    case 0x81d:
    case 0x81e:
    case 0x81f:
    case 0x820:
    case 0x821:
    case 0x822:
    case 0x823:
        return code - 0x81b + 113;
    case 0x825:
    case 0x826:
    case 0x827:
        return code - 0x825 + 122;
    case 0x829:
    case 0x82a:
    case 0x82b:
    case 0x82c:
    case 0x82d:
        return code - 0x829 + 125;
    case 0x951:
        return code - 0x951 + 130;
    case 0x953:
    case 0x954:
        return code - 0x953 + 131;
    case 0xf82:
    case 0xf83:
        return code - 0xf82 + 133;
    case 0xf86:
    case 0xf87:
        return code - 0xf86 + 135;
    case 0x135d:
    case 0x135e:
    case 0x135f:
        return code - 0x135d + 137;
    case 0x17dd:
        return code - 0x17dd + 140;
    case 0x193a:
        return code - 0x193a + 141;
    case 0x1a17:
        return code - 0x1a17 + 142;
    case 0x1a75:
    case 0x1a76:
    case 0x1a77:
    case 0x1a78:
    case 0x1a79:
    case 0x1a7a:
    case 0x1a7b:
    case 0x1a7c:
        return code - 0x1a75 + 143;
    case 0x1b6b:
        return code - 0x1b6b + 151;
    case 0x1b6d:
    case 0x1b6e:
    case 0x1b6f:
    case 0x1b70:
    case 0x1b71:
    case 0x1b72:
    case 0x1b73:
        return code - 0x1b6d + 152;
    case 0x1cd0:
    case 0x1cd1:
    case 0x1cd2:
        return code - 0x1cd0 + 159;
    case 0x1cda:
    case 0x1cdb:
        return code - 0x1cda + 162;
    case 0x1ce0:
        return code - 0x1ce0 + 164;
    case 0x1dc0:
    case 0x1dc1:
        return code - 0x1dc0 + 165;
    case 0x1dc3:
    case 0x1dc4:
    case 0x1dc5:
    case 0x1dc6:
    case 0x1dc7:
    case 0x1dc8:
    case 0x1dc9:
        return code - 0x1dc3 + 167;
    case 0x1dcb:
    case 0x1dcc:
        return code - 0x1dcb + 174;
    case 0x1dd1:
    case 0x1dd2:
    case 0x1dd3:
    case 0x1dd4:
    case 0x1dd5:
    case 0x1dd6:
    case 0x1dd7:
    case 0x1dd8:
    case 0x1dd9:
    case 0x1dda:
    case 0x1ddb:
    case 0x1ddc:
    case 0x1ddd:
    case 0x1dde:
    case 0x1ddf:
    case 0x1de0:
    case 0x1de1:
    case 0x1de2:
    case 0x1de3:
    case 0x1de4:
    case 0x1de5:
    case 0x1de6:
        return code - 0x1dd1 + 176;
    case 0x1dfe:
        return code - 0x1dfe + 198;
    case 0x20d0:
    case 0x20d1:
        return code - 0x20d0 + 199;
    case 0x20d4:
    case 0x20d5:
    case 0x20d6:
    case 0x20d7:
        return code - 0x20d4 + 201;
    case 0x20db:
    case 0x20dc:
        return code - 0x20db + 205;
    case 0x20e1:
        return code - 0x20e1 + 207;
    case 0x20e7:
        return code - 0x20e7 + 208;
    case 0x20e9:
        return code - 0x20e9 + 209;
    case 0x20f0:
        return code - 0x20f0 + 210;
    case 0x2cef:
    case 0x2cf0:
    case 0x2cf1:
        return code - 0x2cef + 211;
    case 0x2de0:
    case 0x2de1:
    case 0x2de2:
    case 0x2de3:
    case 0x2de4:
    case 0x2de5:
    case 0x2de6:
    case 0x2de7:
    case 0x2de8:
    case 0x2de9:
    case 0x2dea:
    case 0x2deb:
    case 0x2dec:
    case 0x2ded:
    case 0x2dee:
    case 0x2def:
    case 0x2df0:
    case 0x2df1:
    case 0x2df2:
    case 0x2df3:
    case 0x2df4:
    case 0x2df5:
    case 0x2df6:
    case 0x2df7:
    case 0x2df8:
    case 0x2df9:
    case 0x2dfa:
    case 0x2dfb:
    case 0x2dfc:
    case 0x2dfd:
    case 0x2dfe:
    case 0x2dff:
        return code - 0x2de0 + 214;
    case 0xa66f:
        return code - 0xa66f + 246;
    case 0xa67c:
    case 0xa67d:
        return code - 0xa67c + 247;
    case 0xa6f0:
    case 0xa6f1:
        return code - 0xa6f0 + 249;
    case 0xa8e0:
    case 0xa8e1:
    case 0xa8e2:
    case 0xa8e3:
    case 0xa8e4:
    case 0xa8e5:
    case 0xa8e6:
    case 0xa8e7:
    case 0xa8e8:
    case 0xa8e9:
    case 0xa8ea:
    case 0xa8eb:
    case 0xa8ec:
    case 0xa8ed:
    case 0xa8ee:
    case 0xa8ef:
    case 0xa8f0:
    case 0xa8f1:
        return code - 0xa8e0 + 251;
    case 0xaab0:
        return code - 0xaab0 + 269;
    case 0xaab2:
    case 0xaab3:
        return code - 0xaab2 + 270;
    case 0xaab7:
    case 0xaab8:
        return code - 0xaab7 + 272;
    case 0xaabe:
    case 0xaabf:
        return code - 0xaabe + 274;
    case 0xaac1:
        return code - 0xaac1 + 276;
    case 0xfe20:
    case 0xfe21:
    case 0xfe22:
    case 0xfe23:
    case 0xfe24:
    case 0xfe25:
    case 0xfe26:
        return code - 0xfe20 + 277;
    case 0x10a0f:
        return code - 0x10a0f + 284;
    case 0x10a38:
        return code - 0x10a38 + 285;
    case 0x1d185:
    case 0x1d186:
    case 0x1d187:
    case 0x1d188:
    case 0x1d189:
        return code - 0x1d185 + 286;
    case 0x1d1aa:
    case 0x1d1ab:
    case 0x1d1ac:
    case 0x1d1ad:
        return code - 0x1d1aa + 291;
    case 0x1d242:
    case 0x1d243:
    case 0x1d244:
        return code - 0x1d242 + 295;
    }
    return 0;
}

uint32_t rowcolumn_num_to_diacritic(uint32_t num) {
    if (num == 0 || num > rowcolumn_diacritic_count)
        return 0;

    return rowcolumn_diacritic_codes[num - 1];
}

const char *rowcolumn_num_to_diacritic_utf8(uint32_t num, uint8_t *len_out) {
    if (len_out != 0)
        *len_out = 0;

    if (num == 0 || num > rowcolumn_diacritic_count)
        return 0;

    if (len_out != 0)
        *len_out = rowcolumn_diacritic_utf8_lens[num - 1];
    return rowcolumn_diacritic_utf8[num - 1];
}
