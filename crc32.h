#pragma once

#include <cstdint>
#include <cstddef>

static constexpr uint32_t kCrc32Poly = 0xedb88320u;
static constexpr uint32_t kCrc32TableXor = 0xd202ef8du;
static constexpr uint32_t kCrc32Seed = 0xeada2d49u;

struct Crc32Table
{
	uint32_t entries[256];
};

static constexpr Crc32Table makeCrc32Table()
{
	Crc32Table table{};
	for (uint32_t i = 0; i < 256; ++i)
	{
		uint32_t c = i;
		for (unsigned bit = 0; bit < 8; ++bit)
			c = (c & 1u) ? (c >> 1) ^ kCrc32Poly : (c >> 1);
		table.entries[i] = c ^ kCrc32TableXor;
	}
	return table;
}

static constexpr Crc32Table crc32HashTable = makeCrc32Table();

static inline uint32_t computeCRC32(const uint8_t *buffer, const size_t &len)
{
	uint32_t result = kCrc32Seed;
	for (size_t i = 0; i < len; ++i)
		result = crc32HashTable.entries[((uint8_t)result) ^ ((uint8_t)buffer[i])] ^ (result >> 8);
	return result;
}
