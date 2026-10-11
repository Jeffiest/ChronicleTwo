#include "memcard.hpp"

#include <cstdio>
#include <cstring>
#include <sifdev.h>

#include "mainloop.hpp"
#include "title.hpp"

// The host memory card and save layer. The card itself is port/src/sce/libmc.cpp's
// <save>/mc0 and <save>/mc1, a directory per card, and every sceMc* command finishes
// inside the call that issues it. Retail's step machine drives that layer unchanged, so
// the unit stays in ps2/src and only the functions whose retail bodies need something
// the host layer does not have are replaced here:
//
//  - FinishForMC released the library with sceMcEnd, which the host layer does not have;
//  - SaveToMc and SaveOamkeFile cleared the save image's "incomplete" byte with
//    sceMcSeek after writing the file, and the host layer has no seek: the image is
//    built with the byte the finished file holds, so its contents are the same;
//  - GetAllSaveFileInfo listed the card with "/BESCES-51190dkcl??", and the host
//    layer's "?" matches exactly one character, so only the two-digit slots appeared;
//    "*" lists every slot;
//  - CheckOmakeFile read the bonus file's size from directory entry 6, which only
//    holds the file while the card returns entries in creation order. The host layer
//    sorts them, so the entry is found by its name.
//
// The state those functions shared with the unit through file statics (the
// dungeon-tree save flag) moves here with them, and the global the menus call to set
// that flag is replaced so it sets the copy SaveToMc reads.

int  MakeCheckDigit(int mode, char *data, int size);
void MakeMemoryCardFileName(int slot, char *path);

/** Whether dungeon-tree state is included in the next save. */
static short DngTreeSaveFlag;

PORT_OVERRIDE void SetDngTreeFlag(int flag) {
    DngTreeSaveFlag = flag;
}

PORT_OVERRIDE void CMemoryCardManager::FinishForMC() {
    // The host card layer holds no library state, so there is nothing to release.
}

