#ifndef NEO_EFFECT_H_
#define NEO_EFFECT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize effect engine (NeoPixel, gamma LUT, states) */
void    neo_effect_init(void);

/* Any byte received from the host: ends the power-on sky blue dimming */
void    neo_effect_host_seen(void);

/* For ID request (0x00) */
uint8_t neo_effect_get_device_id(void);

/* Handle 1-byte command (except 0x00) */
void    neo_effect_handle_command(uint8_t cmd);

/* Step current effect (call every loop) */
void    neo_effect_step(void);

#ifdef __cplusplus
}
#endif

#endif /* NEO_EFFECT_H_ */
