#ifndef COLDTRACKER_SAMPLE_H_
#define COLDTRACKER_SAMPLE_H_

#include <stdint.h>

struct coldtracker_sample {
	int64_t timestamp;
	int32_t temperature_mc;
};

#endif /* COLDTRACKER_SAMPLE_H_ */
