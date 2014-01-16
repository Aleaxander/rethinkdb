#define __STDC_LIMIT_MACROS

#include "arch/runtime/coroutines.hpp"
#include "arch/timing.hpp"
#include "buffer_cache/alt/page.hpp"
#include "buffer_cache/alt/alt.hpp"  // RSI: for alt_memory_tracker_t.  We'll want to have some mock one here probably.
#include "concurrency/auto_drainer.hpp"
#include "serializer/config.hpp"
#include "unittest/gtest.hpp"
#include "unittest/mock_file.hpp"
#include "unittest/unittest_utils.hpp"

namespace unittest {

using alt::alt_access_t;
using alt::alt_create_t;
using alt::current_page_acq_t;
using alt::current_page_t;
using alt::page_acq_t;
using alt::page_cache_t;
using alt::page_t;
using alt::page_txn_t;

struct mock_ser_t {
    mock_file_opener_t opener;
    scoped_ptr_t<standard_serializer_t> ser;
    scoped_ptr_t<alt::alt_memory_tracker_t> tracker;

    mock_ser_t()
        : opener() {
        standard_serializer_t::create(&opener,
                                      standard_serializer_t::static_config_t());
        ser = make_scoped<standard_serializer_t>(log_serializer_t::dynamic_config_t(),
                                                 &opener,
                                                 &get_global_perfmon_collection());
        tracker = make_scoped<alt::alt_memory_tracker_t>();
    }
};

void run_Control() {
    mock_ser_t ser;
}

TEST(PageTest, Control) {
    run_in_thread_pool(run_Control, 4);
}

void run_CreateDestroy() {
    mock_ser_t mock;
    page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
}

TEST(PageTest, CreateDestroy) {
    run_in_thread_pool(run_CreateDestroy, 4);
}

void run_OneTxn() {
    mock_ser_t mock;
    {
        page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
        {
            page_txn_t txn(&page_cache);
        }
    }
}

TEST(PageTest, OneTxn) {
    run_in_thread_pool(run_OneTxn, 4);
}

void run_TwoIndependentTxn() {
    mock_ser_t mock;
    page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
    page_txn_t txn1(&page_cache);
    page_txn_t txn2(&page_cache);
}

TEST(PageTest, TwoIndependentTxn) {
    run_in_thread_pool(run_TwoIndependentTxn, 4);
}

void run_TwoIndependentTxnSwitch() {
    mock_ser_t mock;
    page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
    auto txn1 = make_scoped<page_txn_t>(&page_cache);
    page_txn_t txn2(&page_cache);
    txn1.reset();
}

TEST(PageTest, TwoIndependentTxnSwitch) {
    run_in_thread_pool(run_TwoIndependentTxnSwitch, 4);
}

void run_TwoSequentialTxnSwitch() {
    mock_ser_t mock;
    page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
    auto txn1 = make_scoped<page_txn_t>(&page_cache);
    page_txn_t txn2(&page_cache, txn1.get());
    txn1.reset();
}

TEST(PageTest, TwoSequentialTxnSwitch) {
    run_in_thread_pool(run_TwoSequentialTxnSwitch, 4);
}

void run_OneReadAcq() {
    mock_ser_t mock;
    page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
    page_txn_t txn(&page_cache);
    current_page_acq_t acq(&txn, 0, alt_access_t::read);
    // Do nothing with the acq.
}

TEST(PageTest, OneReadAcq) {
    run_in_thread_pool(run_OneReadAcq, 4);
}

void run_OneWriteAcq() {
    mock_ser_t mock;
    page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
    page_txn_t txn(&page_cache);
    current_page_acq_t acq(&txn, 0, alt_access_t::write);
    // Do nothing with the acq.
}

TEST(PageTest, OneWriteAcq) {
    run_in_thread_pool(run_OneWriteAcq, 4);
}

void run_OneWriteAcqWait() {
    mock_ser_t mock;
    page_cache_t page_cache(mock.ser.get(), mock.tracker.get());
    page_txn_t txn(&page_cache);
    current_page_acq_t acq(&txn, alt_create_t::create);
    page_acq_t page_acq;
    page_t *page = acq.current_page_for_write();
    page_acq.init(page, &page_cache);
    ASSERT_TRUE(page_acq.buf_ready_signal()->is_pulsed());
    void *buf = page_acq.get_buf_write();
    ASSERT_TRUE(buf != NULL);
}

TEST(PageTest, OneWriteAcqWait) {
    run_in_thread_pool(run_OneWriteAcqWait, 4);
}

class bigger_test_t {
public:
    explicit bigger_test_t(uint64_t _memory_limit)
        : memory_limit(_memory_limit), mock(), c(NULL),
          txn1_ptr(NULL), txn2_ptr(NULL) {
        for (size_t i = 0; i < b_len; ++i) {
            b[i] = NULL_BLOCK_ID;
        }
    }

