// Copyright 2010-2012 RethinkDB, all rights reserved.
#include "buffer_cache/mirrored/page_repl_random.hpp"

#include "buffer_cache/mirrored/mirrored.hpp"
#include "logger.hpp"
#include "perfmon/perfmon.hpp"

evictable_t::evictable_t(mc_cache_t *_cache, bool loaded)
    : eviction_priority(DEFAULT_EVICTION_PRIORITY), cache(_cache), page_repl_index(static_cast<size_t>(-1))
{
    cache->assert_thread();
    if (loaded) {
        insert_into_page_repl();
    }
}

evictable_t::~evictable_t() {
    cache->assert_thread();

    // It's the subclass destructor's responsibility to run
    //
    //     if (in_page_repl()) { remove_from_page_repl(); }
    rassert(!in_page_repl());
}

bool evictable_t::in_page_repl() {
    return page_repl_index != static_cast<size_t>(-1);
}

void evictable_t::insert_into_page_repl() {
    cache->assert_thread();
    page_repl_index = cache->page_repl.array.size();
    cache->page_repl.array.push_back(this);
}

void evictable_t::remove_from_page_repl() {
    cache->assert_thread();

    rassert(page_repl_index < cache->page_repl.array.size());
    evictable_t *replacement = cache->page_repl.array.back();
    replacement->page_repl_index = page_repl_index;
    std::swap(cache->page_repl.array[page_repl_index],
              cache->page_repl.array.back());
    cache->page_repl.array.pop_back();
    page_repl_index = static_cast<size_t>(-1);
}

page_repl_random_t::page_repl_random_t(size_t _unload_threshold, cache_t *_cache)
    : unload_threshold(_unload_threshold),
      cache(_cache)
    {}

bool page_repl_random_t::is_full(size_t space_needed) {
    cache->assert_thread();
    return array.size() + space_needed > unload_threshold;
}

//perfmon_counter_t pm_n_blocks_evicted("blocks_evicted");

size_t randsize(size_t n) {
    size_t x = randint(0x10000);
    x = x * 0x10000 + randint(0x10000);
    x = x * 0x10000 + randint(0x10000);
    x = x * 0x10000 + randint(0x10000);
    return x % n;
}


// make_space tries to make sure that the number of blocks currently in memory is at least
// 'space_needed' less than the user-specified memory limit.
void page_repl_random_t::make_space(size_t space_needed) {
    cache->assert_thread();
    // `target` is how many free blocks we want to have when we return.
    size_t target;
    if (space_needed > unload_threshold) {
        // We cannot accomplish our goal of having at least `space_needed` less
        // blocks in memory than the memory limit (`unload_threshold`), because
        // `space_needed` is too large.
        // However we try to get as close as possible by unloading as many blocks
        // as we can.
        target = 0;
    } else {
        target = unload_threshold - space_needed;
    }

    while (array.size() > target) {
        // Try to find a block we can unload. Blocks are ineligible to be unloaded if they are
        // dirty or in use.
        evictable_t *block_to_unload = NULL;
        for (int tries = PAGE_REPL_NUM_TRIES; tries > 0; tries --) {
            /* Choose a block in memory at random. */
            size_t n = randsize(array.size());
            evictable_t *block = array[n];

            // TODO we don't have code that sets buf_snapshot_t eviction priorities.

            if (!block->safe_to_unload()) {
                /* nothing to do here, jetpack away to the next iteration of this loop */
            } else if (block_to_unload == NULL) {
                /* The block is safe to unload, and our only candidate so far, so he's in */
                block_to_unload = block;
            } else if (block_to_unload->eviction_priority < block->eviction_priority) {
                /* This block is a better candidate than one before, he's in */
                block_to_unload = block;
            } else {
                /* Failed to find a better candidate, continue on our way. */
            }
        }

        if (!block_to_unload) {
            // The following log message blows the corostack because it has propensity to overlog.
            // Commenting it out for 1.2. TODO: we might want to address it later in a different
            // way (i.e. spawn_maybe?)
            /*
            if (array.size() > target + (target / 100) + 10)
                logWRN("cache %p exceeding memory target. %d blocks in memory, %d dirty, target is %d.",
                       cache, array.size(), cache->writeback.num_dirty_blocks(), target);
            */
            break;
        }

        // Remove it from the page repl and call its callback. Need to remove it from the repl first
        // because its callback could delete it.
        block_to_unload->remove_from_page_repl();
        block_to_unload->unload();
        ++cache->stats->pm_n_blocks_evicted;
    }
}

evictable_t *page_repl_random_t::get_first_buf() {
    cache->assert_thread();
    return array.empty() ? NULL : array[0];
}
