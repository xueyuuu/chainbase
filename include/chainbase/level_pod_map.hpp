#pragma once

#include <leveldb/cache.h>
#include <leveldb/comparator.h>
#include <leveldb/db.h>

#include <chainbase/exception.hpp>
#include <chainbase/upgrade_leveldb.hpp>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include "datastream_back.hpp"

namespace chainbase {
    namespace db {

        namespace ldb = leveldb;

        /**
         *  @brief implements a high-level API on top of Level DB that stores items using fc::raw / reflection
         *  @note Key must be a POD type
         */
        template<typename Key, typename Value>
        class level_pod_map {
        public:

            void open(const boost::filesystem::path &dir, bool create = true, size_t cache_size = 0) {
                try {
                    assert(!is_open());//, "Database is already open!");

                    ldb::Options opts;
                    opts.comparator = &_comparer;
                    opts.create_if_missing = create;
                    opts.max_open_files = 64;
                    opts.compression = leveldb::kNoCompression;

                    if (cache_size > 0) {
                        opts.write_buffer_size =
                                cache_size / 4; // up to two write buffers may be held in memory simultaneously
                        _cache.reset(leveldb::NewLRUCache(cache_size / 2));
                        opts.block_cache = _cache.get();
                    }

                    if (ldb::kMajorVersion > 1 || (leveldb::kMajorVersion == 1 && leveldb::kMinorVersion >= 16)) {
                        // LevelDB versions before 1.16 consider short writes to be corruption. Only trigger error
                        // on corruption in later versions.
                        opts.paranoid_checks = true;
                    }

                    _read_options.verify_checksums = true;
                    _iter_options.verify_checksums = true;
                    _iter_options.fill_cache = false;
                    _sync_options.sync = true;

                    // Given path must exist to succeed toNativeAnsiPath
                    boost::filesystem::create_directories(dir);
                    std::string ldbPath = dir.generic_string();//to_native_ansi_path();

                    ldb::DB *ndb = nullptr;
                    const auto ntrxstat = ldb::DB::Open(opts, ldbPath.c_str(), &ndb);
                    if (!ntrxstat.ok()) {
                        //elog("Failure opening database: ${db}\nStatus: ${msg}", ("db", dir)("msg", ntrxstat.ToString()));
                        throw level_pod_map_failure();//FC_THROW_EXCEPTION(level_pod_map_open_failure, "Failure opening database: ${db}\nStatus: ${msg}", ("db", dir)("msg", ntrxstat.ToString()));
                    }
                    _db.reset(ndb);

                    try_upgrade_db(dir, ndb, Value::name, sizeof(Value));
                }catch (...) {
                }//FC_CAPTURE_AND_RETHROW((dir)(create)(cache_size))
            }

            bool is_open() const {
                return !!_db;
            }

            void close() {
                _db.reset();
                _cache.reset();
            }

            boost::optional<Value> fetch_optional(const Key &k) {
                try {
                    assert(is_open());//, "Database is not open!");

                    auto itr = find(k);
                    if (itr.valid()) return itr.value();
                    return boost::optional<Value>();
                }catch (...){}
                //FC_RETHROW_EXCEPTIONS(warn, "")
            }

            Value fetch(const Key &key) {
                try {
                    assert(is_open());//, "Database is not open!");

                    ldb::Slice key_slice((char *) &key, sizeof(key));
                    std::string value;
                    auto status = _db->Get(_read_options, key_slice, &value);
                    if (status.IsNotFound()) {
                        throw key_not_found_exception();///FC_THROW_EXCEPTION(fc::key_not_found_exception, "unable to find key ${key}", ("key", key));
                    }
                    if (!status.ok()) {
                        throw level_pod_map_failure();//FC_THROW_EXCEPTION(level_pod_map_failure, "database error: ${msg}", ("msg", status.ToString()));
                    }
                    datastream<const char *> datastream(value.c_str(), value.size());
                    Value tmp;
                    fc::raw::unpack(datastream, tmp);
                    return tmp;
                }catch (...){}
                //FC_RETHROW_EXCEPTIONS(warn, "error fetching key ${key}", ("key", key));
            }

            class iterator {
            public:
                iterator() {}

                bool valid() const {
                    return _it && _it->Valid();
                }

                Key key() const {
                    assert(sizeof(Key) == _it->key().size());
                    return *((Key *) _it->key().data());
                }

                Value value() const {
                    Value tmp_val;
                    datastream<const char *> ds(_it->value().data(), _it->value().size());
                    fc::raw::unpack(ds, tmp_val);
                    return tmp_val;
                }

                iterator &operator++() {
                    _it->Next();
                    return *this;
                }

                iterator &operator--() {
                    _it->Prev();
                    return *this;
                }

            protected:
                friend class level_pod_map;

                iterator(ldb::Iterator *it)
                        : _it(it) {}

                std::shared_ptr<ldb::Iterator> _it;
            };

