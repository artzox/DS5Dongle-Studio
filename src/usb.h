//
// Created by awalol on 2026/3/4.
//

#ifndef DS5_BRIDGE_USB_H
#define DS5_BRIDGE_USB_H

extern uint8_t mute[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)
extern float volume[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)

#endif //DS5_BRIDGE_USB_H

// Present ONLY the wake keyboard (controller away, wake on) so the host keeps a
// device it can be woken from without a phantom gamepad appearing.
void usb_set_idle_pid(bool on);
bool usb_is_idle_pid(void);
// True if the identity the HOST currently has enumerated is the idle one
// (latched at GET_DESCRIPTOR, unlike usb_is_idle_pid which is only intent).
bool usb_served_idle_pid(void);

// Drop the host-set audio control state back to the USB audio default.
// Called when the device is enumerated, because mute[] survives in RAM across a
// re-enumeration while the host's endpoint does not - see the definition.
void usb_audio_controls_reset(void);
