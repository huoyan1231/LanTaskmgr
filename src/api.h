/*
 * api.h - request routing, authentication and the JSON process feed.
 */
#ifndef LTM_API_H
#define LTM_API_H

#include "common.h"

/* Drops every session and unblocks every address. Called whenever the
 * service starts, which is also how a user un-blocks themselves after
 * fat-fingering the password five times. */
void ltm_api_reset(void);

/* Number of distinct clients that have logged in since the last reset,
 * shown in the desktop window. */
int ltm_api_active_sessions(void);

#endif /* LTM_API_H */
