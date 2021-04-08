/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2020  whitequark <whitequark@whitequark.org>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef CXXRTL_VCD_H
#define CXXRTL_VCD_H

#include <backends/cxxrtl/cxxrtl.h>

// TODO debugging only
#include <iostream>

struct bit {
	cxxrtl::chunk_t* ptr;
	size_t offset;
};

typedef std::function<void(uint64_t ts, std::map<std::string, struct bit> bits)> sample_t;

// this extremely cursed construction is so that we can set the function to something else via ldsym
extern "C" {
	sample_t cxxrtl_stream_sample = [](auto ts, auto bits) {
		for (auto &it : bits) {
			bool bit = (*it.second.ptr >> it.second.offset) & 1;
			std::cout << "default " << it.first << ": " << bit << std::endl;
		}
	};
} 

namespace cxxrtl {

class stream_writer {

	std::map<std::string, struct bit> bits;

public:

	void timescale(unsigned number, const std::string &unit) {
	}

	void add(const std::string &hier_name, const debug_item &item, bool multipart = false) {
		if (item.width == 1) {
			bits[hier_name] = {item.curr, 0};
		} else {
			for (size_t i = 0; i < item.width; i++) {
				struct bit currbit;
				currbit.ptr = &item.curr[i/(sizeof(chunk_t)*8)];
				currbit.offset = i % (sizeof(chunk_t)*8);
				std::string name = hier_name + "[" + std::to_string(i) + "]";
				bits[name] = currbit;
			}
		}
	}

	template<class Filter>
	void add(const debug_items &items, const Filter &filter) {
		// `debug_items` is a map, so the items are already sorted in an order optimal for emitting
		// VCD scope sections.
		for (auto &it : items.table)
			for (auto &part : it.second)
				if (filter(it.first, part))
					add(it.first, part, it.second.size() > 1);
	}

	void add(const debug_items &items) {
		this->add(items, [](const std::string &, const debug_item &) {
			return true;
		});
	}

	void add_without_memories(const debug_items &items) {
		this->add(items, [](const std::string &, const debug_item &item) {
			return item.type != debug_item::MEMORY;
		});
	}

	void sample(uint64_t timestamp) {
		if (cxxrtl_stream_sample) {
			cxxrtl_stream_sample(timestamp, bits);
		}
	}
};

}

#endif
