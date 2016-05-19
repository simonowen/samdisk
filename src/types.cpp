// Macro magic for supported disk types

#include "SAMdisk.h"

#ifdef DECLARATIONS_ONLY

extern FILE_TYPE aTypes[];

#define ADD_TYPE_RW(x)		bool Read##x (MemFile&, std::shared_ptr<Disk> &); \
							bool Write##x (FILE*,   std::shared_ptr<Disk> &);
#define ADD_TYPE_RO(x)		bool Read##x (MemFile&, std::shared_ptr<Disk> &);
#define ADD_TYPE_WO(x)		bool Write##x (FILE*,   std::shared_ptr<Disk> &);
#define ADD_HIDDEN_RO(x)	bool Read##x (MemFile&, std::shared_ptr<Disk> &);

#else

#include "types.h"

#define ADD_TYPE_RW(x)		{ #x, Read##x, Write##x },
#define ADD_TYPE_RO(x)		{ #x, Read##x, nullptr },
#define ADD_TYPE_WO(x)		{ #x, nullptr, Write##x },
#define ADD_HIDDEN_RO(x)	{ "", Read##x, nullptr },

FILE_TYPE aTypes[] = {

#endif


// Types with a header signature
ADD_TYPE_RW(DSK)
ADD_TYPE_RO(TD0)
ADD_TYPE_RW(SAD)
ADD_TYPE_RO(SCL)
ADD_TYPE_RW(FDI)
ADD_TYPE_RW(DTI)
ADD_TYPE_RO(IPF)
ADD_TYPE_RO(MSA)
ADD_TYPE_RO(CWTOOL)
/*
ADD_TYPE_RO(UDI)
*/
ADD_TYPE_RW(IMD)
ADD_TYPE_RO(SBT)
ADD_TYPE_RO(DFI)
ADD_TYPE_RO(SCP)
ADD_TYPE_RO(STREAM)
ADD_TYPE_RO(HFE)

// Types with distinctive fields
ADD_TYPE_RO(D80)
ADD_TYPE_RO(BPB)
ADD_TYPE_RW(ADF)
/*
ADD_TYPE_RO(DMK)
*/
ADD_TYPE_RW(MBD)
ADD_TYPE_RW(OPD)
ADD_TYPE_RW(D88)
ADD_TYPE_RW(1DD)

// Raw types, identified by size or extension only
ADD_TYPE_RW(2D)
ADD_TYPE_RW(TRD)
ADD_TYPE_RW(LIF)
ADD_TYPE_RO(CFI)
ADD_TYPE_RO(DSC)
ADD_TYPE_RO(SDF)
ADD_TYPE_RO(S24)
ADD_TYPE_RW(D2M)
ADD_TYPE_RW(D4M)
ADD_TYPE_RW(D81)
ADD_TYPE_RW(MGT)
ADD_TYPE_RO(DS2)
ADD_TYPE_RW(CPM)
ADD_TYPE_RW(RAW)
ADD_HIDDEN_RO(Unsupp)

#ifndef DECLARATIONS_ONLY
{
 nullptr, nullptr, nullptr
}	// list terminator
};
#endif

#undef ADD_TYPE_RW
#undef ADD_TYPE_RO
#undef ADD_TYPE_WO
#undef ADD_HIDDEN_RO
