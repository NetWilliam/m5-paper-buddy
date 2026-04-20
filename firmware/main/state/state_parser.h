#ifndef _STATE_PARSER_H_
#define _STATE_PARSER_H_

#include "tama_state.h"
#include "cJSON.h"

/// Parse a heartbeat JSON object and update TamaState.
void ApplyJson(cJSON* root, TamaState* out);

#endif // _STATE_PARSER_H_
