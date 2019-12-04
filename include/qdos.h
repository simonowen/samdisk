#pragma once

// QDOS (Sinclair QL)
//
// https://github.com/slimlogic/qltools/blob/master/docs/format
//
// Note: all values are BIG ENDIAN!

typedef struct
{
    char signature[4];
    char label[10];
    uint16_t format_id;
    uint32_t update_count;
    uint16_t free_sectors;
    uint16_t good_sectors;
    uint16_t total_sectors;
    uint16_t sectors_per_track;
    uint16_t sectors_per_cyl;
    uint16_t cyls_per_side;
    uint16_t sectors_per_block;
    uint16_t dir_end_block;
    uint16_t dir_end_block_size;
    uint16_t offset_per_track;
    uint8_t log_phys_mapping[18];
    uint8_t phys_log_mapping[18];
    uint8_t unused[20];
} QDOS_HEADER;

typedef struct
{
    uint8_t unused0[64];
    uint32_t file_size;
    uint8_t unused1;
    uint8_t file_type;
    uint32_t dataspace;
    uint32_t unused2;
    uint16_t filename_len;
    char filename[36];
    uint32_t file_date;
    uint32_t unused3;
    uint32_t unused4;
} QDOS_DIR;
