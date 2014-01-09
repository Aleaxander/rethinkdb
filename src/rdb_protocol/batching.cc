// Copyright 2010-2013 RethinkDB, all rights reserved.

#include "rdb_protocol/batching.hpp"

#include "rdb_protocol/datum.hpp"
#include "rdb_protocol/env.hpp"
#include "rdb_protocol/error.hpp"

namespace ql {

batchspec_t::batchspec_t(
    batch_type_t _batch_type,
    int64_t els,
    int64_t size,
    microtime_t _end_time)
    : batch_type(_batch_type),
      els_left(els),
      size_left(size),
      end_time(_batch_type == batch_type_t::NORMAL
               ? _end_time
               : std::numeric_limits<decltype(batchspec_t().end_time)>::max()) {
    r_sanity_check(els_left >= 1);
}

batchspec_t batchspec_t::user(batch_type_t batch_type,
                              const counted_t<const datum_t> &conf) {
    counted_t<const datum_t> max_els_d, max_size_d, max_dur_d;
    if (conf.has()) {
        max_els_d = conf->get("max_els", NOTHROW);
        max_size_d = conf->get("max_size", NOTHROW);
        max_dur_d = conf->get("max_dur", NOTHROW);
    }
    return batchspec_t(
        batch_type,
        max_els_d.has()
            ? max_els_d->as_int()
            : std::numeric_limits<decltype(batchspec_t().els_left)>::max(),
        max_size_d.has() ? max_size_d->as_int() : MEGABYTE / 4,
        (batch_type == batch_type_t::NORMAL)
            ? current_microtime() + (max_dur_d.has() ? max_dur_d->as_int() : 500 * 1000)
            : std::numeric_limits<decltype(batchspec_t().end_time)>::max());
}

batchspec_t batchspec_t::user(batch_type_t batch_type, env_t *env) {
    counted_t<val_t> vconf = env->global_optargs.get_optarg(env, "batch_conf");
    return user(
        batch_type,
        vconf.has() ? vconf->as_datum() : counted_t<const datum_t>());
}

batchspec_t batchspec_t::with_new_batch_type(batch_type_t new_batch_type) const {
    return batchspec_t(new_batch_type, els_left, size_left, end_time);
}

batchspec_t batchspec_t::with_at_most(uint64_t _max_els) const {
    int64_t max_els = std::min(uint64_t(std::numeric_limits<int64_t>::max()), _max_els);
    return batchspec_t(
        batch_type,
        std::max<int64_t>(1, std::min(els_left, max_els)),
        size_left,
        end_time);
}

batchspec_t batchspec_t::scale_down(int64_t divisor) const {
    // These numbers are sort of arbitrary, but they seem to work.  We divide by
    // 7/8th of the divisor and add 8 to reduce the chances of needing a second
    // round-trip (we add a constant because unequal division is more likely
    // with very small sizes).  Law of large numbers says that the chances of
    // needing a second round-trip for large, non-pathological datasets are
    // extremely low.
    int64_t new_els_left = (els_left * 8 / (7 * divisor)) + 8;
    int64_t new_size_left = (size_left * 8 / (7 * divisor)) + 8;
    return batchspec_t(batch_type,
                       std::min(els_left, new_els_left),
                       std::min(size_left, new_size_left),
                       end_time);
}

batcher_t batchspec_t::to_batcher() const {
    microtime_t real_end_time =
        batch_type == batch_type_t::NORMAL && end_time > current_microtime()
            ? end_time
            : std::numeric_limits<decltype(batchspec_t().end_time)>::max();
    return batcher_t(batch_type, els_left, size_left, real_end_time);
}

bool batcher_t::should_send_batch() const {
    return els_left <= 0
        || size_left <= 0
        || (current_microtime() >= end_time && seen_one_el);
}

batcher_t::batcher_t(
    batch_type_t _batch_type,
    int64_t els,
    int64_t size,
    microtime_t _end_time)
    : batch_type(_batch_type),
      seen_one_el(false),
      els_left(els),
      size_left(size),
      end_time(_end_time) { }

size_t array_size_limit() { return 100000; }

} // namespace ql
