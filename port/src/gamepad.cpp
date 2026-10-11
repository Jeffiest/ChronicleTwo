#include "gamepad.hpp"

#include <libgraph.h>
#include <libpad.h>

#include "dataread.hpp"
#include "platform/input.hpp"

// The host's keyboard and mouse drive pad 0 next to the first gamepad (platform/input.hpp): every
// read latches the mouse motion since the previous read as this read's right stick, and GetLX and
// GetLY note that the game looked at the left stick, which decides whether the movement keys also
// press the d-pad on the screens that read the d-pad alone.
//
// UpDate and WaitEnable keep retail's bodies because ps2/src/gamepad.cpp's read path is static to
// that unit, so the port holds its own copy to latch where the game actually reads. The two
// controller-thread functions become host ones: the host runs the game on a single thread, and
// UpDate steps vibration where the PS2's controller thread stepped it every vertical blank. The
// input recording lives in host memory instead of the development-kit address gamepad.hpp names.

int pad_button_read(PAD_STATUS *status, int port, int slot) {
    u8  data[32];
    int extended_id;
    int button;

    extended_id = 0;

    InputLatchPad(port);

    if (scePadRead(port, slot, data) == 0) {
        return 0;
    }

    if (data[0] == 0) {
        button = (data[2] << 8) | data[3];
        button = button ^ 0xFFFF;
        status->button = button & 0xFFFF;
        status->right_x = data[4];
        status->right_y = data[5];
        status->left_x = data[6];
        status->left_y = data[7];
        extended_id = data[1] >> 4;
    }

    return extended_id;
}

int read_pad(PAD_STATUS *status, int port, int slot) {
    int  valid;
    int  terminal_id;
    int *phase = &status->phase;
    int *state = &status->state;
    int *extended_id = &status->extended_id;
    int *pad_mode = &status->pad_mode;
    int *previous_pad_mode = &status->previous_pad_mode;

    *state = scePadGetState(port, slot);

    if (*state == scePadStateDiscon) {
        *phase = PAD_PHASE_QUERY;
    }

    valid = false;

    switch (*phase) {
        case PAD_PHASE_QUERY:
            if (*state == scePadStateStable || *state == scePadStateFindCTP1) {
                terminal_id = scePadInfoMode(port, slot, InfoModeCurID, 0);

                if (terminal_id != 0) {
                    *extended_id = scePadInfoMode(port, slot, InfoModeCurExID, 0);

                    if (*extended_id > 0) {
                        terminal_id = *extended_id;
                    }

                    switch (terminal_id) {
                        case PAD_TERMINAL_NEGCON:
                            *phase = PAD_PHASE_READY;
                            break;
                        case PAD_TERMINAL_KONAMI_GUN:
                            *phase = PAD_PHASE_READY;
                            break;
                        case PAD_TERMINAL_DIGITAL:
                            *phase = PAD_PHASE_ANALOG_CHECK;
                            break;
                        case PAD_TERMINAL_ANALOG_JOYSTICK:
                            *phase = PAD_PHASE_READY;
                            break;
                        case PAD_TERMINAL_NAMCO_GUN:
                            *phase = PAD_PHASE_READY;
                            break;
                        case PAD_TERMINAL_DUALSHOCK:
                            *phase = PAD_PHASE_ACTUATOR_CHECK;
                            break;
                        case PAD_TERMINAL_EX_TSURICON:
                            *phase = PAD_PHASE_READY;
                            break;
                        case PAD_TERMINAL_EX_JOGCON:
                            *phase = PAD_PHASE_READY;
                            break;
                        default:
                            *phase = PAD_PHASE_READY;
                            break;
                    }
                }
            }

            break;
        case PAD_PHASE_ANALOG_CHECK:
            if (scePadInfoMode(port, slot, InfoModeCurExID, 0) == 0) {
                *phase = PAD_PHASE_READY;
                break;
            }

            (*phase)++;

        case PAD_PHASE_ANALOG_SET:
            if (scePadSetMainMode(port, slot, 1, 3) == 1) {
                (*phase)++;
            }

            break;
        case PAD_PHASE_ANALOG_WAIT:
            if (scePadGetState(port, slot) != scePadStateExecCmd) {
                *phase = PAD_PHASE_QUERY;
            }

            break;
        case PAD_PHASE_ACTUATOR_CHECK:
            if (scePadInfoAct(port, slot, -1, 0) == 0) {
                *phase = PAD_PHASE_READY;
            }

            status->actuator[0] = 0;
            status->actuator[1] = 1;
            status->actuator[2] = 0xFF;
            status->actuator[3] = 0xFF;
            status->actuator[4] = 0xFF;
            status->actuator[5] = 0xFF;

            if (scePadSetActAlign(port, slot, status->actuator) != 0) {
                (*phase)++;
            }

            break;
        case PAD_PHASE_ACTUATOR_WAIT:
            if (scePadGetState(port, slot) != scePadStateExecCmd) {
                *phase = PAD_PHASE_READY;
            }

            break;
        default:
            if (*state == scePadStateStable || *state == scePadStateFindCTP1) {
                if ((*pad_mode = pad_button_read(status, port, slot)) != 0) {
                    if (*previous_pad_mode != 0 && *pad_mode != *previous_pad_mode) {
                        *previous_pad_mode = 0;
                        *phase = PAD_PHASE_QUERY;
                    } else {
                        valid = true;
                    }

                    *previous_pad_mode = *pad_mode;
                }
            }

            break;
    }

    if (!valid) {
        status->button = 0;
        status->left_y = PAD_ANALOG_CENTER;
        status->left_x = PAD_ANALOG_CENTER;
        status->right_y = PAD_ANALOG_CENTER;
        status->right_x = PAD_ANALOG_CENTER;
    }

    if (*pad_mode == PAD_TERMINAL_DIGITAL) {
        status->left_y = PAD_ANALOG_CENTER;
        status->left_x = PAD_ANALOG_CENTER;
        status->right_y = PAD_ANALOG_CENTER;
        status->right_x = PAD_ANALOG_CENTER;
    }

    return valid;
}

