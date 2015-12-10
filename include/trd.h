#ifndef TRD_H
#define TRD_H

const int TRD_MAX_TRACKS = 128;		// 1MB images
const int TRD_NORM_TRACKS = 80;		// legacy maximum
const int TRD_MAX_SIDES = 2;
const int TRD_SECTORS = 16;
const int TRD_SECTOR_SIZE = 256;
const int TRD_TRACK_SIZE = TRD_SECTOR_SIZE * TRD_SECTORS;
const int TRD_MAXFILES = 128;

const int TRD_SIZE_40_1 = TRD_TRACK_SIZE * 40 * 1;
const int TRD_SIZE_40_2 = TRD_TRACK_SIZE * 40 * 2;
const int TRD_SIZE_80_2 = TRD_TRACK_SIZE * 80 * 2;
const int TRD_SIZE_128_2 = TRD_TRACK_SIZE * 128 * 2;	// 1MB

int SizeToCylsTRD (int nSizeBytes_);

#endif
