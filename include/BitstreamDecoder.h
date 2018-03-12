#ifndef BITSTREAM_DECODER_H
#define BITSTREAM_DECODER_H

#include "BitBuffer.h"

void scan_flux (TrackData &trackdata);
void scan_flux_mfm_fm (TrackData &trackdata, DataRate last_datarate);
void scan_flux_amiga (TrackData &trackdata);
void scan_flux_gcr (TrackData &trackdata);
void scan_flux_ace (TrackData &trackdata);
void scan_flux_mx (TrackData &trackdata, DataRate last_datarate);
void scan_flux_agat (TrackData &trackdata, DataRate last_datarate);
void scan_flux_apple (TrackData &trackdata);

void scan_bitstream (TrackData &trackdata);
void scan_bitstream_mfm_fm (TrackData &trackdata);
void scan_bitstream_amiga (TrackData &trackdata);
void scan_bitstream_ace (TrackData &trackdata);
void scan_bitstream_gcr (TrackData &trackdata);
void scan_bitstream_mx (TrackData &trackdata);
void scan_bitstream_agat (TrackData &trackdata);
void scan_bitstream_apple (TrackData &trackdata);

#endif // BITSTREAM_DECODER_H
