#ifndef COLDTRACKER_STORAGE_H_
#define COLDTRACKER_STORAGE_H_

#include "coldtracker_sample.h"

typedef int (*storage_callback_t)(const struct coldtracker_sample *sample, void *user_data);

int storage_foreach(storage_callback_t callback, void *user_data);

int storage_append(const struct coldtracker_sample *sample);

int storage_peek(struct coldtracker_sample *sample);

int storage_commit(void);

#endif /* COLDTRACKER_STORAGE_H_ */
