#ifndef BITSTREAM_DECODER_H
#define BITSTREAM_DECODER_H

#include "BitBuffer.h"

const int DEFAULT_MAX_SPLICE = 72;	// limit of bits treated as splice noise between recognised gap patterns

Track scan_flux (const CylHead &cylhead, const std::vector<std::vector<uint32_t>> &flux_revs);
Track scan_flux_mfm_fm (const CylHead &cylhead, const std::vector<std::vector<uint32_t>> &flux_revs, DataRate last_datarate);
Track scan_flux_amiga (const CylHead &cylhead, const std::vector<std::vector<uint32_t>> &flux_revs);
Track scan_flux_gcr (const CylHead &cylhead, const std::vector<std::vector<uint32_t>> &flux_revs);
Track scan_flux_ace (const CylHead &cylhead, const std::vector<std::vector<uint32_t>> &flux_revs);

Track scan_bitstream (const CylHead &cylhead, BitBuffer &bitbuf);
Track scan_bitstream_mfm_fm (const CylHead &cylhead, BitBuffer &bitbuf);
Track scan_bitstream_amiga (const CylHead &cylhead, BitBuffer &bitbuf);
Track scan_bitstream_ace (const CylHead &cylhead, BitBuffer &bitbuf);
Track scan_bitstream_gcr (const CylHead &cylhead, BitBuffer &bitbuf);

#endif // BITSTREAM_DECODER_H
