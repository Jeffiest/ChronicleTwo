#include "common.h"

#include <new>

#include "dataread.hpp"
#include "mg_memory.hpp"
#include "snd_mngr.hpp"
#include "snd_seseq.hpp"
#include "sound.hpp"
#include "sound_port.hpp"

extern sndPortInfo PortInfo[SND_PORT_NUM];

namespace {

bool  init_snd = false;
float PortVolf[SND_PORT_NUM] = {};

// Retail's bound lets port == SND_PORT_NUM through and indexes one past the array; kept as retail's.
sndPortInfo *GetPortInfo(int port) {
    if (port < 0 || port > SND_PORT_NUM) {
        return nullptr;
    }

    return &PortInfo[port];
}

int GetCSndPortNo(int port_no, int *port, int *sq_port, int *vol) {
    *vol = -1;

    switch (port_no) {
        case SND_PORT_BGM:
            *port = 0;
            *sq_port = 0;
            break;
        case SND_PORT_OB:
            *port = 15;
            *sq_port = -1;
            *vol = 0x100;
            break;
        case 2:
            *port = 1;
            *sq_port = 1;
            break;
        case SND_PORT_BASE:
            *port = 10;
            *sq_port = -1;
            *vol = 0x100;
            break;
        case SND_PORT_EVENT:
            *port = 14;
            *sq_port = 2;
            *vol = 0x100;
            break;
        case SND_PORT_ENEMY:
            *port = 13;
            *sq_port = -1;
            *vol = 0x100;
            break;
        case SND_PORT_SYSTEM:
            *port = 12;
            *sq_port = -1;
            *vol = 0x100;
            break;
        case 7:
            *port = 9;
            *sq_port = -1;
            *vol = 0x100;
            break;
        case SND_PORT_MENU:
            *port = 11;
            *sq_port = -1;
            *vol = 0x100;
            break;
        case SND_PORT_BGM2:
            *port = 3;
            *sq_port = 3;
            break;
        case 9:
            *port = 8;
            *sq_port = -1;
            *vol = 0x100;
            break;
        case 10:
            *port = 7;
            *sq_port = -1;
            *vol = 0x100;
            break;
        default:
            return 0;
    }

    return 1;
}

} // namespace

// The game's threads are cooperative fibers on one OS thread and nothing inside these sections yields,
// so there is nothing for a semaphore to guard.
PORT_OVERRIDE void sndWaitSema() {}

PORT_OVERRIDE void sndSignalSema() {}

PORT_OVERRIDE void sndInitMngr() {
    if (init_snd) {
        CSnd.Exit();
        init_snd = false;
    }

    CSnd.Init(4, 0, 40, 0);
    sndSetMasterVol(0, 1.0f);
    sndSetMasterVol(1, 1.0f);
    init_snd = true;

    for (int port = 0; port < 16; port++) {
        sndInitPort(port);
        PortVolf[port] = 0.0f;
    }
}

PORT_OVERRIDE void sndSetPortVol(int port_no, float vol) {
    sndPortInfo *info;
    int          driver_vol;

    info = GetPortInfo(port_no);

    if (info == NULL || info->port < 0 || info->port >= 16) {
        return;
    }

    if (vol < 0.0f) {
        vol = 0.0f;
    }

    if (vol > 1.0f) {
        vol = 1.0f;
    }

    PortVolf[port_no] = vol;
    driver_vol = (int) (127.0f * vol);

    if (vol == 1.0f) {
        driver_vol = 0x100;
    }

    sndWaitSema();
    CSnd.SetVol(info->port, driver_vol);
    sndSignalSema();
}

PORT_OVERRIDE float sndGetPortVol(int port) {
    if (port < 0 || port >= SND_PORT_NUM) {
        return 0.0f;
    }

    return PortVolf[port];
}

