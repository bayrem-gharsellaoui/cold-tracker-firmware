#include <zephyr/zbus/zbus.h>

#include "coldtracker_messages.h"

ZBUS_CHAN_DEFINE_WITH_ID(network_status_chan, NETWORK_STATUS_CHAN_ID, struct network_status_msg,
			 NULL, NULL, ZBUS_OBSERVERS_EMPTY,
			 ZBUS_MSG_INIT(.state = NETWORK_STATE_OFFLINE));

ZBUS_CHAN_DEFINE_WITH_ID(time_status_chan, TIME_STATUS_CHAN_ID, struct time_status_msg, NULL, NULL,
			 ZBUS_OBSERVERS_EMPTY,
			 ZBUS_MSG_INIT(.state = TIME_STATE_UNAVAILABLE,
				       .source = TIME_SOURCE_NONE));
