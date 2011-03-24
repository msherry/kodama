#ifndef _CALIBRATE_H_
#define _CALIBRATE_H_

/* Hexadecimal representations of the correct return values for dotp() for
 * various sample rates */

/// expected dotp() for 8000 Hz
#define DOTP_1600 (0x426a99c3)
/// expected dotp() for 16000 Hz
#define DOTP_3200 (0x42eaae53)

void calibrate(void);

#endif
