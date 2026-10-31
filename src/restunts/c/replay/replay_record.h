#ifndef RESTUNTS_REPLAY_RECORD_H
#define RESTUNTS_REPLAY_RECORD_H

#include "../legacy.h"

void audio_allocate_car_state_records(void);
void set_frame_callback(void);
void remove_frame_callback(void);
void frame_callback(void);
void replay_update_input_tick(legacy_s16 force_neutral_input);
void replay_apply_analog_steering_history(void);

#endif
