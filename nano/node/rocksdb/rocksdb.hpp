#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/rocksdb/rocksdb_iterator.hpp>
#include <nano/secure/blockstore_partial.hpp>
#include <nano/secure/common.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace nano
{
class logging_mt;
class rocksdb_config;

/**
 * rocksdb implementation of the block store
 */
class rocksdb_store : public block_store_partial<rocksdb::Slice, rocksdb_store>
{
public:
	rocksdb_store (nano::logger_mt &, boost::filesystem::path const &, nano::rocksdb_config const & = nano::rocksdb_config{}, bool open_read_only = false);
	nano::write_transaction tx_begin_write (std::vector<nano::tables> const & tables_requiring_lock = {}, std::vector<nano::tables> const & tables_no_lock = {}) override;
	nano::read_transaction tx_begin_read () override;

	std::string vendor_get () const override;

	uint64_t count (nano::transaction const & transaction_a, tables table_a) const override;
	void version_put (nano::write_transaction const &, int) override;

	bool exists (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a) const;
	int get (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a, nano::rocksdb_val & value_a) const;
	int put (nano::write_transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a, nano::rocksdb_val const & value_a);
	int del (nano::write_transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	bool copy_db (boost::filesystem::path const & destination) override;
	void rebuild_db (nano::write_transaction const & transaction_a) override;

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a) const
	{
		return nano::store_iterator<Key, Value> (std::make_unique<nano::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a)));
	}

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key) const
	{
		return nano::store_iterator<Key, Value> (std::make_unique<nano::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), &key));
	}

	bool init_error () const override;

private:
	bool error{ false };
	nano::logger_mt & logger;
	// Optimistic transactions are used in write mode
	rocksdb::OptimisticTransactionDB * optimistic_db = nullptr;
	std::unique_ptr<rocksdb::DB> db;
	std::vector<std::unique_ptr<rocksdb::ColumnFamilyHandle>> handles;
	std::shared_ptr<rocksdb::TableFactory> table_factory;
	std::unordered_map<nano::tables, std::mutex> write_lock_mutexes;

	rocksdb::Transaction * tx (nano::transaction const & transaction_a) const;
	std::vector<nano::tables> all_tables () const;

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;
	int drop (nano::write_transaction const &, tables) override;

	rocksdb::ColumnFamilyHandle * table_to_column_family (tables table_a) const;
	int clear (rocksdb::ColumnFamilyHandle * column_family);

	void open (bool & error_a, boost::filesystem::path const & path_a, bool open_read_only_a);

	rocksdb::ColumnFamilyOptions get_cf_options () const;
	void construct_column_family_mutexes ();
	rocksdb::Options get_db_options () const;
	rocksdb::BlockBasedTableOptions get_table_options () const;
	nano::rocksdb_config rocksdb_config;

	constexpr static int base_memtable_size = 16;
	constexpr static int base_block_cache_size = 16;
};

extern template class block_store_partial<rocksdb::Slice, rocksdb_store>;
}