PORT_OVERRIDE int CMemoryCardManager::SaveToMc(int file_no) {
    char          path[0x80];
    int           result;
    int           command;
    MC_CARD_INFO *card;

    MakeMemoryCardFileName(file_no, path);

    if (port == 0 || port == 1) {
        card = &this->card[port];
    } else {
        card = NULL;
    }

    switch (step) {
        case 0:
            if (sceMcSync(MC_NOWAIT, NULL, NULL) != 0) {
                if (card->present == 0 || card->type != sceMcTypePS2) {
                    return MC_STEP_DONE;
                }

                CSaveData *save = GetSaveData();
                transferred = 0;
                total_transferred = 0;
                CSaveDataDungeon *dungeon = &save->save_dungeon;
                CUserDataManager *user_data = &save->user_data;
                strcpy(save_buffer->version, version);
                save_buffer->costume_bit = 0;

                if (save->GetBitFlag(SAVE_FLAG_COSTUME_UNLOCK)) {
                    save_buffer->costume_bit = user_data->GetCostumeBit();
                }

                // Retail wrote one here and cleared it with a seek once the image
                // was on the card; with no seek, the image is built holding the
                // value the finished file carries, and a short file is still caught
                // by the size and checksum checks when it is read back.
                save_buffer->incomplete = 0;
                save_buffer->omake_flag = 0;

                if (save->GetBitFlag(SAVE_FLAG_FINNY_FRENZY_UNLOCKED)) {
                    save_buffer->omake_flag |= OMAKE_ENABLE_GYORACE;
                }

                if (save->GetBitFlag(SAVE_FLAG_COSTUME_UNLOCK)) {
                    save_buffer->omake_flag |= OMAKE_ENABLE_COSTUME;
                    save_buffer->omake_flag |= OMAKE_ENABLE_DUNGEON;
                }

                save_buffer->debug_code = 0;
                save_buffer->unk_1A = 0;
                save_buffer->unk_1C = 0;

                for (int i = 0; i < 11; i++) {
                    save_buffer->unk_4C[i] = 0;
                }

                save_buffer->unk_47 = 0;
                save_buffer->dng_tree_flag = DngTreeSaveFlag;
                save_buffer->unique_counter = CheckMaxUniqueCounter() + 1;
                save_buffer->progress = save->game_progress;
                save_buffer->fish_num = user_data->CountFish();
                save_buffer->program_loop_no = NowProgramLoopNo;
                save_buffer->map_no = save->map_no;
                save_buffer->dungeon_no = dungeon->stage_id;
                save_buffer->floor_id = dungeon->floor_id[dungeon->stage_id];
                printf("UNIQUE : %d\n", (int) save_buffer->unique_counter);
                memcpy(&save_buffer->save_data, save, sizeof(CSaveData));
                save_buffer->unk_20 = 0;
                save_buffer->unk_24 = 0;
                save_buffer->check_digit_half = MakeCheckDigit(0, (char *) &save_buffer->save_data, 0x32C98);
                save_buffer->check_digit = 0;
                save_buffer->check_digit = MakeCheckDigit(0, (char *) &save_buffer->save_data, sizeof(CSaveData));
                printf("checkbit : %d  ,%d\n", save_buffer->check_digit, save_buffer->check_digit_half);
                transfer_size = sizeof(SAVEDATA_FORMAT);
                transfer_result = 0;
                write_buffer = (char *) save_buffer;

                if (sceMcOpen(port, 1, path, SCE_CREAT | SCE_WRONLY) == MC_COMMAND_BUSY) {
                    sceMcSync(MC_NOWAIT, NULL, NULL);
                } else {
                    step = 0x64;
                }
            }

            break;
        case 0x64:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (result < 0) {
                    McError(result);
                    return MC_STEP_DONE;
                }

                fd = result;

                if (sceMcWrite(fd, write_buffer, 0xC00) == 0) {
                    step++;
                    break;
                }

                return MC_STEP_FAILED;
            }

            break;
        case 0x65:
            if (sceMcSync(MC_NOWAIT, &command, &transfer_result) != 0) {
                if (transfer_result < 0) {
                    McError(transfer_result);
                    return MC_STEP_DONE;
                }

                transferred += transfer_result;
                total_transferred += transfer_result;
                int done = transferred;
                int total = transfer_size;

                if (done >= total) {
                    if (sceMcFlush(fd) == 0) {
                        step++;
                        break;
                    }

                    return MC_STEP_FAILED;
                }

                int left = total - done;
                int chunk = 0xC00;

                if (left < 0xC00) {
                    chunk = left;
                }

                sceMcWrite(fd, write_buffer + done, chunk);
            }
            break;
        case 0x66:
            // The flush has finished. Retail sought back to byte 0x45 and wrote it
            // zero there; the image already holds it zero, so the file is closed.
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (result < 0) {
                    McError(result);
                    return MC_STEP_DONE;
                }

                if (sceMcClose(fd) == 0) {
                    step = 0x68;
                    break;
                }

                return MC_STEP_FAILED;
            }

            break;
        case 0x68:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (result < 0) {
                    McError(result);
                    return MC_STEP_DONE;
                }

                step++;
            }

            break;
        case 0x69: {
            SAVEDATA_INFO *info = &file_info[file_no];

            if (info != NULL) {
                info->state = 1;
                info->file_no = file_no;
                UpDateViewInfo(info, save_buffer);
            }

            return MC_STEP_DONE;
        }
    }

    return MC_STEP_BUSY;
}

