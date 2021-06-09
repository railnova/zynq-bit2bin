/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2019-2021 Titouan Christophe
 *
 * zynq-bit2bin: convert Xilinx Zynq7000 FPGA bitstreams from the .bit file
 *               format to the .bin file format.
 *
 * This program is distributed under the MIT License
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define MIN(x, y) (x < y) ? x : y

/**
 * @brief      Maximal length of a "meta" field to be found in a .bit file
 */
#define MAX_META_FIELD_LENGTH 256

/**
 * @brief      Constant header for .bit files
 */
static const uint8_t BIT_MAGIC_HEADER[] = {
    0x00, 0x09, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0,
    0x0f, 0xf0, 0x00, 0x00, 0x01
};

/**
 * @brief      Constant header of the actual FPGA firmware within a .bit file
 */
static const uint8_t FIRMWARE_MAGIC_HEADER[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0xbb, 0x11, 0x22, 0x00, 0x44,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/**
 * @brief      Read an unsigned integer from a file,
 *             in the .bit file format endianness
 * @param      input  The input file
 * @param[in]  bytes  The size of the integer to parse, in Bytes
 * @param[out] res    The result will be placed there
 * @return     non-zero on error
 */
static int read_uint(FILE *input, size_t bytes, uint32_t *res)
{
    uint8_t buf[bytes];
    int r = fread(buf, 1, bytes, input);
    if (r != (int) bytes){
        fprintf(stderr, "Error reading a %d Bytes number (%d)\n", (int) bytes, r);
        return -1;
    }

    *res = 0;
    for (size_t i=0; i<bytes; i++){
        *res = (*res << 8) | buf[i];
    }
    return 0;
}

/**
 * @brief      Read the content of a meta field (toolchain, build date, ...)
 *             from a .bit file, and print its value to stderr
 * @param      input  The input file
 * @return     non-zero on error
 */
static int read_meta_field(FILE *input)
{
    uint32_t field_length;
    char buf[MAX_META_FIELD_LENGTH];

    if (read_uint(input, 2, &field_length)){
        return -1;
    }

    if (field_length > MAX_META_FIELD_LENGTH){
        fprintf(stderr, "Encountered a field size too large for me\n");
        return -1;
    }

    int r = fread(buf, 1, field_length, input);
    if (r != (int) field_length){
        fprintf(stderr, "Cannot read the whole field (%d)\n", r);
        return -1;
    }

    fprintf(stderr, "* %s\n", buf);

    return 0;
}

/**
 * @brief      Read a magic header from a file (a leading arbitrary sequence),
 *             and validate its content
 * @param      input     The input file
 * @param[in]  expected  The expected sequence of bytes
 * @param[in]  bytes     The number of bytes in the sequence
 * @return     non-zero on error
 */
static int read_magic_header(FILE *input, const uint8_t *expected, size_t bytes)
{
    uint8_t buf[bytes];

    int r = fread(buf, 1, bytes, input);
    if (r != (int) bytes){
        fprintf(stderr, "Unable to read the magic header\n");
        return -1;
    }

    if (memcmp(buf, expected, bytes)){
        fprintf(stderr, "Invalid magic header\n");
        return -1;
    }

    return 0;
}

/**
 * @brief      Shortcut for read_magic_header() with static arrays
 * @param      input     The input file
 * @param      expected  the expected magic header (must be a static array)
 * @return     like read_magic_header()
 */
#define READ_MAGIC_HEADER(input, expected)\
        read_magic_header(input, expected, sizeof(expected))

/**
 * @brief      Invert the endianness of 32bits unsigned integers in a buffer
 * @param[in+out] buf   The buffer to process
 * @param[in]     n     The size of the buffer to process, in Bytes
 *                      (must be a multiple of 4)
 * @return     non-zero on error
 */
static int invert_u32_endianness(uint8_t *buf, size_t n)
{
    if (n % 4){
        fprintf(stderr,
                "Invalid buffer size %d when attempting to invert u32 endianness\n",
                (int) n);
        return -1;
    }
    uint8_t tmp;
    for (size_t i=0; i<n; i+=4){
        tmp = buf[i];
        buf[i] = buf[i+3];
        buf[i+3] = tmp;

        tmp = buf[i+1];
        buf[i+1] = buf[i+2];
        buf[i+2] = tmp;
    }
    return 0;
}

/**
 * @brief      Extract the binary firmware part from a .bit file into the
 *             naked .bin format, ensuring correct byte ordering
 * @param      input   The input file
 * @param      output  The output file
 * @return     non-zero on error
 */
static int process_firmware(FILE *input, FILE *output)
{
    // 1. Read the firware length, in bytes
    uint32_t fw_length;
    if (read_uint(input, 4, &fw_length)){
        return -1;
    }

    // 2. Make sure we have enough room for the magic header + 1 SYNC word
    if (fw_length < 4 + sizeof(FIRMWARE_MAGIC_HEADER)){
        fprintf(stderr, "The firmware blob is too small (%d)\n", (int) fw_length);
        return -1;
    }

    // 3. Make sure the firmware is 4B aligned
    if (fw_length % 4){
        fprintf(stderr, "The firmware is not 4 Bytes aligned\n");
        return -1;
    }

    // 4. Verify and strip the magic header
    if (READ_MAGIC_HEADER(input, FIRMWARE_MAGIC_HEADER)){
        return -1;
    }
    fw_length -= sizeof(FIRMWARE_MAGIC_HEADER);

    // 5. Copy chunk by chunk
    uint8_t chunk[4096];
    int chunks_done = 0;
    bool invert_endian = false;
    while (fw_length > 0){
        int chunk_len = MIN(sizeof(chunk), fw_length);
        int r = fread(chunk, 1, chunk_len, input);
        if (r != chunk_len){
            fprintf(stderr, "Cannot read first chunk of firmware (%d)\n", r);
            return -1;
        }
        
        // On the first chunk, detect the SYNC word endianness
        if (chunks_done == 0){
            if (memcmp(chunk, "\xaa\x99\x55\x66", 4) == 0){
                invert_endian = true;
            } else if (memcmp(chunk, "\x66\x55\x99\xaa", 4) != 0){
                fprintf(stderr,
                        "Invalid SYNC word (%02hhX%02hhX%02hhX%02hhX)\n",
                        chunk[0], chunk[1], chunk[2], chunk[3]);
                return -1;
            }
        }

        if (invert_endian){
            invert_u32_endianness(chunk, chunk_len);
        }

        r = fwrite(chunk, 1, chunk_len, output);
        if (r != chunk_len){
            fprintf(stderr, "Unable to write to output (%d)\n", r);
            return -1;
        }

        fw_length -= chunk_len;
        chunks_done++;
    }

    return 0;
}

/**
 * @brief      Process a .bit file:
 *             - Convert it to the naked .bin format
 *             - Print meta fields to stderr
 * @param      input   The input file (.bit)
 * @param      output  The output file (.bin)
 * @return     non-zero on error
 */
static int process_bit_file(FILE *input, FILE *output)
{
    READ_MAGIC_HEADER(input, BIT_MAGIC_HEADER);

    // 2. Read fields
    uint8_t field_type;
    do {
        int r = fread(&field_type, 1, 1, input);
        if (r != 1){
            fprintf(stderr, "Cannot read field type (%d)\n", r);
            return -1;
        }

        switch (field_type){
            case 0x61:
            case 0x62:
            case 0x63:
            case 0x64:
                if (read_meta_field(input)){
                    return -1;
                }
                break;

            case 0x65:
                if (process_firmware(input, output)){
                    return -1;
                }
                break;

            default:
                fprintf(stderr, "Unknown field type %02hhX\n", field_type);
                return -1;
        }
    } while (field_type != 0x65);
    return 0;
}

int main(int argc, const char **argv)
{
    (void) argc;
    (void) argv;

    return process_bit_file(stdin, stdout);    
}
