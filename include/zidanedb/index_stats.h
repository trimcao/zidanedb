#ifndef ZIDANEDB_INDEX_STATS_H
#define ZIDANEDB_INDEX_STATS_H

#include <cstdint>

namespace zidanedb {

struct IndexStats {
    std::uint64_t num_buckets;
    std::uint64_t empty_buckets;
    std::uint64_t non_empty_buckets;
    // Average chain length among occupied buckets; zero when the index is empty.
    double avg_chain_length;
    std::uint64_t max_chain_length;
};

} // namespace zidanedb

#endif // ZIDANEDB_INDEX_STATS_H
