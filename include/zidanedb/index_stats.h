#ifndef ZIDANEDB_INDEX_STATS_H
#define ZIDANEDB_INDEX_STATS_H

#include <cstdint>

namespace zidanedb {

struct IndexStats {
    // - Number of empty buckets
    // - Number of non-empty buckets
    // - Average chain length
    // - Maximum chain length
    // - load factor (keys / buckets)
    std::uint64_t num_buckets;
    std::uint64_t empty_buckets;
    std::uint64_t non_empty_buckets;
    double avg_chain_length;
    std::uint64_t max_chain_length;
};

} // namespace zidanedb

#endif // ZIDANEDB_INDEX_STATS_H
