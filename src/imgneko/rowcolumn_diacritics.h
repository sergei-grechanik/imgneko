// SPDX-License-Identifier: MIT-0

#ifndef IMGNEKO_ROWCOLUMN_DIACRITICS_H
#define IMGNEKO_ROWCOLUMN_DIACRITICS_H

#include <stdint.h>

// The largest row/column number representable by a row/column diacritic.
#define ROWCOLUMN_DIACRITIC_MAX 297

// Return the 1-based row/column number encoded by `code`, or 0 when `code` is
// not a supported row/column diacritic.
uint16_t rowcolumn_diacritic_to_num(uint32_t code);

// Return the diacritic code point that encodes `num`, or 0 when `num` is 0 or
// larger than ROWCOLUMN_DIACRITIC_MAX.
uint32_t rowcolumn_num_to_diacritic(uint32_t num);

#endif
