/*
 * api.h - request routing, authentication and the JSON process feed.
 */
#ifndef LTM_API_H
#define LTM_API_H

#include "common.h"

/* Drops every active session. Called whenever the service starts. */
void ltm_api_reset(void);

/* Number of distinct clients that have logged in since the last reset,
 * shown in the desktop window. */
int ltm_api_active_sessions(void);

#endif /* LTM_API_H */
