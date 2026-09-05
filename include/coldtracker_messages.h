#ifndef COLDTRACKER_MESSAGES_H_
#define COLDTRACKER_MESSAGES_H_

#include <stdbool.h>

#include <zephyr/zbus/zbus.h>

#define NETWORK_STATUS_CHAN_ID 0x1001
#define TIME_STATUS_CHAN_ID    0x1002

enum network_state {
	NETWORK_STATE_OFFLINE = 0,
	NETWORK_STATE_CONNECTING,
	NETWORK_STATE_ONLINE,
};

struct network_status_msg {
	enum network_state state;
};

enum time_state {
	TIME_STATE_UNAVAILABLE = 0,
	TIME_STATE_AVAILABLE,
};

enum time_source {
	TIME_SOURCE_NONE = 0,
	TIME_SOURCE_RTC,
	TIME_SOURCE_SNTP,
};

struct time_status_msg {
	enum time_state state;
	enum time_source source;
};

ZBUS_CHAN_DECLARE(network_status_chan, time_status_chan);

#endif /* COLDTRACKER_MESSAGES_H_ */
