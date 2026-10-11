#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "scriptinterpreter.hpp"

static int SkipSpace(input_str &in);
static u8  CheckChar(char c);

// Retail copies a stack argument through the operator= of SPI_STACK, which moves the union's
// 32-bit integer member: on the PS2 that is the whole value, a pointer included. A host string
// argument is an eight-byte pointer, so the same copy would keep only its low half and every
// string passed to a tag function would be truncated. The whole argument is moved instead.
void CScriptInterpreter::PushStack(SPI_STACK argument) {
    if (stack_count >= stack_size) {
        printf("SPI stack over!!\n");
        return;
    }

    memcpy(&stack[stack_count], &argument, sizeof(SPI_STACK));
    stack_count++;
}

// Retail reads the last character of an argument with text[(u32) length - 1], which on the PS2
// wraps to text[-1] for an empty argument; on the host the same index addresses four gigabytes
// past the buffer. The empty argument keeps its meaningless last-character test skipped instead.
int CScriptInterpreter::GetArg() {
    char      text[SPI_TOKEN_SIZE];
    SPI_STACK argument;
    int       c;
    int       following;
    int       length;
    int       i;
    char     *value_text;
    int       count;
    int       more;
    int       type;
    int       quotes;
    int       invalid;
    int       non_numeric;

    if (!SkipSpace(*this)) {
        return 0;
    }

    count = 0;
    more = 1;

    do {
        if (!SkipSpace(*this)) {
            return count;
        }

        // Gather the text of one argument, up to a comma or semicolon outside
        // quotes; while this runs, i is non-zero inside quotes.
        length = 0;
        i = 0;

        for (;;) {
            if (!get(&c)) {
                return count;
            }

            if (c == '"') {
                i = !i;
            }

            if (i) {
                if (c & 0x80) {
                    if (c < 0xA1 || c > 0xDF) {
                        // The lead byte of a two-byte Shift-JIS character.
                        text[length++] = c;

                        if (!get(&c)) {
                            return count;
                        }
                    }
                } else if (c == '\\') {
                    if (!get(&following)) {
                        back();
                    } else if (following != '"') {
                        back();
                    } else {
                        c = following;
                    }
                }
            }

            if (!i) {
                if (c == ',') {
                    break;
                }

                if (c == ';') {
                    more = 0;
                    break;
                }
            }

            text[length++] = c;
        }

        if (length == 0 && c == ';') {
            break;
        }

        text[length] = '\0';
        count++;

        // Tell the kind of the argument from its text.
        i = 0;
        type = SPI_STACK_TYPE_INT;
        quotes = 0;
        invalid = 0;
        non_numeric = 0;

        if (text[0] == '"') {
            quotes++;
        }

        if (length > 0 && text[length - 1] == '"') {
            text[length - 1] = '\0';
            quotes++;
        }

        for (; text[i] != '\0'; i++) {
            char ch = text[i];

            if (quotes == 0) {
                if (quotes == 0 && ch == '.') {
                    type = SPI_STACK_TYPE_FLOAT;
                }

                if (CheckChar(ch) && text[i] != '-' && text[i] != '.' && (text[i] < '0' || text[i] > '9')) {
                    non_numeric = 1;
                }

                if (!CheckChar(text[i])) {
                    text[i] = '\0';
                    break;
                }
            }
        }

        if (quotes == 2) {
            type = SPI_STACK_TYPE_STRING;
        }

        if (non_numeric && type != SPI_STACK_TYPE_STRING) {
            invalid = 1;
        }

        if (i == 0) {
            invalid = 1;
        }

        argument.type = type;

        if (invalid) {
            argument.value.string = NULL;
            argument.type = SPI_STACK_TYPE_INVALID;
        }

        value_text = text;

        if (type == SPI_STACK_TYPE_STRING) {
            // The text inside the quotes is kept in the string buffer.
            value_text = &text[1];

            if (string_buff_next + strlen(value_text) + 1 > string_buff + string_buff_size) {
                printf("SPI string buffer over!!\n");
                argument.value.string = NULL;
            } else {
                argument.value.string = string_buff_next;
                strcpy(string_buff_next, value_text);
                string_buff_next += strlen(value_text) + 1;
            }
        }

        if (argument.type == SPI_STACK_TYPE_INT) {
            argument.value.integer = atoi(value_text);
        }

        if (argument.type == SPI_STACK_TYPE_FLOAT) {
            argument.value.real = atof(value_text);
        }

        PushStack(argument);
    } while (more);

    return count;
}

// Moves a reader past spaces, tabs and line breaks;
// gives 1 when text remains to be read and 0 at its end.
static int SkipSpace(input_str &in) {
    char *buffer = in.buffer;
    int   position = in.position;

    while (position < in.size) {
        if (CheckChar(buffer[position])) {
            break;
        }

        position++;
    }

    in.position = position;

    if (position >= in.size) {
        return 0;
    }

    return 1;
}

// Tells whether a character is part of a word: gives 0 for
// a space, a tab or a line break and 1 for anything else.
static u8 CheckChar(char c) {
    int space = 0;

    if (c == ' ') {
        space = 1;
    }

    if (c == '\t') {
        space = 1;
    }

    if (c == '\n') {
        space = 1;
    }

    if (c == '\r') {
        space = 1;
    }

    return (space != 0) ^ 1;
}
