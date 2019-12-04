#pragma once

typedef struct
{
    uint8_t jr_boot[2];         // jump to boot code
    uint8_t cyls;               // tracks
    uint8_t sectors;            // sectors/track
    uint8_t flags;              // flags: b7-6 = fdc size code (128/256/512/1024), b5=0, b4=sides (0=one,1=two), b3-0=0 (on disk)
    uint8_t abCode[250];        // boot code to set up BPB
} OPD_BOOT;

typedef struct
{
    uint8_t last_block_size[2]; // lower 12 bits = size mod 4K
    uint8_t first_block[2];     // first block in file
    uint8_t last_block[2];      // last block in file
    char szName[10];            // file name
} OPUS_DIR;
