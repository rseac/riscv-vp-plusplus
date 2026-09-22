#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

// CVA6 L1 D-cache tag model (timing only, no data). Ara's CVA6 uses the
// write-through cache (cv64a6_imafdcv_sv39): 8 KiB, 4-way, 32 B lines.
// Loads allocate on miss; stores update on hit only (no write-allocate,
// absorbed by the write buffer); Ara vector stores invalidate matching lines
// (axi_inval_filter), vector loads bypass the cache entirely.
class ScalarDCache {
   public:
	bool enabled = false;
	uint32_t miss_penalty_cycles = 0;

	void configure(uint32_t size_bytes, uint32_t ways, uint32_t line_bytes) {
		ways_ = ways;
		line_shift_ = 0;
		while ((1u << line_shift_) < line_bytes) line_shift_++;
		num_sets_ = size_bytes / (ways * line_bytes);
		tags_.assign((size_t)num_sets_ * ways_, 0);
		valid_.assign((size_t)num_sets_ * ways_, 0);
		lru_.assign((size_t)num_sets_ * ways_, 0);
	}

	// Returns true on a miss (line is then allocated).
	bool load(uint64_t addr) {
		uint64_t line = addr >> line_shift_;
		uint32_t set = (uint32_t)(line % num_sets_);
		size_t base = (size_t)set * ways_;
		uint32_t victim = 0;
		uint64_t oldest = UINT64_MAX;
		for (uint32_t w = 0; w < ways_; w++) {
			if (valid_[base + w] && tags_[base + w] == line) {
				lru_[base + w] = ++clock_;
				return false;
			}
			uint64_t age = valid_[base + w] ? lru_[base + w] : 0;
			if (age < oldest) {
				oldest = age;
				victim = w;
			}
		}
		if (!seen_.count(line)) { cold_misses++; seen_.insert(line); }
		else if (inval_.erase(line)) inval_misses++;
		else other_misses++;
		valid_[base + victim] = 1;
		tags_[base + victim] = line;
		lru_[base + victim] = ++clock_;
		return true;
	}

	void invalidate(uint64_t addr, uint32_t bytes) {
		uint64_t first = addr >> line_shift_;
		uint64_t last = (addr + (bytes ? bytes - 1 : 0)) >> line_shift_;
		for (uint64_t line = first; line <= last; line++) {
			uint32_t set = (uint32_t)(line % num_sets_);
			size_t base = (size_t)set * ways_;
			for (uint32_t w = 0; w < ways_; w++) {
				if (valid_[base + w] && tags_[base + w] == line) { valid_[base + w] = 0; inval_.insert(line); }
			}
		}
	}

	void flush() { std::fill(valid_.begin(), valid_.end(), 0); }

	uint64_t misses = 0, cold_misses = 0, inval_misses = 0, other_misses = 0;
	uint64_t accesses = 0;

   private:
	uint32_t ways_ = 4;
	uint32_t line_shift_ = 5;
	uint32_t num_sets_ = 64;
	uint64_t clock_ = 0;
	std::vector<uint64_t> tags_;
	std::vector<uint8_t> valid_;
	std::vector<uint64_t> lru_;
	std::unordered_set<uint64_t> seen_, inval_;
};
