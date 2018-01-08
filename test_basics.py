#! /usr/bin/env python
# coding=utf-8


from pgclient import *
from twisted.trial import unittest
import uuid


class TestBasics(unittest.TestCase):
    TEST_HKEY = 'test_hkey_1'
    TEST_SKEY = 'test_skey_1'
    TEST_VALUE = 'test_value_1'

    @inlineCallbacks
    def setUp(self):
        self.c = Pegasus(['127.0.1.1:34601', '127.0.0.1:34602', '127.0.0.1:34603'], 'temp')
        ret = yield self.c.init()
        self.assertTrue(ret)

    def tearDown(self):
        self.c.close()

    @inlineCallbacks
    def test_set_ok(self):
        ret = yield self.c.set(self.TEST_HKEY, self.TEST_SKEY, self.TEST_VALUE)
        self.assertEqual(ret, error_types.ERR_OK.value)

    @inlineCallbacks
    def test_set_timeout(self):
        ret = yield self.c.set(self.TEST_HKEY, self.TEST_SKEY, self.TEST_VALUE*100000, 0, 10)
        self.assertEqual(ret, error_types.ERR_TIMEOUT.value)

    @inlineCallbacks
    def test_remove_ok(self):
        ret = yield self.c.remove(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(ret, error_types.ERR_OK.value)

    @inlineCallbacks
    def test_exist_ok(self):
        ret = yield self.c.set(self.TEST_HKEY, self.TEST_SKEY, self.TEST_VALUE)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, v) = yield self.c.exist(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(rc, error_types.ERR_OK.value)

    @inlineCallbacks
    def test_exist_none(self):
        ret = yield self.c.remove(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, v) = yield self.c.exist(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(rc, error_types.ERR_DATA_NOT_EXIST.value)

    @inlineCallbacks
    def test_get_ok(self):
        ret = yield self.c.set(self.TEST_HKEY, self.TEST_SKEY, self.TEST_VALUE)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, v) = yield self.c.get(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(v, self.TEST_VALUE)

    @inlineCallbacks
    def test_get_none(self):
        ret = yield self.c.remove(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, v) = yield self.c.get(self.TEST_HKEY, self.TEST_SKEY)
        self.assertNotEqual(rc, error_types.ERR_OK.value)

    @inlineCallbacks
    def test_ttl_forever(self):
        ret = yield self.c.set(self.TEST_HKEY, self.TEST_SKEY, self.TEST_VALUE)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, v) = yield self.c.ttl(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(v, -1)

    @inlineCallbacks
    def test_ttl_N(self):
        ttl = 60
        ret = yield self.c.set(self.TEST_HKEY, self.TEST_SKEY, self.TEST_VALUE, ttl)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, v) = yield self.c.ttl(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(v, ttl)

    @inlineCallbacks
    def test_ttl_N_with_phase(self):
        ttl = 10
        ret = yield self.c.set(self.TEST_HKEY, self.TEST_SKEY, self.TEST_VALUE, ttl)
        self.assertEqual(ret, error_types.ERR_OK.value)

        period = 3
        d = defer.Deferred()
        reactor.callLater(period, d.callback, 'ok')
        yield d

        (rc, v) = yield self.c.ttl(self.TEST_HKEY, self.TEST_SKEY)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(v, ttl - period)

    @inlineCallbacks
    def test_multi_set_ok(self):
        count = 50
        kvs = {self.TEST_SKEY + str(x): self.TEST_VALUE + str(x) for x in range(count)}
        ret = yield self.c.multi_set(self.TEST_HKEY, kvs)
        self.assertEqual(ret, error_types.ERR_OK.value)

    @inlineCallbacks
    def test_multi_get_ok(self):
        count = 50
        ks = {self.TEST_SKEY + str(x) for x in range(count)}
        kvs = {self.TEST_SKEY + str(x): self.TEST_VALUE + str(x) for x in range(count)}

        ret = yield self.c.multi_set(self.TEST_HKEY, kvs)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, get_kvs) = yield self.c.multi_get(self.TEST_HKEY, ks)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(len(get_kvs), len(kvs))
        self.assertEqual(get_kvs, kvs)

    @inlineCallbacks
    def test_multi_del_ok(self):
        count = 50
        ks = {self.TEST_SKEY + str(x) for x in range(count)}

        (rc, del_count) = yield self.c.multi_del(self.TEST_HKEY, ks)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(del_count, len(ks))

    @inlineCallbacks
    def test_multi_get_part_ok(self):
        count = 50
        ks = {self.TEST_SKEY + str(x) for x in range(count/2)}
        kvs = {self.TEST_SKEY + str(x): self.TEST_VALUE + str(x) for x in range(count)}

        ret = yield self.c.multi_set(self.TEST_HKEY, kvs)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, get_kvs) = yield self.c.multi_get(self.TEST_HKEY, ks)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(len(get_kvs), len(ks))
        for (k, v) in get_kvs.items():
            self.assertIn(k, ks)
            self.assertIn(k, kvs)
            self.assertEqual(v, kvs[k])

    @inlineCallbacks
    def test_multi_get_more_ok(self):
        count = 50
        ks = {self.TEST_SKEY + str(x) for x in range(count)}
        kvs = {self.TEST_SKEY + str(x): self.TEST_VALUE + str(x) for x in range(count/2)}

        (rc, del_count) = yield self.c.multi_del(self.TEST_HKEY, ks)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(del_count, len(ks))

        ret = yield self.c.multi_set(self.TEST_HKEY, kvs)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, get_kvs) = yield self.c.multi_get(self.TEST_HKEY, ks)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(len(get_kvs), len(kvs))
        for (k, v) in get_kvs.items():
            self.assertIn(k, ks)
            self.assertIn(k, kvs)
            self.assertEqual(v, kvs[k])

    @inlineCallbacks
    def test_sort_key_count_ok(self):
        rand_key = uuid.uuid1().hex
        (rc, count) = yield self.c.sort_key_count(self.TEST_HKEY + rand_key)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(count, 0)

    @inlineCallbacks
    def test_get_sort_keys_none(self):
        rand_key = uuid.uuid1().hex
        (rc, get_ks) = yield self.c.get_sort_keys(self.TEST_HKEY + rand_key)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(len(get_ks), 0)

    @inlineCallbacks
    def test_get_sort_keys_ok(self):
        count = 50
        rand_key = uuid.uuid1().hex
        ks = {self.TEST_SKEY + str(x) for x in range(count)}
        kvs = {self.TEST_SKEY + str(x): self.TEST_VALUE + str(x) for x in range(count)}

        (rc, del_count) = yield self.c.multi_del(self.TEST_HKEY + rand_key, ks)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(del_count, len(ks))

        ret = yield self.c.multi_set(self.TEST_HKEY + rand_key, kvs)
        self.assertEqual(ret, error_types.ERR_OK.value)

        (rc, get_ks) = yield self.c.get_sort_keys(self.TEST_HKEY + rand_key)
        self.assertEqual(rc, error_types.ERR_OK.value)
        self.assertEqual(len(get_ks), count)

    @inlineCallbacks
    def test_scan_none(self):
        rand_key = uuid.uuid1().hex
        o = ScanOptions()
        s = self.c.get_scanner(self.TEST_HKEY + rand_key, b'\x00\x00', b'\xFF\xFF', o)
        ret = yield s.get_next()
        self.assertEqual(ret, None)
        s.close()

    @inlineCallbacks
    def test_scan_all(self):
        count = 50
        kvs = {self.TEST_SKEY + str(x): self.TEST_VALUE + str(x) for x in range(count)}

        rand_key = uuid.uuid1().hex
        ret = yield self.c.multi_set(self.TEST_HKEY + rand_key, kvs)
        self.assertEqual(ret, error_types.ERR_OK.value)

        o = ScanOptions()
        s = self.c.get_scanner(self.TEST_HKEY + rand_key, b'\x00\x00', b'\xFF\xFF', o)
        get_count = 0
        last_sk = None
        while True:
            hk_sk_v = yield s.get_next()
            if hk_sk_v is None:
                break
            else:
                get_count += 1
                hk = hk_sk_v[0][0]
                sk = hk_sk_v[0][1]
                v = hk_sk_v[1]

                self.assertNotEqual(sk, None)
                if last_sk is not None:
                    self.assertLess(last_sk, sk)
                last_sk = sk

                self.assertEqual(hk, self.TEST_HKEY + rand_key)
                self.assertIn(sk, kvs)
                self.assertEqual(v, kvs[sk])
        s.close()
        self.assertEqual(count, get_count)

    @inlineCallbacks
    def test_scan_part(self):
        count = 50
        self.assertLess(2, count)
        count_len = len(str(count))
        kvs = {self.TEST_SKEY + str(x).zfill(count_len): self.TEST_VALUE + str(x) for x in range(count)}
        sub_kvs = {self.TEST_SKEY + str(x).zfill(count_len): self.TEST_VALUE + str(x) for x in range(1, count-1)}

        rand_key = uuid.uuid1().hex
        ret = yield self.c.multi_set(self.TEST_HKEY + rand_key, kvs)
        self.assertEqual(ret, error_types.ERR_OK.value)

        o = ScanOptions()
        s = self.c.get_scanner(self.TEST_HKEY + rand_key,
                               self.TEST_SKEY + str(1).zfill(count_len),
                               self.TEST_SKEY + str(count-1).zfill(count_len),
                               o)
        get_count = 0
        last_sk = None
        while True:
            hk_sk_v = yield s.get_next()
            if hk_sk_v is None:
                break
            else:
                get_count += 1
                hk = hk_sk_v[0][0]
                sk = hk_sk_v[0][1]
                v = hk_sk_v[1]

                self.assertNotEqual(sk, None)
                if last_sk is not None:
                    self.assertLess(last_sk, sk)
                last_sk = sk

                self.assertEqual(hk, self.TEST_HKEY + rand_key)
                self.assertIn(sk, sub_kvs)
                self.assertEqual(v, sub_kvs[sk])
        s.close()
        self.assertEqual(len(sub_kvs), get_count)

    @inlineCallbacks
    def test_unordered_scan_none(self):
        split_count = 5
        o = ScanOptions()
        scanners = self.c.get_unordered_scanners(split_count, o)
        self.assertEqual(len(scanners), split_count)
        for scanner in scanners:
            while True:
                hk_sk_v = yield scanner.get_next()
                # self.assertEqual(hk_sk_v, None)                   # TODO there maybe some remain data
                if hk_sk_v is None:
                    break
            scanner.close()

    @inlineCallbacks
    def test_unordered_scan_all(self):
        hkey_count = 20
        count = 50
        rand_key = uuid.uuid1().hex
        hks = {self.TEST_HKEY + rand_key + '_' + str(i) for i in range(hkey_count)}
        kvs = {self.TEST_SKEY + str(x): self.TEST_VALUE + str(x) for x in range(count)}

        for hk in hks:
            ret = yield self.c.multi_set(hk, kvs)
            self.assertEqual(ret, error_types.ERR_OK.value)

        split_count = 5
        o = ScanOptions()
        scanners = self.c.get_unordered_scanners(split_count, o)
        self.assertEqual(len(scanners), split_count)

        get_hk_count = 0
        for scanner in scanners:
            last_hk = None
            last_sk = None
            hk_found = False
            get_count = 0
            while True:
                hk_sk_v = yield scanner.get_next()
                if hk_sk_v is None:
                    break

                hk = hk_sk_v[0][0]
                sk = hk_sk_v[0][1]
                v = hk_sk_v[1]

                self.assertNotEqual(hk, None)
                if last_hk is None:
                    last_hk = hk
                    get_hk_count += 1

                self.assertNotEqual(sk, None)
                if hk == last_hk:
                    get_count += 1
                    if last_sk is not None:
                        self.assertLess(last_sk, sk)
                else:
                    get_hk_count += 1
                    if hk_found:
                        self.assertEqual(count, get_count)
                    get_count = 1
                    last_hk = hk
                last_sk = sk

                if hk in hks:
                    hk_found = True
                    self.assertIn(sk, kvs)
                    self.assertEqual(v, kvs[sk])
                else:
                    hk_found = False

            scanner.close()

        self.assertLessEqual(hkey_count, get_hk_count)
