#define Range_Search 1
#define UPSERT 1 // indicates that supports upsert

#include "ordered_map.h"

namespace parlay {
template <typename K,
          typename V,
          typename Compare = std::less<K>>
using ordered_map = verlib::btree<K,V,Compare>;
}
