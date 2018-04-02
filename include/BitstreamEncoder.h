#ifndef BITSTREAM_ENCODER_H
#define BITSTREAM_ENCODER_H

#include "BitBuffer.h"

bool generate_special (TrackData &trackdata);
void generate_bitstream (TrackData &trackdata);
void generate_flux (TrackData &trackdata);

#endif // BITSTREAM_ENCODER_H