PORT_OVERRIDE unsigned int sndLoadSound(int port_no, unsigned int *pack, mgCMemory *memory) {
    sndPortInfo  *info;
    sndBankInfo  *bank;
    unsigned int *sq[32];
    int           sq_size[32];
    char         *sq_name[32];
    unsigned int *mid[48];
    int           mid_size[48];
    char         *mid_name[48];
    int           initial_vol;
    unsigned int *config;
    int           config_size;
    unsigned int *volume;
    int           volume_size;
    unsigned int *bd;
    unsigned int *hd;
    int           bd_size;
    int           hd_size;
    int           bank_no;
    unsigned int  size;
    unsigned int  quadwords;
    int           i;
    unsigned int  sound_id;
    char          cfg_ext[] = "cfg";
    char          vol_ext[] = "vol";
    char          bd_ext[] = "bd";
    char          hd_ext[] = "hd";
    char          sq_ext[] = "sq";
    char          mid_ext[] = "mid";

    // Retail's EnableSndMngr gate is dropped: no unit writes it, so it has always been 1.

    info = GetPortInfo(port_no);

    if (info == NULL) {
        return -1;
    }

    initial_vol = -1;

    if (GetCSndPortNo(port_no, &info->port, &info->sq_port, &initial_vol) == 0) {
        return -1;
    }

    sound_id = (port_no & 0xFF) << 24;

    if (GetPackFileExt(pack, cfg_ext, &config, 1, &config_size, NULL) <= 0) {
        return -1;
    }

    if (GetPackFileExt(pack, vol_ext, &volume, 1, &volume_size, NULL) <= 0) {
        return -1;
    }

    bank_no = info->bank_num;

    if (GetPackFileExt(pack, bd_ext, &bd, 1, &bd_size, NULL) <= 0) {
        bd_size = 0;
    }

    if (GetPackFileExt(pack, hd_ext, &hd, 1, &hd_size, NULL) <= 0) {
        hd_size = 0;
    }

    sndWaitSema();
    // Retail's delay loop gave the IOP time before the transfer; the host has no IOP, so only the step remains.
    CSnd.Step();

    if (info->bank_num == 0) {
        if (bd_size != 0 && hd_size != 0) {
            SoundLoadHdBd(info->port, hd, hd_size, bd, bd_size);
        }

        sndWaitTransBd();
        info->bank_num++;
    } else {
        if (info->bank_num > 15) {
            sndSignalSema();
            return -1;
        }

        if (bd_size != 0 && hd_size != 0) {
            SoundLoadHdBdAdd(info->port, hd, hd_size, bd, bd_size);
        }

        sndWaitTransBd();
        info->bank_num++;
    }

    sound_id |= (bank_no & 0xFF) << 16;
    bank = info->GetBank(bank_no);

    if (bank == NULL) {
        sndSignalSema();
        return -1;
    }

    if (port_no == SND_PORT_BGM || port_no == SND_PORT_BGM2 || port_no == 2 || port_no == SND_PORT_EVENT) {
        bank->sq_num = GetPackFileExt(pack, sq_ext, sq, 32, sq_size, sq_name);
        size = bank->sq_num * sizeof(char *);

        if (size & 0xF) {
            quadwords = (size >> 4) + 1;
        } else {
            quadwords = size >> 4;
        }

        bank->sq_name = new (memory->Alloc(quadwords + 2)) char *[bank->sq_num];

        for (i = 0; i < bank->sq_num; i++) {
            bank->sq_name[i] = NULL;
            SoundLoadSeq(info->sq_port, sq[i], sq_size[i]);
            bank->sq_name[i] = mgCopyString(sq_name[i], memory);
        }
    }

    bank->seseq_num = GetPackFileExt(pack, mid_ext, mid, 48, mid_size, mid_name);

    if (bank->seseq_num > 0) {
        size = bank->seseq_num * sizeof(sndCSeSeqData);

        if (size & 0xF) {
            quadwords = (size >> 4) + 1;
        } else {
            quadwords = size >> 4;
        }

        bank->seseq = new (memory->Alloc(quadwords + 2)) sndCSeSeqData[bank->seseq_num];
    }

    for (i = 0; i < bank->seseq_num; i++) {
        bank->seseq[i].name = mgCopyString(mid_name[i], memory);
        bank->seseq[i].LoadSMF((char *) mid[i], mid_size[i], memory);
    }

    if (initial_vol >= 0) {
        CSnd.SetVol(info->port, initial_vol);

        if (port_no >= 0 && port_no <= 16) {
            PortVolf[port_no] = 1.0f;
        }
    }

    info->LoadSeInfoTxt(bank_no, (char *) config, config_size, memory);
    info->LoadVolInfoTxt(bank_no, (char *) volume, volume_size);
    sndSignalSema();
    return sound_id;
}