            iterator begin() {
                try {
                    assert(is_open());, "Database is not open!");

                    iterator itr(_db->NewIterator(_iter_options));
                    itr._it->SeekToFirst();

                    if (itr._it->status().IsNotFound()) {
                        throw key_not_found_exception(); //FC_THROW_EXCEPTION(fc::key_not_found_exception, "");
                    }
                    if (!itr._it->status().ok()) {
                        throw level_pod_map_failure();//FC_THROW_EXCEPTION(level_pod_map_failure, "database error: ${msg}", ("msg", itr._it->status().ToString()));
                    }

                    if (itr.valid()) {
                        return itr;
                    }
                    return iterator();
                }catch (...){

                }
                //FC_RETHROW_EXCEPTIONS(warn, "error seeking to first")
            }

            iterator find(const Key &key) {
                try {
                    assert(is_open());//, "Database is not open!");

                    ldb::Slice key_slice((char *) &key, sizeof(key));
                    iterator itr(_db->NewIterator(_iter_options));
                    itr._it->Seek(key_slice);
                    if (itr.valid() && itr.key() == key) {
                        return itr;
                    }
                    return iterator();
                }catch (...){}
                //FC_RETHROW_EXCEPTIONS(warn, "error finding ${key}", ("key", key))
            }

            iterator lower_bound(const Key &key) {
                try {
                    assert(is_open());//, "Database is not open!");

                    ldb::Slice key_slice((char *) &key, sizeof(key));
                    iterator itr(_db->NewIterator(_iter_options));
                    itr._it->Seek(key_slice);
                    if (itr.valid()) {
                        return itr;
                    }
                    return iterator();
                }
                FC_RETHROW_EXCEPTIONS(warn, "error finding ${key}", ("key", key))
            }

            bool last(Key &k) {
                try {
                    assert(is_open());//, "Database is not open!");

                    std::unique_ptr<ldb::Iterator> it(_db->NewIterator(_iter_options));
                    assert(it != nullptr);
                    it->SeekToLast();
                    if (!it->Valid()) {
                        return false;
                    }
                    assert(sizeof(Key) == it->key().size());
                    k = *((Key *) it->key().data());
                    return true;
                }
                FC_RETHROW_EXCEPTIONS(warn, "error reading last item from database");
            }

            bool last(Key &k, Value &v) {
                try {
                    assert(is_open());//, "Database is not open!");

                    std::unique_ptr<ldb::Iterator> it(_db->NewIterator(_iter_options));
                    assert(it != nullptr);
                    it->SeekToLast();
                    if (!it->Valid()) {
                        return false;
                    }
                    datastream<const char *> ds(it->value().data(), it->value().size());
                    fc::raw::unpack(ds, v);

                    assert(sizeof(Key) == it->key().size());
                    k = *((Key *) it->key().data());
                    return true;
                }
                FC_RETHROW_EXCEPTIONS(warn, "error reading last item from database");
            }

            void store(const Key &k, const Value &v, bool sync = false) {
                try {
                    assert(is_open());//, "Database is not open!");

                    ldb::Slice ks((char *) &k, sizeof(k));
                    auto vec = fc::raw::pack(v);
                    ldb::Slice vs(vec.data(), vec.size());

                    auto status = _db->Put(sync ? _sync_options : _write_options, ks, vs);
                    if (!status.ok()) {
                        FC_THROW_EXCEPTION(level_pod_map_failure, "database error: ${msg}", ("msg", status.ToString()));
                    }
                }
                FC_RETHROW_EXCEPTIONS(warn, "error storing ${key} = ${value}", ("key", k)("value", v));
            }

            void remove(const Key &k, bool sync = false) {
                try {
                    assert(is_open());//, "Database is not open!");

                    ldb::Slice ks((char *) &k, sizeof(k));
                    auto status = _db->Delete(sync ? _sync_options : _write_options, ks);
                    if (status.IsNotFound()) {
                        FC_THROW_EXCEPTION(fc::key_not_found_exception, "unable to find key ${key}", ("key", k));
                    }
                    if (!status.ok()) {
                        FC_THROW_EXCEPTION(level_pod_map_failure, "database error: ${msg}", ("msg", status.ToString()));
                    }
                }
                FC_RETHROW_EXCEPTIONS(warn, "error removing ${key}", ("key", k));
            }

        private:
            class key_compare : public leveldb::Comparator {
            public:
                int Compare(const leveldb::Slice &a, const leveldb::Slice &b) const {
                    assert((a.size() == sizeof(Key)) && (b.size() == sizeof(Key)));
                    Key *ak = (Key *) a.data();
                    Key *bk = (Key *) b.data();
                    if (*ak < *bk) return -1;
                    if (*ak == *bk) return 0;
                    return 1;
                }

                const char *Name() const { return "key_compare"; }

                void FindShortestSeparator(std::string *, const leveldb::Slice &) const {}

                void FindShortSuccessor(std::string *) const {};
            };

            std::unique_ptr<leveldb::DB> _db;
            std::unique_ptr<leveldb::Cache> _cache;
            key_compare _comparer;

            ldb::ReadOptions _read_options;
            ldb::ReadOptions _iter_options;
            ldb::WriteOptions _write_options;
            ldb::WriteOptions _sync_options;
        };

    }
} // bts::db