// Copyright 2010-2013 RethinkDB, all rights reserved.
#ifndef SERIALIZER_SERIALIZER_HPP_
#define SERIALIZER_SERIALIZER_HPP_

#include <vector>

#include "utils.hpp"
#include <boost/optional.hpp>

#include "arch/types.hpp"
#include "concurrency/cond_var.hpp"
#include "containers/segmented_vector.hpp"
#include "repli_timestamp.hpp"
#include "serializer/types.hpp"

struct index_write_op_t {
    block_id_t block_id;
    // Buf to write. None if not to be modified. Initialized but a null ptr if to be removed from lba.
    boost::optional<counted_t<standard_block_token_t> > token;
    // RSI: Shouldn't recency always be modified?
    boost::optional<repli_timestamp_t> recency; // Recency, if it should be modified.

    explicit index_write_op_t(block_id_t _block_id,
                              boost::optional<counted_t<standard_block_token_t> > _token = boost::none,
                              boost::optional<repli_timestamp_t> _recency = boost::none)
        : block_id(_block_id), token(_token), recency(_recency) { }
};

void debug_print(printf_buffer_t *buf, const index_write_op_t &write_op);

/* serializer_t is an abstract interface that describes how each serializer should
behave. It is implemented by log_serializer_t, semantic_checking_serializer_t, and
translator_serializer_t. */

/* Except as otherwise noted, the serializer's methods should only be
   called from the thread it was created on, and it should be
   destroyed on that same thread. */

class serializer_t : public home_thread_mixin_t {
public:
    typedef standard_block_token_t block_token_type;

    serializer_t() { }
    virtual ~serializer_t() { }

    /* The buffers that are used with do_read() and do_write() must be allocated using
    these functions. They can be safely called from any thread. */

    virtual scoped_malloc_t<ser_buffer_t> malloc() = 0;
    // RSI: Does the new cache use clone?
    virtual scoped_malloc_t<ser_buffer_t> clone(const ser_buffer_t *) = 0;

    /* Allocates a new io account for the underlying file.
    Use delete to free it. */
    file_account_t *make_io_account(int priority);
    virtual file_account_t *make_io_account(int priority, int outstanding_requests_limit) = 0;

    /* Some serializer implementations support read-ahead to speed up cache warmup.
    This is supported through a serializer_read_ahead_callback_t which gets called whenever the serializer has read-ahead some buf.
    The callee can then decide whether it wants to use the offered buffer of discard it.
    */
    virtual void register_read_ahead_cb(serializer_read_ahead_callback_t *cb) = 0;
    virtual void unregister_read_ahead_cb(serializer_read_ahead_callback_t *cb) = 0;

    // Reading a block from the serializer.  Reads a block, blocks the coroutine.
    virtual void block_read(const counted_t<standard_block_token_t> &token,
                            ser_buffer_t *buf, file_account_t *io_account) = 0;

    /* The index stores three pieces of information for each ID:
     * 1. A pointer to a data block on disk (which may be NULL)
     * 2. A repli_timestamp_t, called the "recency"
     * 3. A boolean, called the "delete bit" */

    /* max_block_id() and get_delete_bit() are used by the buffer cache to reconstruct
    the free list of unused block IDs. */

    /* Returns a block ID such that every existing block has an ID less than
     * that ID. Note that index_read(max_block_id() - 1) is not guaranteed to be
     * non-NULL. Note that for k > 0, max_block_id() - k might have never been
     * created. */
    virtual block_id_t max_block_id() = 0;

    // RSI: Is this obsolete?
    /* Gets a block's timestamp.  This may return repli_timestamp_t::invalid. */
    /* You must never call this after _writing_ a block. */
    virtual repli_timestamp_t get_recency(block_id_t id) = 0;

    /* Returns all recencies, for all block ids of the form first + step * k, for k =
       0, 1, 2, 3, ..., in order by block id.  Non-existant block ids have recency
       repli_timestamp_t::invalid.  You must never call this after _writing_ a block. */
    virtual segmented_vector_t<repli_timestamp_t>
    get_all_recencies(block_id_t first, block_id_t step) = 0;

    /* Returns all recencies, indexed by block id. */
    segmented_vector_t<repli_timestamp_t> get_all_recencies() {
        return get_all_recencies(0, 1);
    }

    /* Reads the block's delete bit. */
    // RSI: Does this actually get used by the new cache?
    virtual bool get_delete_bit(block_id_t id) = 0;

    /* Reads the block's actual data */
    virtual counted_t<standard_block_token_t> index_read(block_id_t block_id) = 0;

    /* index_write() applies all given index operations in an atomic way */
    virtual void index_write(const std::vector<index_write_op_t>& write_ops, file_account_t *io_account) = 0;

    // Returns block tokens in the same order as write_infos.
    virtual std::vector<counted_t<standard_block_token_t> >
    block_writes(const std::vector<buf_write_info_t> &write_infos,
                 file_account_t *io_account,
                 iocallback_t *cb) = 0;

    /* The size, in bytes, of each serializer block */
    // RSI: Rename to max_block_size or default_block_size.
    virtual block_size_t get_block_size() const = 0;

    /* Return true if no other processes have the file locked */
    virtual bool coop_lock_and_check() = 0;

private:
    DISABLE_COPYING(serializer_t);
};


// The do_write interface is now obvious helper functions

struct serializer_write_launched_callback_t {
    virtual void on_write_launched(const counted_t<standard_block_token_t>& token) = 0;
    virtual ~serializer_write_launched_callback_t() {}
};
struct serializer_write_t {
    block_id_t block_id;

    enum { UPDATE, DELETE, TOUCH } action_type;
    union {
        struct {
            const void *buf;
            uint32_t ser_block_size;
            repli_timestamp_t recency;
            iocallback_t *io_callback;
            serializer_write_launched_callback_t *launch_callback;
        } update;
        struct {
            repli_timestamp_t recency;
        } touch;
    } action;

    static serializer_write_t make_touch(block_id_t block_id, repli_timestamp_t recency);
    static serializer_write_t make_update(block_id_t block_id, block_size_t block_size,
                                          repli_timestamp_t recency, const void *buf,
                                          iocallback_t *io_callback,
                                          serializer_write_launched_callback_t *launch_callback);
    static serializer_write_t make_delete(block_id_t block_id);
};

/* A bad wrapper for doing block writes and index writes.
 */
void do_writes(serializer_t *ser, const std::vector<serializer_write_t>& writes, file_account_t *io_account);


// Helpers for default implementations that can be used on log_serializer_t.

template <class serializer_type>
void serializer_index_write(serializer_type *ser, const index_write_op_t& op, file_account_t *io_account) {
    std::vector<index_write_op_t> ops;
    ops.push_back(op);
    return ser->index_write(ops, io_account);
}

counted_t<standard_block_token_t> serializer_block_write(serializer_t *ser, ser_buffer_t *buf,
                                                         block_size_t block_size,
                                                         block_id_t block_id, file_account_t *io_account);

#endif /* SERIALIZER_SERIALIZER_HPP_ */