    void run() {
        {
            page_cache_t cache(mock.ser.get(), mock.tracker.get(), memory_limit);
            auto_drainer_t drain;
            c = &cache;

            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn6,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn7,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn8,
                                            this, drain.lock()));

            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn1,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn2,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn3,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn4,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn5,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn10,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn11,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn12,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn13,
                                            this, drain.lock()));

            condH.wait();
            condB.pulse();
            condE.pulse();
            condF.pulse();
            condG.pulse();
            condK.pulse();

            condZ5.wait();
            t678cond.pulse();
        }
        c = NULL;

        {
            page_cache_t cache(mock.ser.get(), mock.tracker.get(), memory_limit);
            auto_drainer_t drain;
            c = &cache;
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn14,
                                            this, drain.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn15,
                                            this, drain.lock()));
        }
        c = NULL;

        {
            page_cache_t cache(mock.ser.get(), mock.tracker.get(), memory_limit);
            c = &cache;
            page_txn_t txn(c);

            check_value(&txn, b[0], "t6");
            check_value(&txn, b[1], "t6");
            check_value(&txn, b[2], "t6");
            check_value(&txn, b[3], "t7t14t15");
            check_value(&txn, b[4], "t7t15t14it14ii");
            check_value(&txn, b[5], "t8");
            check_value(&txn, b[6], "t1t2t9");
            check_value(&txn, b[7], "t2t5");
            check_value(&txn, b[8], "t12t13");
            check_value(&txn, b[9], "t2");
            check_value(&txn, b[10], "t5");
            check_value(&txn, b[11], "t9");
            check_value(&txn, b[12], "t9");
            check_value(&txn, b[13], "t9");
            check_value(&txn, b[14], "t9");
            check_value(&txn, b[15], "t10");
            check_value(&txn, b[16], "t5t9");
        }
        c = NULL;
    }