PORT_OVERRIDE void CGamePad::WaitEnable() {
    sceGsSyncV(0);

    while (!read_pad(&pad[0], 0, 0)) {
        sceGsSyncV(0);

        if (scePadGetState(0, 0) == scePadStateDiscon) {
            break;
        }
    }

    while (!read_pad(&pad[1], 1, 0)) {
        sceGsSyncV(0);

        if (scePadGetState(1, 0) == scePadStateDiscon) {
            break;
        }
    }
}

PORT_OVERRIDE void CGamePad::UpDate() {
    int i;
    int j;

    static int cnt = 0;

    previous_pad[0] = pad[0];
    read_pad(&pad[0], 0, 0);
    previous_pad[1] = pad[1];
    read_pad(&pad[1], 1, 0);

    switch (capture_mode) {
        case PAD_CAPTURE_RECORD:
            Capture(&pad[0]);
            break;
        case PAD_CAPTURE_PLAY:
            Play(&pad[0]);
            break;
    }

    for (i = 0; i < 2; i++) {
        int threshold = axis_threshold[i];

        if (threshold < 0) {
            threshold = 0;
        }

        if (threshold > 0) {
            if (threshold < GetLX()) {
                pad[i].button |= PAD_RIGHT;
            }

            if (GetLX() < -threshold) {
                pad[i].button |= PAD_LEFT;
            }

            if (threshold < GetLY()) {
                pad[i].button |= PAD_DOWN;
            }

            if (GetLY() < -threshold) {
                pad[i].button |= PAD_UP;
            }
        }
    }

    for (j = 0; j < 2; j++) {
        int         bit = 1;
        PAD_REPEAT *auto_repeat = &repeat[j];

        for (i = 0; i < 32; i++, bit <<= 1) {
            if (auto_repeat->enabled & bit) {
                if ((pad[j].button & auto_repeat->enabled) & bit) {
                    auto_repeat->counter[i]++;

                    if (auto_repeat->counter[i] >= auto_repeat->initial_delay[i]) {
                        auto_repeat->active |= bit;
                    }
                } else {
                    auto_repeat->counter[i] = 0;
                    auto_repeat->active &= ~bit;
                }

                if (auto_repeat->counter[i] >= auto_repeat->repeat_delay[i] && (auto_repeat->active & bit)) {
                    pad[j].button &= ~bit;
                    auto_repeat->counter[i] = 0;
                }
            }
        }
    }

    for (i = 0; i < 2; i++) {
        if ((pad[i].button & PAD_UP) && (pad[i].button & PAD_DOWN)) {
            pad[i].button &= ~(PAD_UP | PAD_DOWN);
        }

        if ((pad[i].button & PAD_RIGHT) && (pad[i].button & PAD_LEFT)) {
            pad[i].button &= ~(PAD_RIGHT | PAD_LEFT);
        }
    }

    if (key_lock2) {
        pad[1].button = 0;
        pad[1].right_x = PAD_ANALOG_CENTER;
        pad[1].right_y = PAD_ANALOG_CENTER;
        pad[1].left_x = PAD_ANALOG_CENTER;
        pad[1].left_y = PAD_ANALOG_CENTER;
        previous_pad[1].button = 0;
        previous_pad[1].right_x = PAD_ANALOG_CENTER;
        previous_pad[1].right_y = PAD_ANALOG_CENTER;
        previous_pad[1].left_x = PAD_ANALOG_CENTER;
        previous_pad[1].left_y = PAD_ANALOG_CENTER;
    }

    cnt = !cnt;

    // The PS2's controller thread stepped the actuators every vertical blank; the host has no
    // second thread, so each update steps them by one frame.
    Step(1);
    SwitchGamePadThread();
}

