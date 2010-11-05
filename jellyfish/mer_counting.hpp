#ifndef __JELLYFISH_MER_COUNTING__
#define __JELLYFISH_MER_COUNTING__

#include <sys/types.h>
#include <stdint.h>
#include <stdarg.h>
#include <vector>
#include <utility>

#include <jellyfish/misc.hpp>
#include <jellyfish/storage.hpp>
#include <jellyfish/hash.hpp>
#include <jellyfish/concurrent_queues.hpp>
#include <jellyfish/atomic_gcc.hpp>
#include <jellyfish/allocators_mmap.hpp>
#include <jellyfish/hash.hpp>
#include <jellyfish/compacted_hash.hpp>
#include <jellyfish/compacted_dumper.hpp>

// Invertible hash types
#include <jellyfish/invertible_hash_array.hpp>
#include <jellyfish/sorted_dumper.hpp>
typedef jellyfish::invertible_hash::array<uint64_t,atomic::gcc<uint64_t>,allocators::mmap> inv_hash_storage_t;
typedef jellyfish::sorted_dumper< inv_hash_storage_t,atomic::gcc<uint64_t> > inv_hash_dumper_t;
typedef jellyfish::hash< uint64_t,uint64_t,inv_hash_storage_t,atomic::gcc<uint64_t> > inv_hash_t;

// Direct indexing types
#include <jellyfish/direct_indexing_array.hpp>
#include <jellyfish/direct_sorted_dumper.hpp>
typedef jellyfish::direct_indexing::array<uint64_t,uint32_t,atomic::gcc<uint32_t>,allocators::mmap> direct_index_storage_t;
typedef jellyfish::direct_sorted_dumper< direct_index_storage_t, atomic::gcc<uint64_t> > direct_index_dumper_t;
typedef jellyfish::hash< uint64_t,uint32_t,direct_index_storage_t,atomic::gcc<uint64_t> > direct_index_t;

typedef jellyfish::compacted_hash::reader<uint64_t,uint64_t> hash_reader_t;
typedef jellyfish::compacted_hash::query<uint64_t,uint64_t> hash_query_t;
typedef jellyfish::compacted_hash::writer<hash_reader_t> hash_writer_t;

#endif /* __MER_COUNTING__ */