#include "lob/sim/market_data_log.hpp"

namespace lob::sim {

namespace {

// True iff `sample`'s key sorts strictly before (timestamp, event_ordinal).
bool Precedes(const MidPriceSample& sample, Timestamp timestamp, std::uint64_t event_ordinal) {
  if (sample.timestamp != timestamp) {
    return sample.timestamp < timestamp;
  }
  return sample.event_ordinal < event_ordinal;
}

// True iff (timestamp, event_ordinal) sorts strictly before `sample`'s key.
bool KeyPrecedes(Timestamp timestamp, std::uint64_t event_ordinal, const MidPriceSample& sample) {
  if (timestamp != sample.timestamp) {
    return timestamp < sample.timestamp;
  }
  return event_ordinal < sample.event_ordinal;
}

}  // namespace

void MarketDataLog::Record(Timestamp timestamp, std::uint64_t event_ordinal, bool has_bid,
                           Price best_bid, bool has_ask, Price best_ask) {
  MidPriceSample sample;
  sample.timestamp = timestamp;
  sample.event_ordinal = event_ordinal;
  sample.valid = has_bid && has_ask;
  sample.mid =
      sample.valid ? (static_cast<double>(best_bid) + static_cast<double>(best_ask)) / 2.0 : 0.0;
  samples_.push_back(sample);
}

std::optional<double> MarketDataLog::MidStrictlyBefore(Timestamp timestamp,
                                                       std::uint64_t event_ordinal) const {
  // Manual binary search (not std::upper_bound/lower_bound with a custom
  // comparator) to avoid any ambiguity in comparator argument order --
  // correctness here matters more than brevity.
  std::size_t lo = 0;
  std::size_t hi = samples_.size();
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo) / 2;
    if (Precedes(samples_[mid], timestamp, event_ordinal)) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  // lo is the first index that does NOT precede the key; lo - 1 is the
  // last sample that does.
  if (lo == 0) {
    return std::nullopt;
  }
  const MidPriceSample& candidate = samples_[lo - 1];
  return candidate.valid ? std::optional<double>(candidate.mid) : std::nullopt;
}

std::optional<double> MarketDataLog::MidAsOf(Timestamp timestamp,
                                             std::uint64_t event_ordinal) const {
  std::size_t lo = 0;
  std::size_t hi = samples_.size();
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo) / 2;
    if (KeyPrecedes(timestamp, event_ordinal, samples_[mid])) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  // lo is the first index whose key is strictly after the query key;
  // lo - 1 is the last sample at or before it.
  if (lo == 0) {
    return std::nullopt;
  }
  const MidPriceSample& candidate = samples_[lo - 1];
  return candidate.valid ? std::optional<double>(candidate.mid) : std::nullopt;
}

}  // namespace lob::sim