int AxisCalibration(int axis) {
    int calibrated = axis - PAD_ANALOG_CENTER;

    if (calibrated < 50 && calibrated > -50) {
        return 0;
    }

    if (calibrated > 0) {
        return ((calibrated - 49) << 7) / 78;
    }

    return ((calibrated + 50) << 7) / 78;
}

PORT_OVERRIDE int CGamePad::GetLX() {
    InputNoteLeftStickRead();
    return AxisCalibration(pad[0].left_x);
}

PORT_OVERRIDE int CGamePad::GetLY() {
    InputNoteLeftStickRead();
    return AxisCalibration(pad[0].left_y);
}

// The PS2 yielded to its controller thread here. The host runs the game on one thread, and UpDate
// has already stepped vibration in the thread's place.
PORT_OVERRIDE void SwitchGamePadThread() {
}

// The PS2's controller thread ran on a second thread; the host has nothing to create, and UpDate
// steps vibration in the thread's place.
PORT_OVERRIDE void CreateGamePadThread(CGamePad *game_pad) {
}

// The PS2 kept the input recording at a fixed development-kit address (PAD_CAPTURE_BUFFER,
// gamepad.hpp); the host keeps it in its own storage, so recording and replaying cannot fault.
static PAD_CAPTURE_FRAME capture_buffer[PAD_CAPTURE_FRAME_MAX];

PORT_OVERRIDE void CGamePad::Capture(PAD_STATUS *status) {
    PAD_CAPTURE_FRAME *frame = capture_buffer;
    u32                index = capture_frame;

    if (index < PAD_CAPTURE_FRAME_MAX) {
        frame += index;
        frame->button = status->button;
        frame->left_y = status->left_y;
        frame->left_x = status->left_x;
        frame->right_y = status->right_y;
        frame->right_x = status->right_x;
        capture_frame++;
    }
}

PORT_OVERRIDE void CGamePad::Play(PAD_STATUS *status) {
    PAD_CAPTURE_FRAME *frame;
    u32                index = capture_frame;

    if (index < PAD_CAPTURE_FRAME_MAX) {
        frame = capture_buffer;
        frame += index;
        status->button = frame->button;
        status->left_y = frame->left_y;
        status->left_x = frame->left_x;
        status->right_y = frame->right_y;
        status->right_x = frame->right_x;
        capture_frame++;
    }
}

PORT_OVERRIDE void CGamePad::SaveCapture() {
    // The data layer's paths are the PS2's writable char *; the port keeps its own.
    static char path[] = "host0:key_cap.bin";
    WriteFile(path, capture_buffer, capture_frame * sizeof(PAD_CAPTURE_FRAME));
}

PORT_OVERRIDE void CGamePad::LoadCapture() {
    static char path[] = "key_cap.bin";
    static char empty[] = "";
    SetCurrentDir(empty);
    LoadFile2(path, capture_buffer, NULL, LOAD_FILE_READ);
    SetCurrentDir(NULL);
}
