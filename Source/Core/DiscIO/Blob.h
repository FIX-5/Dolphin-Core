// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

// BLOB

// Blobs in Dolphin are read only Binary Large OBjects. For example, a typical DVD image.
// Often, you may want to store these things in a highly compressed format, but still
// allow random access. Or you may store them on an odd device, like raw on a DVD.

// Always read your BLOBs using an interface returned by CreateBlobReader(). It will
// detect whether the file is a compressed blob, or just a big hunk of data, or a drive, and
// automatically do the right thing.

#include <array>
#include <memory>
#include <string>
#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"

namespace DiscIO
{

// Increment CACHE_REVISION if the enum below is modified (ISOFile.cpp & GameFile.cpp)
enum class BlobType
{
	PLAIN,
	DRIVE,
	DIRECTORY,
	GCZ,
	CISO,
	WBFS
};

class IBlobReader
{
public:
	virtual ~IBlobReader() {}

	virtual BlobType GetBlobType() const = 0;
	virtual u64 GetRawSize() const = 0;
	virtual u64 GetDataSize() const = 0;
	// NOT thread-safe - can't call this from multiple threads.
	virtual bool Read(u64 offset, u64 size, u8* out_ptr) = 0;

protected:
	IBlobReader() {}
};


// Provides caching and split-operation-to-block-operations facilities.
// Used for compressed blob reading and direct drive reading.
// Currently only uses a single entry cache.
// Multi-block reads are not cached.
class SectorReader : public IBlobReader
{
public:
	virtual ~SectorReader();

	bool Read(u64 offset, u64 size, u8 *out_ptr) override;
	friend class DriveReader;

protected:
	void SetSectorSize(int blocksize);
	virtual void GetBlock(u64 block_num, u8 *out) = 0;
	// This one is uncached. The default implementation is to simply call GetBlockData multiple times and memcpy.
	virtual bool ReadMultipleAlignedBlocks(u64 block_num, u64 num_blocks, u8 *out_ptr);

private:
	// A reference returned by GetBlockData is invalidated as soon as GetBlockData, Read, or ReadMultipleAlignedBlocks is called again.
	const std::vector<u8>& GetBlockData(u64 block_num);

	enum { CACHE_SIZE = 32 };
	int m_blocksize;
	std::array<std::vector<u8>, CACHE_SIZE> m_cache;
	std::array<u64, CACHE_SIZE> m_cache_tags;
};

class CBlobBigEndianReader
{
public:
	CBlobBigEndianReader(IBlobReader& reader) : m_reader(reader) {}

	template <typename T>
	bool ReadSwapped(u64 offset, T* buffer) const
	{
		T temp;
		if (!m_reader.Read(offset, sizeof(T), reinterpret_cast<u8*>(&temp)))
			return false;
		*buffer = Common::FromBigEndian(temp);
		return true;
	}

private:
	IBlobReader& m_reader;
};

// Factory function - examines the path to choose the right type of IBlobReader, and returns one.
std::unique_ptr<IBlobReader> CreateBlobReader(const std::string& filename);

typedef bool (*CompressCB)(const std::string& text, float percent, void* arg);

bool CompressFileToBlob(const std::string& infile, const std::string& outfile, u32 sub_type = 0, int sector_size = 16384,
		CompressCB callback = nullptr, void *arg = nullptr);
bool DecompressBlobToFile(const std::string& infile, const std::string& outfile,
		CompressCB callback = nullptr, void *arg = nullptr);

}  // namespace