PORT_OVERRIDE int CMemoryCardManager::SaveOamkeFile() {
    char          path[0x80];
    int           result;
    int           command;
    MC_CARD_INFO *card;

    result = 0;

    if (port == 0 || port == 1) {
        card = &this->card[port];
    } else {
        card = NULL;
    }

    MC_ERROR_INFO *errors = &error;

    switch (step) {
        case 0: {
            int synced = sceMcSync(MC_NOWAIT, NULL, NULL);

            if (McCheckMCPs2(card) == 0) {
                return MC_STEP_DONE;
            }

            if (synced != 0) {
                InitError();
                strcpy(path, "/BESCES-51190dc2omake/");
                strcat(path, icon[2].name);

                if (sceMcOpen(port, 1, path, SCE_RDONLY) == 0) {
                    step++;
                    break;
                }

                return MC_STEP_DONE;
            }

            break;
        }
        case 1:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (0 > result) {
                    McError(result);

                    if (result == sceMcResNoEntry) {
                        errors->code = MC_ERROR_FILE;
                    }

                    return MC_STEP_DONE;
                } else {
                    read_buffer = SubGameOmakeTempBuffer;
                    transfer_result = 0;
                    transferred = 0;
                    total_transferred = 0;
                    fd = result;
                    sceMcRead(fd, read_buffer, icon[2].size);
                    step++;
                }
            }

            break;
        case 2:
            if (McCheckMCPs2(card) == 0) {
                return MC_STEP_DONE;
            }

            if (sceMcSync(MC_NOWAIT, &command, &transfer_result) != 0) {
                if (transfer_result < 0) {
                    McError(transfer_result);
                    return MC_STEP_FAILED;
                }

                if (transfer_result != icon[2].size) {
                    errors->code = MC_ERROR_FILE;
                    McError(result);
                    return MC_STEP_FAILED;
                }

                if (sceMcClose(fd) == 0) {
                    step = 0xA;
                    break;
                }

                return MC_STEP_FAILED;
            }

            break;
        case 0xA:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                CSubGameData *sub_game = GetSubGameSaveData();

                if (sub_game == NULL) {
                    return MC_STEP_DONE;
                }

                transferred = 0;
                total_transferred = 0;
                // Retail wrote one here and cleared it with a seek after the write;
                // the host layer has no seek, so the file carries the clear value.
                sub_game->incomplete = 0;
                transfer_size = GetSaveDataSize(MC_SIZE_OMAKE_FILE);
                transfer_result = 0;
                transferred = 0;
                total_transferred = 0;
                write_buffer = (char *) sub_game;
                sceMcOpen(port, 1, "/BESCES-51190dc2omake/BESCES-51190dc2omake", SCE_CREAT | SCE_WRONLY);
                step = 0x64;
            }

            break;
        case 0x64:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (result < 0) {
                    McError(result);
                    return MC_STEP_DONE;
                }

                fd = result;

                if (sceMcWrite(fd, write_buffer, 0xC00) == 0) {
                    step = 0x6E;
                    break;
                }

                return MC_STEP_FAILED;
            }

            break;
        case 0x6E:
            if (sceMcSync(MC_NOWAIT, &command, &transfer_result) != 0) {
                if (transfer_result < 0) {
                    McError(transfer_result);
                    return MC_STEP_DONE;
                }

                transferred += transfer_result;
                total_transferred += transfer_result;
                int done = transferred;
                int total = transfer_size;

                if (done >= total) {
                    if (sceMcFlush(fd) == 0) {
                        step++;
                        break;
                    }

                    return MC_STEP_FAILED;
                }

                int left = total - done;
                int chunk = 0xC00;

                if (left < 0xC00) {
                    chunk = left;
                }

                sceMcWrite(fd, write_buffer + done, chunk);
            }

            break;
        case 0x6F:
            // The flush has finished; retail sought to the first byte and wrote it
            // zero there, and the file already carries that zero, so close.
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (result < 0) {
                    McError(result);
                    return MC_STEP_DONE;
                }

                if (sceMcClose(fd) == 0) {
                    step = 0x71;
                    break;
                }

                return MC_STEP_FAILED;
            }

            break;
        case 0x71:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (result < 0) {
                    McError(result);
                }

                return MC_STEP_DONE;
            }

            break;
    }

    return MC_STEP_BUSY;
}

