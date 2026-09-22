//
// Created by awalol on 2026/5/15.
//

#ifndef DS5_BRIDGE_STATE_MGR_H
#define DS5_BRIDGE_STATE_MGR_H
#include <cstdint>

void state_init();
bool state_release_for_suspend();
uint32_t state_synth_interval_ms();
bool state_synth_tick(); // main loop: recompose from live positions + cached host intent; true = state changed, push it
void state_set(uint8_t *data, uint8_t size);
bool player_led_wants_report(void);
bool state_update(const uint8_t *data, uint8_t size);
void set_volume(uint8_t value);
void set_volume(uint8_t speaker, uint8_t headset);

#endif //DS5_BRIDGE_STATE_MGR_H