private:
    void run_txn1(auto_drainer_t::lock_t) {
        {
            page_txn_t txn1(c);
            txn1_ptr = &txn1;
            auto acq6 = make_scoped<current_page_acq_t>(&txn1,
                                                        alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[6]);
            b[6] = acq6->block_id();

            condA.pulse();

            acq6->read_acq_signal()->wait();
            ASSERT_TRUE(acq6->write_acq_signal()->is_pulsed());

            make_empty(acq6);
            check_and_append(acq6, "", "t1");

            condB.wait();
            acq6.reset();

            condCR1.pulse();
            condC.wait();
            txn1_ptr = NULL;
        }
    }

    void run_txn2(auto_drainer_t::lock_t) {
        {
            condA.wait();
            ASSERT_TRUE(txn1_ptr != NULL);
            page_txn_t txn2(c, txn1_ptr);
            txn2_ptr = &txn2;

            ASSERT_NE(NULL_BLOCK_ID, b[6]);
            auto acq6 = make_scoped<current_page_acq_t>(&txn2, b[6], alt_access_t::write);

            condC.pulse();

            ASSERT_FALSE(acq6->read_acq_signal()->is_pulsed());

            acq6->read_acq_signal()->wait();
            ASSERT_TRUE(acq6->write_acq_signal()->is_pulsed());

            check_and_append(acq6, "t1", "t2");

            condE.wait();

            auto acq7 = make_scoped<current_page_acq_t>(&txn2, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[7]);
            b[7] = acq7->block_id();

            acq6.reset();

            acq7->write_acq_signal()->wait();

            make_empty(acq7);
            check_and_append(acq7, "", "t2");

            condF.wait();

            auto acq8 = make_scoped<current_page_acq_t>(&txn2, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[8]);
            b[8] = acq8->block_id();

            auto acq9 = make_scoped<current_page_acq_t>(&txn2, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[9]);
            b[9] = acq9->block_id();

            acq7.reset();

            make_empty(acq8);
            check_and_append(acq8, "", "t2");
            ASSERT_TRUE(acq8->write_acq_signal()->is_pulsed());

            make_empty(acq9);
            check_and_append(acq9, "", "t2");
            acq9.reset();

            condG.wait();

            acq8.reset();

            condCR2.pulse();

            txn2_ptr = NULL;
        }
    }

    void run_txn3(auto_drainer_t::lock_t) {
        {
            page_txn_t txn3(c);

            condC.wait();
            ASSERT_NE(NULL_BLOCK_ID, b[6]);
            auto acq6 = make_scoped<current_page_acq_t>(&txn3, b[6], alt_access_t::read);

            condD.pulse();

            acq6->read_acq_signal()->wait();
            check_value(acq6, "t1t2");

            condI.wait();

            ASSERT_NE(NULL_BLOCK_ID, b[7]);
            auto acq7 = make_scoped<current_page_acq_t>(&txn3, b[7], alt_access_t::read);
            acq6.reset();

            check_value(acq7, "t2");
            ASSERT_TRUE(acq7->read_acq_signal()->is_pulsed());

            ASSERT_NE(NULL_BLOCK_ID, b[8]);
            auto acq8 = make_scoped<current_page_acq_t>(&txn3, b[8], alt_access_t::read);
            acq7.reset();

            acq8->read_acq_signal()->wait();
            check_value(acq8, "t2");

            condJ.wait();

            acq8->declare_snapshotted();

            check_value(acq8, "t2");

            condL.wait();

            check_value(acq8, "t2");

            condK.wait();

            check_value(acq8, "t2");

            acq8.reset();
        }
    }

    void run_txn4(auto_drainer_t::lock_t) {
        {
            page_txn_t txn4(c);

            condD.wait();
            ASSERT_NE(NULL_BLOCK_ID, b[6]);
            auto acq6 = make_scoped<current_page_acq_t>(&txn4, b[6],
                                                        alt_access_t::write);

            condH.pulse();

            acq6->read_acq_signal()->wait();
            check_value(acq6, "t1t2");

            ASSERT_FALSE(acq6->write_acq_signal()->is_pulsed());
            condI.pulse();

            acq6->write_acq_signal()->wait();
            ASSERT_NE(NULL_BLOCK_ID, b[7]);
            auto acq7 = make_scoped<current_page_acq_t>(&txn4, b[7],
                                                        alt_access_t::write);
            acq6.reset();

            check_value(acq7, "t2");
            acq7->write_acq_signal()->wait();
            ASSERT_NE(NULL_BLOCK_ID, b[8]);
            auto acq8 = make_scoped<current_page_acq_t>(&txn4, b[8],
                                                        alt_access_t::write);
            acq7.reset();

            acq8->read_acq_signal()->wait();
            ASSERT_FALSE(acq8->write_acq_signal()->is_pulsed());
            condJ.pulse();

            check_and_append(acq8, "t2", "t4");
            ASSERT_TRUE(acq8->write_acq_signal()->is_pulsed());
            condL.pulse();

            acq8.reset();
        }
    }

    void run_txn5(auto_drainer_t::lock_t) {
        {
            page_txn_t txn5(c);

            condH.wait();
            ASSERT_NE(NULL_BLOCK_ID, b[6]);
            auto acq6 = make_scoped<current_page_acq_t>(&txn5, b[6],
                                                        alt_access_t::write);

            condM.pulse();
            acq6->write_acq_signal()->wait();
            check_value(acq6, "t1t2");
            ASSERT_NE(NULL_BLOCK_ID, b[7]);
            auto acq7 = make_scoped<current_page_acq_t>(&txn5, b[7],
                                                        alt_access_t::write);
            acq6.reset();
            acq7->write_acq_signal()->wait();
            check_and_append(acq7, "t2", "t5");
            auto acq10 = make_scoped<current_page_acq_t>(&txn5, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[10]);
            b[10] = acq10->block_id();

            acq7.reset();

            acq10->write_acq_signal()->wait();

            make_empty(acq10);
            check_and_append(acq10, "", "t5");

            auto acq16 = make_scoped<current_page_acq_t>(&txn5, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[16]);
            b[16] = acq16->block_id();

            acq10.reset();

            acq16->write_acq_signal()->wait();
            make_empty(acq16);
            check_and_append(acq16, "", "t5");

            condN.wait();

            condCR3.pulse();
        }
    }

    void run_txn6(auto_drainer_t::lock_t) {
        {
            page_txn_t txn6(c);
            auto acq0 = make_scoped<current_page_acq_t>(&txn6, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[0]);
            b[0] = acq0->block_id();
            acq0->write_acq_signal()->wait();
            make_empty(acq0);
            check_and_append(acq0, "", "t6");
            auto acq1 = make_scoped<current_page_acq_t>(&txn6, alt_create_t::create);
            acq0.reset();
            ASSERT_EQ(NULL_BLOCK_ID, b[1]);
            b[1] = acq1->block_id();
            acq1->write_acq_signal()->wait();
            make_empty(acq1);
            check_and_append(acq1, "", "t6");
            auto acq2 = make_scoped<current_page_acq_t>(&txn6, alt_create_t::create);
            acq1.reset();
            ASSERT_EQ(NULL_BLOCK_ID, b[2]);
            b[2] = acq2->block_id();
            ASSERT_NE(NULL_BLOCK_ID, b[0]);
            ASSERT_NE(NULL_BLOCK_ID, b[1]);
            ASSERT_NE(NULL_BLOCK_ID, b[2]);
            assert_unique_ids();
            acq2->write_acq_signal()->wait();
            make_empty(acq2);
            check_and_append(acq2, "", "t6");

            // Wait while holding block.
            t678cond.wait();
            acq2.reset();
        }
    }

    void run_txn7(auto_drainer_t::lock_t) {
        {
            page_txn_t txn7(c);
            auto acq3 = make_scoped<current_page_acq_t>(&txn7, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[3]);
            b[3] = acq3->block_id();
            acq3->write_acq_signal()->wait();
            make_empty(acq3);
            check_and_append(acq3, "", "t7");
            auto acq4 = make_scoped<current_page_acq_t>(&txn7, alt_create_t::create);
            acq3.reset();
            ASSERT_EQ(NULL_BLOCK_ID, b[4]);
            b[4] = acq4->block_id();
            acq4->write_acq_signal()->wait();
            make_empty(acq4);
            check_and_append(acq4, "", "t7");
            acq4.reset();

            // Wait after releasing block, to be different than run_txn6.
            t678cond.wait();
        }
    }

    void run_txn8(auto_drainer_t::lock_t) {
        {
            page_txn_t txn8(c);
            auto acq5 = make_scoped<current_page_acq_t>(&txn8, alt_create_t::create);
            ASSERT_EQ(NULL_BLOCK_ID, b[5]);
            b[5] = acq5->block_id();
            acq5->write_acq_signal()->wait();
            make_empty(acq5);
            check_and_append(acq5, "", "t8");
            acq5.reset();

            // Idk, wait after releasing block.
            t678cond.wait();
        }
    }

    void run_txn9(auto_drainer_t::lock_t) {
        {
            page_txn_t txn9(c);
            auto_drainer_t subdrainer;

            condM.wait();
            auto acq6 = make_scoped<current_page_acq_t>(&txn9, b[6], alt_access_t::write);

            condP.pulse();

            acq6->write_acq_signal()->wait();

            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9A, this, &txn9,
                                    subdrainer.lock()));
            coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9B, this, &txn9,
                                    subdrainer.lock()));

            condQ1.wait();
            condQ2.wait();

            check_and_append(acq6, "t1t2", "t9");
            acq6.reset();
        }
    }

    void run_txn9A(page_txn_t *txn9, auto_drainer_t::lock_t lock) {
        auto acq11 = make_scoped<current_page_acq_t>(txn9, alt_create_t::create);
        ASSERT_EQ(NULL_BLOCK_ID, b[11]);
        b[11] = acq11->block_id();

        condQ1.pulse();

        make_empty(acq11);
        check_and_append(acq11, "", "t9");

        coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9C, this, txn9, lock));
        coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9D, this, txn9, lock));

        condR1.pulse();
        condS1.wait();
        acq11.reset();
    }

    void run_txn9B(page_txn_t *txn9, auto_drainer_t::lock_t) {
        auto acq7 = make_scoped<current_page_acq_t>(txn9, b[7], alt_access_t::write);

        condQ2.pulse();

        acq7->write_acq_signal()->wait();
        check_value(acq7, "t2t5");
        auto acq10 = make_scoped<current_page_acq_t>(txn9, b[10], alt_access_t::write);
        acq7.reset();
        acq10->write_acq_signal()->wait();
        check_value(acq10, "t5");

        condR2.pulse();
        condS2.wait();

        acq10.reset();
    }

    void run_txn9C(page_txn_t *txn9, auto_drainer_t::lock_t lock) {
        auto acq12 = make_scoped<current_page_acq_t>(txn9, alt_create_t::create);
        ASSERT_EQ(NULL_BLOCK_ID, b[12]);
        b[12] = acq12->block_id();

        make_empty(acq12);
        check_and_append(acq12, "", "t9");

        coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9E, this, txn9, lock));
        coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9F, this, txn9, lock));

        condT1.wait();
        condT2.wait();
        acq12.reset();
    }

    void run_txn9D(page_txn_t *txn9, auto_drainer_t::lock_t) {
        auto acq13 = make_scoped<current_page_acq_t>(txn9, alt_create_t::create);
        ASSERT_EQ(NULL_BLOCK_ID, b[13]);
        b[13] = acq13->block_id();

        make_empty(acq13);
        check_and_append(acq13, "", "t9");

        condU.pulse();
        condT3.wait();
        acq13.reset();
    }

    void run_txn9E(page_txn_t *txn9, auto_drainer_t::lock_t) {
        auto acq14 = make_scoped<current_page_acq_t>(txn9, alt_create_t::create);
        ASSERT_EQ(NULL_BLOCK_ID, b[14]);
        b[14] = acq14->block_id();

        condT1.pulse();

        make_empty(acq14);
        check_and_append(acq14, "", "t9");
        acq14.reset();
    }

    void run_txn9F(page_txn_t *txn9, auto_drainer_t::lock_t lock) {
        condU.wait();

        auto acq15 = make_scoped<current_page_acq_t>(txn9, alt_create_t::create);
        ASSERT_EQ(NULL_BLOCK_ID, b[15]);
        b[15] = acq15->block_id();

        condT2.pulse();
        condT3.pulse();

        make_empty(acq15);
        check_and_append(acq15, "", "t10");

        coro_t::spawn_ordered(std::bind(&bigger_test_t::run_txn9G, this, txn9, lock));

        condR3.pulse();
        condS3.wait();
        acq15.reset();
    }

    void run_txn9G(page_txn_t *txn9, auto_drainer_t::lock_t) {
        condR1.wait();
        condR2.wait();
        condR3.wait();

        ASSERT_NE(NULL_BLOCK_ID, b[16]);
        auto acq16 = make_scoped<current_page_acq_t>(txn9, b[16], alt_access_t::write);

        condS1.pulse();
        condS2.pulse();
        condS3.pulse();

        ASSERT_FALSE(acq16->read_acq_signal()->is_pulsed());
        ASSERT_FALSE(acq16->write_acq_signal()->is_pulsed());

        condN.pulse();

        acq16->write_acq_signal()->wait();
        check_and_append(acq16, "t5", "t9");
        acq16.reset();

        condCR4.pulse();
    }

    void run_txn10(auto_drainer_t::lock_t) {
        {
            condP.wait();
            page_txn_t txn10(c);

            auto acq6 = make_scoped<current_page_acq_t>(&txn10, b[6], alt_access_t::read);
            condV.pulse();
            check_value(acq6, "t1t2t9");

            auto acq7 = make_scoped<current_page_acq_t>(&txn10, b[7], alt_access_t::read);
            acq6.reset();

            check_value(acq7, "t2t5");

            auto acq8 = make_scoped<current_page_acq_t>(&txn10, b[8], alt_access_t::read);
            auto acq9 = make_scoped<current_page_acq_t>(&txn10, b[9], alt_access_t::read);
            auto acq10 = make_scoped<current_page_acq_t>(&txn10, b[10], alt_access_t::read);

            acq7->declare_snapshotted();

            acq8->declare_snapshotted();
            acq9->read_acq_signal()->wait();
            acq9->declare_snapshotted();
            acq10->declare_snapshotted();

            check_value(acq8, "t2t4");
            check_value(acq9, "t2");
            check_value(acq10, "t5");

            condX1.wait();
            check_value(acq8, "t2t4");

            condZ1.wait();
            check_value(acq8, "t2t4");

            condZ2.wait();
            check_value(acq8, "t2t4");

            condZ3.wait();
            check_value(acq8, "t2t4");

            condZ4.wait();
            check_value(acq8, "t2t4");

            condZ5.wait();
            check_value(acq8, "t2t4");

            acq8.reset();
            acq9.reset();
            acq10.reset();
            acq7.reset();
        }
    }

    void run_txn11(auto_drainer_t::lock_t) {
        condCR1.wait();
        condCR2.wait();
        condCR3.wait();
        condCR4.wait();
        {
            page_txn_t txn11(c);
            condV.wait();

            auto acq6 = make_scoped<current_page_acq_t>(&txn11, b[6], alt_access_t::write);
            condW.pulse();

            check_value(acq6, "t1t2t9");
            acq6->write_acq_signal()->wait();
            auto acq7 = make_scoped<current_page_acq_t>(&txn11, b[7], alt_access_t::write);
            acq6.reset();
            check_value(acq7, "t2t5");
            acq7->write_acq_signal()->wait();
            auto acq8 = make_scoped<current_page_acq_t>(&txn11, b[8], alt_access_t::write);
            acq7.reset();

            acq8->mark_deleted();

            condX1.pulse();
            acq8.reset();
        }
        condX2.pulse();
    }

    void run_txn12(auto_drainer_t::lock_t) {
        {
            page_txn_t txn12(c);
            condW.wait();

            auto acq6 = make_scoped<current_page_acq_t>(&txn12, b[6], alt_access_t::write);
            condY.pulse();

            check_value(acq6, "t1t2t9");
            acq6->write_acq_signal()->wait();
            auto acq7 = make_scoped<current_page_acq_t>(&txn12, b[7], alt_access_t::write);
            check_value(acq7, "t2t5");
            acq7->write_acq_signal()->wait();

            condX2.wait();

            // Thanks to predictable free list behavior, we expect this new block to have
            // the same block id as the block id b[8], which was deleted by run_txn11
            // just before condX2 got pulsed.
            auto acq8 = make_scoped<current_page_acq_t>(&txn12, alt_create_t::create);
            ASSERT_EQ(b[8], acq8->block_id());
            acq7.reset();

            make_empty(acq8);
            check_and_append(acq8, "", "t12");
            condZ1.pulse();
            acq8.reset();
            condZ2.pulse();
        }
        condZ3.pulse();
    }

    void run_txn13(auto_drainer_t::lock_t) {
        {
            page_txn_t txn13(c);
            condY.wait();

            auto acq6 = make_scoped<current_page_acq_t>(&txn13, b[6], alt_access_t::write);
            check_value(acq6, "t1t2t9");
            acq6->write_acq_signal()->wait();
            auto acq7 = make_scoped<current_page_acq_t>(&txn13, b[7], alt_access_t::write);
            acq6.reset();
            check_value(acq7, "t2t5");
            acq7->write_acq_signal()->wait();
            auto acq8 = make_scoped<current_page_acq_t>(&txn13, b[8], alt_access_t::write);
            acq7.reset();
            check_and_append(acq8, "t12", "t13");
            condZ4.pulse();
            acq8.reset();
        }
        condZ5.pulse();
    }

    void run_txn14(auto_drainer_t::lock_t) {
        {
            page_txn_t txn14(c);
            auto acq3 = make_scoped<current_page_acq_t>(&txn14, b[3],
                                                        alt_access_t::write);
            check_and_append(acq3, "t7", "t14");
            acq3.reset();
            bad1.pulse();
            bad2.wait();
            auto acq4i = make_scoped<current_page_acq_t>(&txn14, b[4],
                                                         alt_access_t::write);
            check_and_append(acq4i, "t7t15", "t14i");
            // We try to re-acquire the same block!  While this txn still holds it!
            auto acq4ii = make_scoped<current_page_acq_t>(&txn14, b[4],
                                                          alt_access_t::write);
            acq4i.reset();
            check_and_append(acq4ii, "t7t15t14i", "t14ii");
            acq4ii.reset();
        }
    }

    void run_txn15(auto_drainer_t::lock_t) {
        {
            page_txn_t txn15(c);
            bad1.wait();
            auto acq3 = make_scoped<current_page_acq_t>(&txn15, b[3],
                                                        alt_access_t::write);
            check_and_append(acq3, "t7t14", "t15");
            acq3.reset();
            auto acq4 = make_scoped<current_page_acq_t>(&txn15, b[4],
                                                        alt_access_t::write);
            check_and_append(acq4, "t7", "t15");
            acq4.reset();
            bad2.pulse();
        }
    }

    void assert_unique_ids() {
        for (size_t i = 0; i < b_len; ++i) {
            if (b[i] != NULL_BLOCK_ID) {
                for (size_t j = 0; j < b_len; ++j) {
                    if (i != j) {
                        ASSERT_NE(b[i], b[j]);
                    }
                }
            }
        }
    }

    void make_empty(const scoped_ptr_t<current_page_acq_t> &acq) {
        page_acq_t page_acq;
        page_acq.init(acq->current_page_for_write(), c);
        const uint32_t n = page_acq.get_buf_size();
        ASSERT_EQ(4080u, n);
        memset(page_acq.get_buf_write(), 0, n);
    }

    void check_page_acq(page_acq_t *page_acq, const std::string &expected) {
        const uint32_t n = page_acq->get_buf_size();
        ASSERT_EQ(4080u, n);
        const char *const p = static_cast<const char *>(page_acq->get_buf_read());

        ASSERT_LE(expected.size() + 1, n);
        ASSERT_EQ(expected, std::string(p));
    }

    void check_value(const scoped_ptr_t<current_page_acq_t> &acq,
                     const std::string &expected) {
        page_acq_t page_acq;
        page_acq.init(acq->current_page_for_read(), c);
        check_page_acq(&page_acq, expected);
    }

    void check_value(page_txn_t *txn, block_id_t block_id,
                     const std::string &expected) {
        SCOPED_TRACE(block_id);
        auto acq = make_scoped<current_page_acq_t>(txn, block_id, alt_access_t::read);
        check_value(acq, expected);
    }

    void check_and_append(const scoped_ptr_t<current_page_acq_t> &acq,
                          const std::string &expected,
                          const std::string &append) {
        check_value(acq, expected);

        {
            page_acq_t page_acq;
            page_t *page_for_write = acq->current_page_for_write();
            page_acq.init(page_for_write, c);
            check_page_acq(&page_acq, expected);

            char *const p = static_cast<char *>(page_acq.get_buf_write());
            ASSERT_EQ(4080u, page_acq.get_buf_size());
            ASSERT_LE(expected.size() + append.size() + 1, page_acq.get_buf_size());
            memcpy(p + expected.size(), append.c_str(), append.size() + 1);
        }
    }

    const uint64_t memory_limit;

    mock_ser_t mock;
    page_cache_t *c;

    // The block ids for the blocks we call b[0] through b[16].  Note that b[i]
    // usually equals [i], but the last time I checked, that's not true for 11, 15,
    // and 16.
    static const size_t b_len = 17;
    block_id_t b[b_len];

    cond_t condA, condB, condC, condD, condE, condF, condG;
    cond_t condH, condI, condJ, condK, condL, condM, condN;
    cond_t condP;
    cond_t condQ1, condQ2, condR1, condR2, condR3, condS1, condS2, condS3;
    cond_t condT1, condT2, condT3, condU;
    cond_t condV, condW;
    cond_t condX1, condX2;
    cond_t condY;
    cond_t condZ1, condZ2, condZ3, condZ4, condZ5;

    cond_t condCR1, condCR2, condCR3, condCR4;

    cond_t t678cond;

    cond_t bad1, bad2;

    page_txn_t *txn1_ptr;
    page_txn_t *txn2_ptr;
};

void run_BiggerTest() {
    bigger_test_t test(GIGABYTE);
    test.run();
}

TEST(PageTest, BiggerTest) {
    run_in_thread_pool(run_BiggerTest, 4);
}

void run_BiggerTestTightMemory() {
    bigger_test_t test(8192);
    test.run();
}

TEST(PageTest, BiggerTestTightMemory) {
    run_in_thread_pool(run_BiggerTestTightMemory, 4);
}

void run_BiggerTestSuperTightMemory() {
    bigger_test_t test(4096);
    test.run();
}

TEST(PageTest, BiggerTestSuperTightMemory) {
    run_in_thread_pool(run_BiggerTestSuperTightMemory, 4);
}

void run_BiggerTestNoMemory() {
    bigger_test_t test(0);
    test.run();
}

TEST(PageTest, BiggerTestNoMemory) {
    run_in_thread_pool(run_BiggerTestNoMemory, 4);
}

}  // namespace unittest
