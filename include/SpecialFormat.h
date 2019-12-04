#pragma once

bool IsEmptyTrack (const Track &track);
bool IsKBI19Track (const Track &track);
bool IsSystem24Track (const Track &track);
bool IsSpectrumSpeedlockTrack (const Track &track, int &weak_offset, int &weak_size);
bool IsCpcSpeedlockTrack (const Track &track, int &weak_offset, int &weak_size);
bool IsRainbowArtsTrack (const Track &track, int &weak_offset, int &weak_size);
bool IsKBIWeakSectorTrack (const Track &track, int &weak_offset, int &weak_size);
bool IsLogoProfTrack (const Track &track);
bool IsOperaSoftTrack (const Track &track);
bool Is8KSectorTrack (const Track &track);
bool IsPrehistorikTrack(const Track &track);
bool Is11SectorTrack(const Track &track);
bool IsReussirProtectedTrack (const Track &track);

TrackData GenerateEmptyTrack (const CylHead &cylhead, const Track &track);
TrackData GenerateKBI19Track (const CylHead &cylhead, const Track &track);
TrackData GenerateSpectrumSpeedlockTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size);
TrackData GenerateCpcSpeedlockTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size);
TrackData GenerateRainbowArtsTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size);
TrackData GenerateKBIWeakSectorTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size);
TrackData GenerateLogoProfTrack (const CylHead &cylhead, const Track &track);
TrackData GenerateSystem24Track (const CylHead &cylhead, const Track &track);
TrackData GenerateOperaSoftTrack (const CylHead &cylhead, const Track &track);
TrackData Generate8KSectorTrack (const CylHead &cylhead, const Track &track);
TrackData GeneratePrehistorikTrack(const CylHead &cylhead, const Track &track);
TrackData Generate11SectorTrack(const CylHead &cylhead, const Track &track);
TrackData GenerateReussirProtectedTrack (const CylHead &cylhead, const Track &track);