PORT_OVERRIDE int CMemoryCardManager::GetAllSaveFileInfo() {
    int result;
    int command;
    int sub_step;

    result = 0;
    /** Save slot whose header is currently being read. */
    static int ReadFileNo = 0;

    switch (step) {
        case 0:
            if (sceMcSync(MC_NOWAIT, NULL, NULL) != 0) {
                InitSaveFileInfoTable();
                total_transferred = 0;
                // Retail's pattern put two '?' after the save name, and the host
                // layer matches a '?' to exactly one character, so only the
                // two-digit slots of a thirteen-slot card would list; '*' lists
                // every slot and still excludes the album and bonus directories.
                char pattern[0x80] = "/BESCES-51190dkcl*";

                if (sceMcGetDir(port, 1, pattern, 0, MC_DIR_TABLE_NUM, dir_table) == 0) {
                    step++;
                }
            }

            break;
        case 1:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                switch (command) {
                    case sceMcFuncNoGetDir:
                        break;
                    default:
                        McError(result);
                        return MC_STEP_DONE;
                }

                ReadFileNo = 0;
                dir_entries = 0;

                if (result >= 0) {
                    dir_entries = result;
                    step++;
                    break;
                }

                MC_ERROR_INFO *errors = &error;

                if (result == sceMcResNoFormat) {
                    errors->code = MC_ERROR_UNFORMATTED;
                }

                errors->func_no = GetFuncNo();
                errors->step = step;
                return MC_STEP_FAILED;
            }

            break;
        default:
            sub_step = step - 2;
            int finished = GetSaveFileInfoFromMc(ReadFileNo, &sub_step);
            step = sub_step + 2;

            if (finished != 0) {
                ReadFileNo++;
            }

            if (ReadFileNo >= MC_SAVE_FILE_NUM) {
                return MC_STEP_DONE;
            }

            break;
    }

    return MC_STEP_BUSY;
}

PORT_OVERRIDE int CMemoryCardManager::CheckOmakeFile() {
    int            command;
    int            result;
    int           *album_found = &file_exists;
    MC_ERROR_INFO *error_record = &error;

    switch (step) {
        case 0:
            if (sceMcSync(MC_NOWAIT, NULL, NULL) != 0) {
                InitSaveFileInfoTable();
                *album_found = 0;

                if (sceMcGetDir(port, 1, "/BESCES-51190dc2omake/*", 0, 0xD, dir_table) == 0) {
                    step = 1;
                } else {
                    error_record->code = MC_ERROR_NONE;
                    return MC_STEP_DONE;
                }
            }

            break;
        case 1:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                dir_entries = 0;

                if (result >= 7) {
                    error_record->code = MC_ERROR_NONE;
                    *album_found = 1;
                    album_found[1] = 0;
                    dir_entries = result;
                    unsigned int file_size = 0;

                    // Retail read entry 6, which held the bonus file while the
                    // card returned entries in creation order; the host layer
                    // sorts them, so the entry is found by its name.
                    for (int i = 0; i < result && i < MC_DIR_TABLE_NUM; i++) {
                        if (strcmp(dir_table[i].entry_name, "BESCES-51190dc2omake") == 0) {
                            file_size = dir_table[i].file_size_byte;
                            break;
                        }
                    }

                    if (file_size < (unsigned int) GetSaveDataSize(MC_SIZE_OMAKE_FILE)) {
                        error_record->code = MC_ERROR_BROKEN;
                        *album_found = 0;
                    }

                    step = 2;
                    sceMcOpen(port, 1, "/BESCES-51190dc2omake/BESCES-51190dc2omake", SCE_RDONLY);
                    break;
                }

                if (result < 0) {
                    McError(result);

                    if (result == sceMcResNoFormat) {
                        error_record->code = MC_ERROR_UNFORMATTED;
                    } else if (result == sceMcResNoEntry) {
                        error_record->code = MC_ERROR_NONE;
                        dir_entries = result;
                        return MC_STEP_DONE;
                    }

                    error_record->func_no = GetFuncNo();
                    error_record->step = step;
                    return MC_STEP_DONE;
                }

                error_record->code = MC_ERROR_FILE;
                *album_found = 2;
                return MC_STEP_DONE;
            }

            break;
        case 2:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (0 > result) {
                    McError(result);
                    return MC_STEP_DONE;
                }

                fd = result;
                sceMcRead(fd, work_buffer, 0x100);

                if (sceMcSync(MC_WAIT, &command, &result) != 0) {
                    if (0 > result) {
                        McError(result);
                        return MC_STEP_DONE;
                    }

                    sceMcClose(fd);
                    step++;
                }
            }

            break;
        case 3:
            if (sceMcSync(MC_NOWAIT, &command, &result) != 0) {
                if (0 > result) {
                    McError(result);
                    return MC_STEP_DONE;
                }

                CSubGameData sub_game;
                memcpy(&sub_game, work_buffer, 0x100);

                if (album_found != NULL) {
                    album_found[1] = sub_game.play_enable;
                }

                return MC_STEP_DONE;
            }

            break;
    }

    return MC_STEP_BUSY;
}
