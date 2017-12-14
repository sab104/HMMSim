/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef TYPES_H_
#define TYPES_H_

#include <vector>

#include <cstdint>

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint64 addrint;

enum PageType{
	DRAM,
	PCM,
	INVALID
};

struct CountEntry {
	int pid;
	addrint page;
	uint64 reads;
	uint64 writes;
//	std::vector<uint64> readBlocks;
//	std::vector<uint64> writtenBlocks;
	uint64 lastReadTime;

	CountEntry(addrint pageArg) : pid(0), page(pageArg), reads(0), writes(0), lastReadTime(0){}
//	CountEntry(addrint pageArg, unsigned blocksPerPage) : pid(0), page(pageArg), reads(0), writes(0), readBlocks(blocksPerPage), writtenBlocks(blocksPerPage), lastReadTime(0){}
//	CountEntry(const CountEntry& entry) = default;
//	CountEntry(CountEntry&& entry) = default;
//	CountEntry& operator=(const CountEntry& rhs) = default;
//	CountEntry& operator=(CountEntry&&) = default;
//	CountEntry(addrint pageArg, const CountEntry &entry) : pid(entry.pid), page(pageArg), reads(entry.reads), writes(entry.writes), readBlocks(entry.readBlocks), writtenBlocks(entry.writtenBlocks) {}
//	CountEntry(const CountEntry &entry) : pid(entry.pid), page(entry.page), reads(entry.reads), writes(entry.writes), readBlocks(entry.readBlocks), writtenBlocks(entry.writtenBlocks) {}
	bool operator<(const CountEntry& rhs) const {
		return lastReadTime < rhs.lastReadTime;
	}
};

struct ProgressEntry {
	int pid;
	addrint page;
	uint32 blocksRead;
	ProgressEntry(addrint pageArg, uint32 blocksLeftArg) : page(pageArg), blocksRead(blocksLeftArg) {}
};

#endif /* TYPES_H_ */
