#include <boost/algorithm/string.hpp>
#include <nano/lib/interface.h>
#include <nano/node/node.hpp>
#include <nano/node/rpc.hpp>

#ifdef NANO_SECURE_RPC
#include <nano/node/rpc_secure.hpp>
#endif

#include <nano/lib/errors.hpp>

namespace
{
void construct_json (nano::seq_con_info_component * component, boost::property_tree::ptree & parent);
}

nano::rpc_secure_config::rpc_secure_config () :
enable (false),
verbose_logging (false)
{
}

nano::error nano::rpc_secure_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("enable", enable);
	json.put ("verbose_logging", verbose_logging);
	json.put ("server_key_passphrase", server_key_passphrase);
	json.put ("server_cert_path", server_cert_path);
	json.put ("server_key_path", server_key_path);
	json.put ("server_dh_path", server_dh_path);
	json.put ("client_certs_path", client_certs_path);
	return json.get_error ();
}

nano::error nano::rpc_secure_config::deserialize_json (nano::jsonconfig & json)
{
	json.get_required<bool> ("enable", enable);
	json.get_required<bool> ("verbose_logging", verbose_logging);
	json.get_required<std::string> ("server_key_passphrase", server_key_passphrase);
	json.get_required<std::string> ("server_cert_path", server_cert_path);
	json.get_required<std::string> ("server_key_path", server_key_path);
	json.get_required<std::string> ("server_dh_path", server_dh_path);
	json.get_required<std::string> ("client_certs_path", client_certs_path);
	return json.get_error ();
}

nano::rpc_config::rpc_config (bool enable_control_a) :
address (boost::asio::ip::address_v6::loopback ()),
port (nano::rpc::rpc_port),
enable_control (enable_control_a),
frontier_request_limit (16384),
chain_request_limit (16384),
max_json_depth (20),
enable_sign_hash (false)
{
}

nano::error nano::rpc_config::serialize_json (nano::jsonconfig & json) const
{
	json.put ("address", address.to_string ());
	json.put ("port", port);
	json.put ("enable_control", enable_control);
	json.put ("frontier_request_limit", frontier_request_limit);
	json.put ("chain_request_limit", chain_request_limit);
	json.put ("max_json_depth", max_json_depth);
	json.put ("enable_sign_hash", enable_sign_hash);
	return json.get_error ();
}

nano::error nano::rpc_config::deserialize_json (nano::jsonconfig & json)
{
	auto rpc_secure_l (json.get_optional_child ("secure"));
	if (rpc_secure_l)
	{
		secure.deserialize_json (*rpc_secure_l);
	}

	json.get_required<boost::asio::ip::address_v6> ("address", address);
	json.get_optional<uint16_t> ("port", port);
	json.get_optional<bool> ("enable_control", enable_control);
	json.get_optional<uint64_t> ("frontier_request_limit", frontier_request_limit);
	json.get_optional<uint64_t> ("chain_request_limit", chain_request_limit);
	json.get_optional<uint8_t> ("max_json_depth", max_json_depth);
	json.get_optional<bool> ("enable_sign_hash", enable_sign_hash);
	return json.get_error ();
}

nano::rpc::rpc (boost::asio::io_context & io_ctx_a, nano::node & node_a, nano::rpc_config const & config_a) :
acceptor (io_ctx_a),
config (config_a),
node (node_a)
{
}

void nano::rpc::add_block_observer ()
{
	node.observers.blocks.add ([this](std::shared_ptr<nano::block> block_a, nano::account const & account_a, nano::uint128_t const &, bool) {
		observer_action (account_a);
	});
}

void nano::rpc::start (bool rpc_enabled_a)
{
	if (rpc_enabled_a)
	{
		auto endpoint (nano::tcp_endpoint (config.address, config.port));
		acceptor.open (endpoint.protocol ());
		acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

		boost::system::error_code ec;
		acceptor.bind (endpoint, ec);
		if (ec)
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("Error while binding for RPC on port %1%: %2%") % endpoint.port () % ec.message ());
			throw std::runtime_error (ec.message ());
		}

		acceptor.listen ();
	}

	add_block_observer ();

	if (rpc_enabled_a)
	{
		accept ();
	}
}

void nano::rpc::accept ()
{
	auto connection (std::make_shared<nano::rpc_connection> (node, *this));
	acceptor.async_accept (connection->socket, [this, connection](boost::system::error_code const & ec) {
		if (ec != boost::asio::error::operation_aborted && acceptor.is_open ())
		{
			accept ();
		}
		if (!ec)
		{
			connection->parse_connection ();
		}
		else
		{
			BOOST_LOG (this->node.log) << boost::str (boost::format ("Error accepting RPC connections: %1% (%2%)") % ec.message () % ec.value ());
		}
	});
}

void nano::rpc::stop ()
{
	acceptor.close ();
}

nano::rpc_handler::rpc_handler (nano::node & node_a, nano::rpc & rpc_a, std::string const & body_a, std::string const & request_id_a, std::function<void(boost::property_tree::ptree const &)> const & response_a) :
body (body_a),
request_id (request_id_a),
node (node_a),
rpc (rpc_a),
response (response_a)
{
}

void nano::rpc::observer_action (nano::account const & account_a)
{
	std::shared_ptr<nano::payment_observer> observer;
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (payment_observers.find (account_a));
		if (existing != payment_observers.end ())
		{
			observer = existing->second;
		}
	}
	if (observer != nullptr)
	{
		observer->observe ();
	}
}

void nano::error_response (std::function<void(boost::property_tree::ptree const &)> response_a, std::string const & message_a)
{
	boost::property_tree::ptree response_l;
	response_l.put ("error", message_a);
	response_a (response_l);
}

void nano::rpc_handler::response_errors ()
{
	if (ec || response_l.empty ())
	{
		boost::property_tree::ptree response_error;
		response_error.put ("error", ec ? ec.message () : "Empty response");
		response (response_error);
	}
	else
	{
		response (response_l);
	}
}

std::shared_ptr<nano::wallet> nano::rpc_handler::wallet_impl ()
{
	if (!ec)
	{
		std::string wallet_text (request.get<std::string> ("wallet"));
		nano::uint256_union wallet;
		if (!wallet.decode_hex (wallet_text))
		{
			auto existing (node.wallets.items.find (wallet));
			if (existing != node.wallets.items.end ())
			{
				return existing->second;
			}
			else
			{
				ec = nano::error_common::wallet_not_found;
			}
		}
		else
		{
			ec = nano::error_common::bad_wallet_number;
		}
	}
	return nullptr;
}

bool nano::rpc_handler::wallet_locked_impl (nano::transaction const & transaction_a, std::shared_ptr<nano::wallet> wallet_a)
{
	bool result (false);
	if (!ec)
	{
		if (!wallet_a->store.valid_password (transaction_a))
		{
			ec = nano::error_common::wallet_locked;
			result = true;
		}
	}
	return result;
}

bool nano::rpc_handler::wallet_account_impl (nano::transaction const & transaction_a, std::shared_ptr<nano::wallet> wallet_a, nano::account const & account_a)
{
	bool result (false);
	if (!ec)
	{
		if (wallet_a->store.find (transaction_a, account_a) != wallet_a->store.end ())
		{
			result = true;
		}
		else
		{
			ec = nano::error_common::account_not_found_wallet;
		}
	}
	return result;
}

nano::account nano::rpc_handler::account_impl (std::string account_text)
{
	nano::account result (0);
	if (!ec)
	{
		if (account_text.empty ())
		{
			account_text = request.get<std::string> ("account");
		}
		if (result.decode_account (account_text))
		{
			ec = nano::error_common::bad_account_number;
		}
	}
	return result;
}

nano::amount nano::rpc_handler::amount_impl ()
{
	nano::amount result (0);
	if (!ec)
	{
		std::string amount_text (request.get<std::string> ("amount"));
		if (result.decode_dec (amount_text))
		{
			ec = nano::error_common::invalid_amount;
		}
	}
	return result;
}

std::shared_ptr<nano::block> nano::rpc_handler::block_impl (bool signature_work_required)
{
	std::shared_ptr<nano::block> result;
	if (!ec)
	{
		std::string block_text (request.get<std::string> ("block"));
		boost::property_tree::ptree block_l;
		std::stringstream block_stream (block_text);
		boost::property_tree::read_json (block_stream, block_l);
		if (!signature_work_required)
		{
			block_l.put ("signature", "0");
			block_l.put ("work", "0");
		}
		result = nano::deserialize_block_json (block_l);
		if (result == nullptr)
		{
			ec = nano::error_blocks::invalid_block;
		}
	}
	return result;
}

nano::block_hash nano::rpc_handler::hash_impl (std::string search_text)
{
	nano::block_hash result (0);
	if (!ec)
	{
		std::string hash_text (request.get<std::string> (search_text));
		if (result.decode_hex (hash_text))
		{
			ec = nano::error_blocks::invalid_block_hash;
		}
	}
	return result;
}

nano::amount nano::rpc_handler::threshold_optional_impl ()
{
	nano::amount result (0);
	boost::optional<std::string> threshold_text (request.get_optional<std::string> ("threshold"));
	if (!ec && threshold_text.is_initialized ())
	{
		if (result.decode_dec (threshold_text.get ()))
		{
			ec = nano::error_common::bad_threshold;
		}
	}
	return result;
}

uint64_t nano::rpc_handler::work_optional_impl ()
{
	uint64_t result (0);
	boost::optional<std::string> work_text (request.get_optional<std::string> ("work"));
	if (!ec && work_text.is_initialized ())
	{
		if (nano::from_string_hex (work_text.get (), result))
		{
			ec = nano::error_common::bad_work_format;
		}
	}
	return result;
}

namespace
{
bool decode_unsigned (std::string const & text, uint64_t & number)
{
	bool result;
	size_t end;
	try
	{
		number = std::stoull (text, &end);
		result = false;
	}
	catch (std::invalid_argument const &)
	{
		result = true;
	}
	catch (std::out_of_range const &)
	{
		result = true;
	}
	result = result || end != text.size ();
	return result;
}
}

uint64_t nano::rpc_handler::count_impl ()
{
	uint64_t result (0);
	if (!ec)
	{
		std::string count_text (request.get<std::string> ("count"));
		if (decode_unsigned (count_text, result) || result == 0)
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

uint64_t nano::rpc_handler::count_optional_impl (uint64_t result)
{
	boost::optional<std::string> count_text (request.get_optional<std::string> ("count"));
	if (!ec && count_text.is_initialized ())
	{
		if (decode_unsigned (count_text.get (), result))
		{
			ec = nano::error_common::invalid_count;
		}
	}
	return result;
}

uint64_t nano::rpc_handler::offset_optional_impl (uint64_t result)
{
	boost::optional<std::string> offset_text (request.get_optional<std::string> ("offset"));
	if (!ec && offset_text.is_initialized ())
	{
		if (decode_unsigned (offset_text.get (), result))
		{
			ec = nano::error_rpc::invalid_offset;
		}
	}
	return result;
}

bool nano::rpc_handler::rpc_control_impl ()
{
	bool result (false);
	if (!ec)
	{
		if (!rpc.config.enable_control)
		{
			ec = nano::error_rpc::rpc_control_disabled;
		}
		else
		{
			result = true;
		}
	}
	return result;
}

void nano::rpc_handler::account_balance ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto balance (node.balance_pending (account));
		response_l.put ("balance", balance.first.convert_to<std::string> ());
		response_l.put ("pending", balance.second.convert_to<std::string> ());
	}
	response_errors ();
}

void nano::rpc_handler::account_block_count ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		nano::account_info info;
		if (!node.store.account_get (transaction, account, info))
		{
			response_l.put ("block_count", std::to_string (info.block_count));
		}
		else
		{
			ec = nano::error_common::account_not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::account_create ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		const bool generate_work = request.get<bool> ("work", true);
		nano::account new_key;
		auto index_text (request.get_optional<std::string> ("index"));
		if (index_text.is_initialized ())
		{
			uint64_t index;
			if (decode_unsigned (index_text.get (), index) || index > static_cast<uint64_t> (std::numeric_limits<uint32_t>::max ()))
			{
				ec = nano::error_common::invalid_index;
			}
			else
			{
				new_key = wallet->deterministic_insert (static_cast<uint32_t> (index), generate_work);
			}
		}
		else
		{
			new_key = wallet->deterministic_insert (generate_work);
		}

		if (!ec)
		{
			if (!new_key.is_zero ())
			{
				response_l.put ("account", new_key.to_account ());
			}
			else
			{
				ec = nano::error_common::wallet_locked;
			}
		}
	}
	response_errors ();
}

void nano::rpc_handler::account_get ()
{
	std::string key_text (request.get<std::string> ("key"));
	nano::uint256_union pub;
	if (!pub.decode_hex (key_text))
	{
		response_l.put ("account", pub.to_account ());
	}
	else
	{
		ec = nano::error_common::bad_public_key;
	}
	response_errors ();
}

void nano::rpc_handler::account_info ()
{
	auto account (account_impl ());
	if (!ec)
	{
		const bool representative = request.get<bool> ("representative", false);
		const bool weight = request.get<bool> ("weight", false);
		const bool pending = request.get<bool> ("pending", false);
		auto transaction (node.store.tx_begin_read ());
		nano::account_info info;
		if (!node.store.account_get (transaction, account, info))
		{
			response_l.put ("frontier", info.head.to_string ());
			response_l.put ("open_block", info.open_block.to_string ());
			response_l.put ("representative_block", info.rep_block.to_string ());
			std::string balance;
			nano::uint128_union (info.balance).encode_dec (balance);
			response_l.put ("balance", balance);
			response_l.put ("modified_timestamp", std::to_string (info.modified));
			response_l.put ("block_count", std::to_string (info.block_count));
			response_l.put ("account_version", info.epoch == nano::epoch::epoch_1 ? "1" : "0");
			if (representative)
			{
				auto block (node.store.block_get (transaction, info.rep_block));
				assert (block != nullptr);
				response_l.put ("representative", block->representative ().to_account ());
			}
			if (weight)
			{
				auto account_weight (node.ledger.weight (transaction, account));
				response_l.put ("weight", account_weight.convert_to<std::string> ());
			}
			if (pending)
			{
				auto account_pending (node.ledger.account_pending (transaction, account));
				response_l.put ("pending", account_pending.convert_to<std::string> ());
			}
		}
		else
		{
			ec = nano::error_common::account_not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::account_key ()
{
	auto account (account_impl ());
	if (!ec)
	{
		response_l.put ("key", account.to_string ());
	}
	response_errors ();
}

void nano::rpc_handler::account_list ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree accounts;
		auto transaction (node.wallets.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), j (wallet->store.end ()); i != j; ++i)
		{
			boost::property_tree::ptree entry;
			entry.put ("", nano::account (i->first).to_account ());
			accounts.push_back (std::make_pair ("", entry));
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void nano::rpc_handler::account_move ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string source_text (request.get<std::string> ("source"));
		auto accounts_text (request.get_child ("accounts"));
		nano::uint256_union source;
		if (!source.decode_hex (source_text))
		{
			auto existing (node.wallets.items.find (source));
			if (existing != node.wallets.items.end ())
			{
				auto source (existing->second);
				std::vector<nano::public_key> accounts;
				for (auto i (accounts_text.begin ()), n (accounts_text.end ()); i != n; ++i)
				{
					nano::public_key account;
					account.decode_account (i->second.get<std::string> (""));
					accounts.push_back (account);
				}
				auto transaction (node.wallets.tx_begin_write ());
				auto error (wallet->store.move (transaction, source->store, accounts));
				response_l.put ("moved", error ? "0" : "1");
			}
			else
			{
				ec = nano::error_rpc::source_not_found;
			}
		}
		else
		{
			ec = nano::error_rpc::bad_source;
		}
	}
	response_errors ();
}

void nano::rpc_handler::account_remove ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_write ());
		wallet_locked_impl (transaction, wallet);
		wallet_account_impl (transaction, wallet, account);
		if (!ec)
		{
			wallet->store.erase (transaction, account);
			response_l.put ("removed", "1");
		}
	}
	response_errors ();
}

void nano::rpc_handler::account_representative ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		nano::account_info info;
		if (!node.store.account_get (transaction, account, info))
		{
			auto block (node.store.block_get (transaction, info.rep_block));
			assert (block != nullptr);
			response_l.put ("representative", block->representative ().to_account ());
		}
		else
		{
			ec = nano::error_common::account_not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::account_representative_set ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	if (!ec)
	{
		std::string representative_text (request.get<std::string> ("representative"));
		nano::account representative;
		if (!representative.decode_account (representative_text))
		{
			auto work (work_optional_impl ());
			if (!ec && work)
			{
				auto transaction (node.wallets.tx_begin_write ());
				wallet_locked_impl (transaction, wallet);
				if (!ec)
				{
					nano::account_info info;
					auto block_transaction (node.store.tx_begin_read ());
					if (!node.store.account_get (block_transaction, account, info))
					{
						if (nano::work_validate (info.head, work))
						{
							ec = nano::error_common::invalid_work;
						}
					}
					else
					{
						ec = nano::error_common::account_not_found;
					}
				}
			}
			if (!ec)
			{
				bool generate_work (work == 0); // Disable work generation if "work" option is provided
				auto response_a (response);
				// clang-format off
				wallet->change_async (account, representative, [response_a](std::shared_ptr<nano::block> block) {
					nano::block_hash hash (0);
					if (block != nullptr)
					{
						hash = block->hash ();
					}
					boost::property_tree::ptree response_l;
					response_l.put ("block", hash.to_string ());
					response_a (response_l);
				},
				work, generate_work);
				// clang-format on
			}
		}
		else
		{
			ec = nano::error_rpc::bad_representative_number;
		}
	}
	// Because of change_async
	if (ec)
	{
		response_errors ();
	}
}

void nano::rpc_handler::account_weight ()
{
	auto account (account_impl ());
	if (!ec)
	{
		auto balance (node.weight (account));
		response_l.put ("weight", balance.convert_to<std::string> ());
	}
	response_errors ();
}

void nano::rpc_handler::accounts_balances ()
{
	boost::property_tree::ptree balances;
	for (auto & accounts : request.get_child ("accounts"))
	{
		auto account (account_impl (accounts.second.data ()));
		if (!ec)
		{
			boost::property_tree::ptree entry;
			auto balance (node.balance_pending (account));
			entry.put ("balance", balance.first.convert_to<std::string> ());
			entry.put ("pending", balance.second.convert_to<std::string> ());
			balances.push_back (std::make_pair (account.to_account (), entry));
		}
	}
	response_l.add_child ("balances", balances);
	response_errors ();
}

void nano::rpc_handler::accounts_create ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		const bool generate_work = request.get<bool> ("work", false);
		boost::property_tree::ptree accounts;
		for (auto i (0); accounts.size () < count; ++i)
		{
			nano::account new_key (wallet->deterministic_insert (generate_work));
			if (!new_key.is_zero ())
			{
				boost::property_tree::ptree entry;
				entry.put ("", new_key.to_account ());
				accounts.push_back (std::make_pair ("", entry));
			}
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void nano::rpc_handler::accounts_frontiers ()
{
	boost::property_tree::ptree frontiers;
	auto transaction (node.store.tx_begin_read ());
	for (auto & accounts : request.get_child ("accounts"))
	{
		auto account (account_impl (accounts.second.data ()));
		if (!ec)
		{
			auto latest (node.ledger.latest (transaction, account));
			if (!latest.is_zero ())
			{
				frontiers.put (account.to_account (), latest.to_string ());
			}
		}
	}
	response_l.add_child ("frontiers", frontiers);
	response_errors ();
}

void nano::rpc_handler::accounts_pending ()
{
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	const bool source = request.get<bool> ("source", false);
	const bool include_active = request.get<bool> ("include_active", false);
	boost::property_tree::ptree pending;
	auto transaction (node.store.tx_begin_read ());
	for (auto & accounts : request.get_child ("accounts"))
	{
		auto account (account_impl (accounts.second.data ()));
		if (!ec)
		{
			boost::property_tree::ptree peers_l;
			for (auto i (node.store.pending_begin (transaction, nano::pending_key (account, 0))); nano::pending_key (i->first).account == account && peers_l.size () < count; ++i)
			{
				nano::pending_key key (i->first);
				std::shared_ptr<nano::block> block (include_active ? nullptr : node.store.block_get (transaction, key.hash));
				if (include_active || (block && !node.active.active (*block)))
				{
					if (threshold.is_zero () && !source)
					{
						boost::property_tree::ptree entry;
						entry.put ("", key.hash.to_string ());
						peers_l.push_back (std::make_pair ("", entry));
					}
					else
					{
						nano::pending_info info (i->second);
						if (info.amount.number () >= threshold.number ())
						{
							if (source)
							{
								boost::property_tree::ptree pending_tree;
								pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
								pending_tree.put ("source", info.source.to_account ());
								peers_l.add_child (key.hash.to_string (), pending_tree);
							}
							else
							{
								peers_l.put (key.hash.to_string (), info.amount.number ().convert_to<std::string> ());
							}
						}
					}
				}
			}
			pending.add_child (account.to_account (), peers_l);
		}
	}
	response_l.add_child ("blocks", pending);
	response_errors ();
}

void nano::rpc_handler::available_supply ()
{
	auto genesis_balance (node.balance (nano::genesis_account)); // Cold storage genesis
	auto landing_balance (node.balance (nano::account ("059F68AAB29DE0D3A27443625C7EA9CDDB6517A8B76FE37727EF6A4D76832AD5"))); // Active unavailable account
	auto faucet_balance (node.balance (nano::account ("8E319CE6F3025E5B2DF66DA7AB1467FE48F1679C13DD43BFDB29FA2E9FC40D3B"))); // Faucet account
	auto burned_balance ((node.balance_pending (nano::account (0))).second); // Burning 0 account
	auto available (nano::genesis_amount - genesis_balance - landing_balance - faucet_balance - burned_balance);
	response_l.put ("available", available.convert_to<std::string> ());
	response_errors ();
}

void nano::rpc_handler::block_info ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		nano::block_sideband sideband;
		auto transaction (node.store.tx_begin_read ());
		auto block (node.store.block_get (transaction, hash, &sideband));
		if (block != nullptr)
		{
			nano::account account (block->account ().is_zero () ? sideband.account : block->account ());
			response_l.put ("block_account", account.to_account ());
			auto amount (node.ledger.amount (transaction, hash));
			response_l.put ("amount", amount.convert_to<std::string> ());
			auto balance (node.ledger.balance (transaction, hash));
			response_l.put ("balance", balance.convert_to<std::string> ());
			response_l.put ("height", std::to_string (sideband.height));
			response_l.put ("local_timestamp", std::to_string (sideband.timestamp));
			std::string contents;
			block->serialize_json (contents);
			response_l.put ("contents", contents);
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::block_confirm ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto block_l (node.store.block_get (transaction, hash));
		if (block_l != nullptr)
		{
			node.block_confirm (std::move (block_l));
			response_l.put ("started", "1");
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::blocks ()
{
	std::vector<std::string> hashes;
	boost::property_tree::ptree blocks;
	auto transaction (node.store.tx_begin_read ());
	for (boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
	{
		if (!ec)
		{
			std::string hash_text = hashes.second.data ();
			nano::uint256_union hash;
			if (!hash.decode_hex (hash_text))
			{
				auto block (node.store.block_get (transaction, hash));
				if (block != nullptr)
				{
					std::string contents;
					block->serialize_json (contents);
					blocks.put (hash_text, contents);
				}
				else
				{
					ec = nano::error_blocks::not_found;
				}
			}
			else
			{
				ec = nano::error_blocks::bad_hash_number;
			}
		}
	}
	response_l.add_child ("blocks", blocks);
	response_errors ();
}

void nano::rpc_handler::blocks_info ()
{
	const bool pending = request.get<bool> ("pending", false);
	const bool source = request.get<bool> ("source", false);
	std::vector<std::string> hashes;
	boost::property_tree::ptree blocks;
	auto transaction (node.store.tx_begin_read ());
	for (boost::property_tree::ptree::value_type & hashes : request.get_child ("hashes"))
	{
		if (!ec)
		{
			std::string hash_text = hashes.second.data ();
			nano::uint256_union hash;
			if (!hash.decode_hex (hash_text))
			{
				nano::block_sideband sideband;
				auto block (node.store.block_get (transaction, hash, &sideband));
				if (block != nullptr)
				{
					boost::property_tree::ptree entry;
					nano::account account (block->account ().is_zero () ? sideband.account : block->account ());
					entry.put ("block_account", account.to_account ());
					auto amount (node.ledger.amount (transaction, hash));
					entry.put ("amount", amount.convert_to<std::string> ());
					auto balance (node.ledger.balance (transaction, hash));
					entry.put ("balance", balance.convert_to<std::string> ());
					entry.put ("height", std::to_string (sideband.height));
					entry.put ("local_timestamp", std::to_string (sideband.timestamp));
					std::string contents;
					block->serialize_json (contents);
					entry.put ("contents", contents);
					if (pending)
					{
						bool exists (false);
						auto destination (node.ledger.block_destination (transaction, *block));
						if (!destination.is_zero ())
						{
							exists = node.store.pending_exists (transaction, nano::pending_key (destination, hash));
						}
						entry.put ("pending", exists ? "1" : "0");
					}
					if (source)
					{
						nano::block_hash source_hash (node.ledger.block_source (transaction, *block));
						auto block_a (node.store.block_get (transaction, source_hash));
						if (block_a != nullptr)
						{
							auto source_account (node.ledger.account (transaction, source_hash));
							entry.put ("source_account", source_account.to_account ());
						}
						else
						{
							entry.put ("source_account", "0");
						}
					}
					blocks.push_back (std::make_pair (hash_text, entry));
				}
				else
				{
					ec = nano::error_blocks::not_found;
				}
			}
			else
			{
				ec = nano::error_blocks::bad_hash_number;
			}
		}
	}
	response_l.add_child ("blocks", blocks);
	response_errors ();
}

void nano::rpc_handler::block_account ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		if (node.store.block_exists (transaction, hash))
		{
			auto account (node.ledger.account (transaction, hash));
			response_l.put ("account", account.to_account ());
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::block_count ()
{
	auto transaction (node.store.tx_begin_read ());
	response_l.put ("count", std::to_string (node.store.block_count (transaction).sum ()));
	response_l.put ("unchecked", std::to_string (node.store.unchecked_count (transaction)));
	response_errors ();
}

void nano::rpc_handler::block_count_type ()
{
	auto transaction (node.store.tx_begin_read ());
	nano::block_counts count (node.store.block_count (transaction));
	response_l.put ("send", std::to_string (count.send));
	response_l.put ("receive", std::to_string (count.receive));
	response_l.put ("open", std::to_string (count.open));
	response_l.put ("change", std::to_string (count.change));
	response_l.put ("state_v0", std::to_string (count.state_v0));
	response_l.put ("state_v1", std::to_string (count.state_v1));
	response_l.put ("state", std::to_string (count.state_v0 + count.state_v1));
	response_errors ();
}

void nano::rpc_handler::block_create ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string type (request.get<std::string> ("type"));
		nano::uint256_union wallet (0);
		boost::optional<std::string> wallet_text (request.get_optional<std::string> ("wallet"));
		if (wallet_text.is_initialized ())
		{
			if (wallet.decode_hex (wallet_text.get ()))
			{
				ec = nano::error_common::bad_wallet_number;
			}
		}
		nano::uint256_union account (0);
		boost::optional<std::string> account_text (request.get_optional<std::string> ("account"));
		if (!ec && account_text.is_initialized ())
		{
			if (account.decode_account (account_text.get ()))
			{
				ec = nano::error_common::bad_account_number;
			}
		}
		nano::uint256_union representative (0);
		boost::optional<std::string> representative_text (request.get_optional<std::string> ("representative"));
		if (!ec && representative_text.is_initialized ())
		{
			if (representative.decode_account (representative_text.get ()))
			{
				ec = nano::error_rpc::bad_representative_number;
			}
		}
		nano::uint256_union destination (0);
		boost::optional<std::string> destination_text (request.get_optional<std::string> ("destination"));
		if (!ec && destination_text.is_initialized ())
		{
			if (destination.decode_account (destination_text.get ()))
			{
				ec = nano::error_rpc::bad_destination;
			}
		}
		nano::block_hash source (0);
		boost::optional<std::string> source_text (request.get_optional<std::string> ("source"));
		if (!ec && source_text.is_initialized ())
		{
			if (source.decode_hex (source_text.get ()))
			{
				ec = nano::error_rpc::bad_source;
			}
		}
		nano::uint128_union amount (0);
		boost::optional<std::string> amount_text (request.get_optional<std::string> ("amount"));
		if (!ec && amount_text.is_initialized ())
		{
			if (amount.decode_dec (amount_text.get ()))
			{
				ec = nano::error_common::invalid_amount;
			}
		}
		auto work (work_optional_impl ());
		nano::raw_key prv;
		prv.data.clear ();
		nano::uint256_union previous (0);
		nano::uint128_union balance (0);
		if (!ec && wallet != 0 && account != 0)
		{
			auto existing (node.wallets.items.find (wallet));
			if (existing != node.wallets.items.end ())
			{
				auto transaction (node.wallets.tx_begin_read ());
				auto block_transaction (node.store.tx_begin_read ());
				wallet_locked_impl (transaction, existing->second);
				wallet_account_impl (transaction, existing->second, account);
				if (!ec)
				{
					existing->second->store.fetch (transaction, account, prv);
					previous = node.ledger.latest (block_transaction, account);
					balance = node.ledger.account_balance (block_transaction, account);
				}
			}
			else
			{
				ec = nano::error_common::wallet_not_found;
			}
		}
		boost::optional<std::string> key_text (request.get_optional<std::string> ("key"));
		if (!ec && key_text.is_initialized ())
		{
			if (prv.data.decode_hex (key_text.get ()))
			{
				ec = nano::error_common::bad_private_key;
			}
		}
		boost::optional<std::string> previous_text (request.get_optional<std::string> ("previous"));
		if (!ec && previous_text.is_initialized ())
		{
			if (previous.decode_hex (previous_text.get ()))
			{
				ec = nano::error_rpc::bad_previous;
			}
		}
		boost::optional<std::string> balance_text (request.get_optional<std::string> ("balance"));
		if (!ec && balance_text.is_initialized ())
		{
			if (balance.decode_dec (balance_text.get ()))
			{
				ec = nano::error_rpc::invalid_balance;
			}
		}
		nano::uint256_union link (0);
		boost::optional<std::string> link_text (request.get_optional<std::string> ("link"));
		if (!ec && link_text.is_initialized ())
		{
			if (link.decode_account (link_text.get ()))
			{
				if (link.decode_hex (link_text.get ()))
				{
					ec = nano::error_rpc::bad_link;
				}
			}
		}
		else
		{
			// Retrieve link from source or destination
			link = source.is_zero () ? destination : source;
		}
		if (prv.data != 0)
		{
			nano::uint256_union pub (nano::pub_key (prv.data));
			// Fetching account balance & previous for send blocks (if aren't given directly)
			if (!previous_text.is_initialized () && !balance_text.is_initialized ())
			{
				auto transaction (node.store.tx_begin_read ());
				previous = node.ledger.latest (transaction, pub);
				balance = node.ledger.account_balance (transaction, pub);
			}
			// Double check current balance if previous block is specified
			else if (previous_text.is_initialized () && balance_text.is_initialized () && type == "send")
			{
				auto transaction (node.store.tx_begin_read ());
				if (node.store.block_exists (transaction, previous) && node.store.block_balance (transaction, previous) != balance.number ())
				{
					ec = nano::error_rpc::block_create_balance_mismatch;
				}
			}
			// Check for incorrect account key
			if (!ec && account_text.is_initialized ())
			{
				if (account != pub)
				{
					ec = nano::error_rpc::block_create_public_key_mismatch;
				}
			}
			if (type == "state")
			{
				if (previous_text.is_initialized () && !representative.is_zero () && (!link.is_zero () || link_text.is_initialized ()))
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (previous.is_zero () ? pub : previous);
					}
					nano::state_block state (pub, previous, representative, balance, link, prv, pub, work);
					response_l.put ("hash", state.hash ().to_string ());
					std::string contents;
					state.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_state;
				}
			}
			else if (type == "open")
			{
				if (representative != 0 && source != 0)
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (pub);
					}
					nano::open_block open (source, representative, pub, prv, pub, work);
					response_l.put ("hash", open.hash ().to_string ());
					std::string contents;
					open.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_open;
				}
			}
			else if (type == "receive")
			{
				if (source != 0 && previous != 0)
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (previous);
					}
					nano::receive_block receive (previous, source, prv, pub, work);
					response_l.put ("hash", receive.hash ().to_string ());
					std::string contents;
					receive.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_receive;
				}
			}
			else if (type == "change")
			{
				if (representative != 0 && previous != 0)
				{
					if (work == 0)
					{
						work = node.work_generate_blocking (previous);
					}
					nano::change_block change (previous, representative, prv, pub, work);
					response_l.put ("hash", change.hash ().to_string ());
					std::string contents;
					change.serialize_json (contents);
					response_l.put ("block", contents);
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_change;
				}
			}
			else if (type == "send")
			{
				if (destination != 0 && previous != 0 && balance != 0 && amount != 0)
				{
					if (balance.number () >= amount.number ())
					{
						if (work == 0)
						{
							work = node.work_generate_blocking (previous);
						}
						nano::send_block send (previous, destination, balance.number () - amount.number (), prv, pub, work);
						response_l.put ("hash", send.hash ().to_string ());
						std::string contents;
						send.serialize_json (contents);
						response_l.put ("block", contents);
					}
					else
					{
						ec = nano::error_common::insufficient_balance;
					}
				}
				else
				{
					ec = nano::error_rpc::block_create_requirements_send;
				}
			}
			else
			{
				ec = nano::error_blocks::invalid_type;
			}
		}
		else
		{
			ec = nano::error_rpc::block_create_key_required;
		}
	}
	response_errors ();
}

void nano::rpc_handler::block_hash ()
{
	auto block (block_impl (false));
	if (!ec)
	{
		response_l.put ("hash", block->hash ().to_string ());
	}
	response_errors ();
}

void nano::rpc_handler::bootstrap ()
{
	std::string address_text = request.get<std::string> ("address");
	std::string port_text = request.get<std::string> ("port");
	boost::system::error_code address_ec;
	auto address (boost::asio::ip::address_v6::from_string (address_text, address_ec));
	if (!address_ec)
	{
		uint16_t port;
		if (!nano::parse_port (port_text, port))
		{
			node.bootstrap_initiator.bootstrap (nano::endpoint (address, port));
			response_l.put ("success", "");
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	else
	{
		ec = nano::error_common::invalid_ip_address;
	}
	response_errors ();
}

void nano::rpc_handler::bootstrap_any ()
{
	node.bootstrap_initiator.bootstrap ();
	response_l.put ("success", "");
	response_errors ();
}

void nano::rpc_handler::bootstrap_lazy ()
{
	rpc_control_impl ();
	auto hash (hash_impl ());
	const bool force = request.get<bool> ("force", false);
	if (!ec)
	{
		node.bootstrap_initiator.bootstrap_lazy (hash, force);
		response_l.put ("started", "1");
	}
	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::rpc_handler::bootstrap_status ()
{
	auto attempt (node.bootstrap_initiator.current_attempt ());
	if (attempt != nullptr)
	{
		response_l.put ("clients", std::to_string (attempt->clients.size ()));
		response_l.put ("pulls", std::to_string (attempt->pulls.size ()));
		response_l.put ("pulling", std::to_string (attempt->pulling));
		response_l.put ("connections", std::to_string (attempt->connections));
		response_l.put ("idle", std::to_string (attempt->idle.size ()));
		response_l.put ("target_connections", std::to_string (attempt->target_connections (attempt->pulls.size ())));
		response_l.put ("total_blocks", std::to_string (attempt->total_blocks));
		response_l.put ("runs_count", std::to_string (attempt->runs_count));
		std::string mode_text;
		if (attempt->mode == nano::bootstrap_mode::legacy)
		{
			mode_text = "legacy";
		}
		else if (attempt->mode == nano::bootstrap_mode::lazy)
		{
			mode_text = "lazy";
		}
		else if (attempt->mode == nano::bootstrap_mode::wallet_lazy)
		{
			mode_text = "wallet_lazy";
		}
		response_l.put ("mode", mode_text);
		response_l.put ("lazy_blocks", std::to_string (attempt->lazy_blocks.size ()));
		response_l.put ("lazy_state_unknown", std::to_string (attempt->lazy_state_unknown.size ()));
		response_l.put ("lazy_balances", std::to_string (attempt->lazy_balances.size ()));
		response_l.put ("lazy_pulls", std::to_string (attempt->lazy_pulls.size ()));
		response_l.put ("lazy_stopped", std::to_string (attempt->lazy_stopped));
		response_l.put ("lazy_keys", std::to_string (attempt->lazy_keys.size ()));
		if (!attempt->lazy_keys.empty ())
		{
			response_l.put ("lazy_key_1", (*(attempt->lazy_keys.begin ())).to_string ());
		}
	}
	else
	{
		response_l.put ("active", "0");
	}
	response_errors ();
}

void nano::rpc_handler::chain (bool successors)
{
	successors = successors != request.get<bool> ("reverse", false);
	auto hash (hash_impl ("block"));
	auto count (count_impl ());
	auto offset (offset_optional_impl (0));
	if (!ec)
	{
		boost::property_tree::ptree blocks;
		auto transaction (node.store.tx_begin_read ());
		while (!hash.is_zero () && blocks.size () < count)
		{
			auto block_l (node.store.block_get (transaction, hash));
			if (block_l != nullptr)
			{
				if (offset > 0)
				{
					--offset;
				}
				else
				{
					boost::property_tree::ptree entry;
					entry.put ("", hash.to_string ());
					blocks.push_back (std::make_pair ("", entry));
				}
				hash = successors ? node.store.block_successor (transaction, hash) : block_l->previous ();
			}
			else
			{
				hash.clear ();
			}
		}
		response_l.add_child ("blocks", blocks);
	}
	response_errors ();
}

void nano::rpc_handler::confirmation_active ()
{
	uint64_t announcements (0);
	boost::optional<std::string> announcements_text (request.get_optional<std::string> ("announcements"));
	if (announcements_text.is_initialized ())
	{
		announcements = strtoul (announcements_text.get ().c_str (), NULL, 10);
	}
	boost::property_tree::ptree elections;
	{
		std::lock_guard<std::mutex> lock (node.active.mutex);
		for (auto i (node.active.roots.begin ()), n (node.active.roots.end ()); i != n; ++i)
		{
			if (i->election->announcements >= announcements && !i->election->confirmed && !i->election->stopped)
			{
				boost::property_tree::ptree entry;
				entry.put ("", i->root.to_string ());
				elections.push_back (std::make_pair ("", entry));
			}
		}
	}
	response_l.add_child ("confirmations", elections);
	response_errors ();
}

void nano::rpc_handler::confirmation_history ()
{
	boost::property_tree::ptree elections;
	boost::property_tree::ptree confirmation_stats;
	std::chrono::milliseconds running_total (0);
	nano::block_hash hash (0);
	boost::optional<std::string> hash_text (request.get_optional<std::string> ("hash"));
	if (hash_text.is_initialized ())
	{
		hash = hash_impl ();
	}
	if (!ec)
	{
		auto confirmed (node.active.list_confirmed ());
		for (auto i (confirmed.begin ()), n (confirmed.end ()); i != n; ++i)
		{
			if (hash.is_zero () || i->winner->hash () == hash)
			{
				boost::property_tree::ptree election;
				election.put ("hash", i->winner->hash ().to_string ());
				election.put ("duration", i->election_duration.count ());
				election.put ("time", i->election_end.count ());
				election.put ("tally", i->tally.to_string_dec ());
				elections.push_back (std::make_pair ("", election));
			}
			running_total += i->election_duration;
		}
	}
	confirmation_stats.put ("count", elections.size ());
	if (elections.size () >= 1)
	{
		confirmation_stats.put ("average", (running_total.count ()) / elections.size ());
	}
	response_l.add_child ("confirmation_stats", confirmation_stats);
	response_l.add_child ("confirmations", elections);
	response_errors ();
}

void nano::rpc_handler::confirmation_info ()
{
	const bool representatives = request.get<bool> ("representatives", false);
	const bool contents = request.get<bool> ("contents", true);
	std::string root_text (request.get<std::string> ("root"));
	nano::uint512_union root;
	if (!root.decode_hex (root_text))
	{
		std::lock_guard<std::mutex> lock (node.active.mutex);
		auto conflict_info (node.active.roots.find (root));
		if (conflict_info != node.active.roots.end ())
		{
			response_l.put ("announcements", std::to_string (conflict_info->election->announcements));
			auto election (conflict_info->election);
			nano::uint128_t total (0);
			response_l.put ("last_winner", election->status.winner->hash ().to_string ());
			auto transaction (node.store.tx_begin_read ());
			auto tally_l (election->tally (transaction));
			boost::property_tree::ptree blocks;
			for (auto i (tally_l.begin ()), n (tally_l.end ()); i != n; ++i)
			{
				boost::property_tree::ptree entry;
				auto tally (i->first);
				entry.put ("tally", tally.convert_to<std::string> ());
				total += tally;
				if (contents)
				{
					std::string contents;
					i->second->serialize_json (contents);
					entry.put ("contents", contents);
				}
				if (representatives)
				{
					std::multimap<nano::uint128_t, nano::account, std::greater<nano::uint128_t>> representatives;
					for (auto ii (election->last_votes.begin ()), nn (election->last_votes.end ()); ii != nn; ++ii)
					{
						if (i->second->hash () == ii->second.hash)
						{
							nano::account representative (ii->first);
							auto amount (node.store.representation_get (transaction, representative));
							representatives.insert (std::make_pair (amount, representative));
						}
					}
					boost::property_tree::ptree representatives_list;
					for (auto ii (representatives.begin ()), nn (representatives.end ()); ii != nn; ++ii)
					{
						representatives_list.put (ii->second.to_account (), ii->first.convert_to<std::string> ());
					}
					entry.add_child ("representatives", representatives_list);
				}
				blocks.add_child ((i->second->hash ()).to_string (), entry);
			}
			response_l.put ("total_tally", total.convert_to<std::string> ());
			response_l.add_child ("blocks", blocks);
		}
		else
		{
			ec = nano::error_rpc::confirmation_not_found;
		}
	}
	else
	{
		ec = nano::error_rpc::invalid_root;
	}
	response_errors ();
}

void nano::rpc_handler::confirmation_quorum ()
{
	response_l.put ("quorum_delta", node.delta ().convert_to<std::string> ());
	response_l.put ("online_weight_quorum_percent", std::to_string (node.config.online_weight_quorum));
	response_l.put ("online_weight_minimum", node.config.online_weight_minimum.to_string_dec ());
	response_l.put ("online_stake_total", node.online_reps.online_stake ().convert_to<std::string> ());
	response_l.put ("peers_stake_total", node.peers.total_weight ().convert_to<std::string> ());
	if (request.get<bool> ("peer_details", false))
	{
		boost::property_tree::ptree peers;
		for (auto & peer : node.peers.list_probable_rep_weights ())
		{
			boost::property_tree::ptree peer_node;
			peer_node.put ("account", peer.probable_rep_account.to_account ());
			peer_node.put ("ip", peer.ip_address.to_string ());
			peer_node.put ("weight", peer.rep_weight.to_string_dec ());
			peers.push_back (std::make_pair ("", peer_node));
		}
		response_l.add_child ("peers", peers);
	}
	response_errors ();
}

void nano::rpc_handler::delegators ()
{
	auto account (account_impl ());
	if (!ec)
	{
		boost::property_tree::ptree delegators;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.latest_begin (transaction)), n (node.store.latest_end ()); i != n; ++i)
		{
			nano::account_info info (i->second);
			auto block (node.store.block_get (transaction, info.rep_block));
			assert (block != nullptr);
			if (block->representative () == account)
			{
				std::string balance;
				nano::uint128_union (info.balance).encode_dec (balance);
				delegators.put (nano::account (i->first).to_account (), balance);
			}
		}
		response_l.add_child ("delegators", delegators);
	}
	response_errors ();
}

void nano::rpc_handler::delegators_count ()
{
	auto account (account_impl ());
	if (!ec)
	{
		uint64_t count (0);
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.latest_begin (transaction)), n (node.store.latest_end ()); i != n; ++i)
		{
			nano::account_info info (i->second);
			auto block (node.store.block_get (transaction, info.rep_block));
			assert (block != nullptr);
			if (block->representative () == account)
			{
				++count;
			}
		}
		response_l.put ("count", std::to_string (count));
	}
	response_errors ();
}

void nano::rpc_handler::deterministic_key ()
{
	std::string seed_text (request.get<std::string> ("seed"));
	std::string index_text (request.get<std::string> ("index"));
	nano::raw_key seed;
	if (!seed.data.decode_hex (seed_text))
	{
		try
		{
			uint32_t index (std::stoul (index_text));
			nano::uint256_union prv;
			nano::deterministic_key (seed.data, index, prv);
			nano::uint256_union pub (nano::pub_key (prv));
			response_l.put ("private", prv.to_string ());
			response_l.put ("public", pub.to_string ());
			response_l.put ("account", pub.to_account ());
		}
		catch (std::logic_error const &)
		{
			ec = nano::error_common::invalid_index;
		}
	}
	else
	{
		ec = nano::error_common::bad_seed;
	}
	response_errors ();
}

void nano::rpc_handler::frontiers ()
{
	auto start (account_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		boost::property_tree::ptree frontiers;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n && frontiers.size () < count; ++i)
		{
			frontiers.put (nano::account (i->first).to_account (), nano::account_info (i->second).head.to_string ());
		}
		response_l.add_child ("frontiers", frontiers);
	}
	response_errors ();
}

void nano::rpc_handler::account_count ()
{
	auto transaction (node.store.tx_begin_read ());
	auto size (node.store.account_count (transaction));
	response_l.put ("count", std::to_string (size));
	response_errors ();
}

namespace
{
class history_visitor : public nano::block_visitor
{
public:
	history_visitor (nano::rpc_handler & handler_a, bool raw_a, nano::transaction & transaction_a, boost::property_tree::ptree & tree_a, nano::block_hash const & hash_a) :
	handler (handler_a),
	raw (raw_a),
	transaction (transaction_a),
	tree (tree_a),
	hash (hash_a)
	{
	}
	virtual ~history_visitor () = default;
	void send_block (nano::send_block const & block_a)
	{
		tree.put ("type", "send");
		auto account (block_a.hashables.destination.to_account ());
		tree.put ("account", account);
		auto amount (handler.node.ledger.amount (transaction, hash).convert_to<std::string> ());
		tree.put ("amount", amount);
		if (raw)
		{
			tree.put ("destination", account);
			tree.put ("balance", block_a.hashables.balance.to_string_dec ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void receive_block (nano::receive_block const & block_a)
	{
		tree.put ("type", "receive");
		auto account (handler.node.ledger.account (transaction, block_a.hashables.source).to_account ());
		tree.put ("account", account);
		auto amount (handler.node.ledger.amount (transaction, hash).convert_to<std::string> ());
		tree.put ("amount", amount);
		if (raw)
		{
			tree.put ("source", block_a.hashables.source.to_string ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void open_block (nano::open_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "open");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("source", block_a.hashables.source.to_string ());
			tree.put ("opened", block_a.hashables.account.to_account ());
		}
		else
		{
			// Report opens as a receive
			tree.put ("type", "receive");
		}
		if (block_a.hashables.source != nano::genesis_account)
		{
			tree.put ("account", handler.node.ledger.account (transaction, block_a.hashables.source).to_account ());
			tree.put ("amount", handler.node.ledger.amount (transaction, hash).convert_to<std::string> ());
		}
		else
		{
			tree.put ("account", nano::genesis_account.to_account ());
			tree.put ("amount", nano::genesis_amount.convert_to<std::string> ());
		}
	}
	void change_block (nano::change_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "change");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
	}
	void state_block (nano::state_block const & block_a)
	{
		if (raw)
		{
			tree.put ("type", "state");
			tree.put ("representative", block_a.hashables.representative.to_account ());
			tree.put ("link", block_a.hashables.link.to_string ());
			tree.put ("balance", block_a.hashables.balance.to_string_dec ());
			tree.put ("previous", block_a.hashables.previous.to_string ());
		}
		auto balance (block_a.hashables.balance.number ());
		auto previous_balance (handler.node.ledger.balance (transaction, block_a.hashables.previous));
		if (balance < previous_balance)
		{
			if (raw)
			{
				tree.put ("subtype", "send");
			}
			else
			{
				tree.put ("type", "send");
			}
			tree.put ("account", block_a.hashables.link.to_account ());
			tree.put ("amount", (previous_balance - balance).convert_to<std::string> ());
		}
		else
		{
			if (block_a.hashables.link.is_zero ())
			{
				if (raw)
				{
					tree.put ("subtype", "change");
				}
			}
			else if (balance == previous_balance && !handler.node.ledger.epoch_link.is_zero () && handler.node.ledger.is_epoch_link (block_a.hashables.link))
			{
				if (raw)
				{
					tree.put ("subtype", "epoch");
					tree.put ("account", handler.node.ledger.epoch_signer.to_account ());
				}
			}
			else
			{
				if (raw)
				{
					tree.put ("subtype", "receive");
				}
				else
				{
					tree.put ("type", "receive");
				}
				tree.put ("account", handler.node.ledger.account (transaction, block_a.hashables.link).to_account ());
				tree.put ("amount", (balance - previous_balance).convert_to<std::string> ());
			}
		}
	}
	nano::rpc_handler & handler;
	bool raw;
	nano::transaction & transaction;
	boost::property_tree::ptree & tree;
	nano::block_hash const & hash;
};
}

void nano::rpc_handler::account_history ()
{
	nano::account account;
	bool output_raw (request.get_optional<bool> ("raw") == true);
	nano::block_hash hash;
	auto head_str (request.get_optional<std::string> ("head"));
	auto transaction (node.store.tx_begin_read ());
	if (head_str)
	{
		if (!hash.decode_hex (*head_str))
		{
			if (node.store.block_exists (transaction, hash))
			{
				account = node.ledger.account (transaction, hash);
			}
			else
			{
				ec = nano::error_blocks::not_found;
			}
		}
		else
		{
			ec = nano::error_blocks::bad_hash_number;
		}
	}
	else
	{
		account = account_impl ();
		if (!ec)
		{
			hash = node.ledger.latest (transaction, account);
		}
	}
	auto count (count_impl ());
	auto offset (offset_optional_impl (0));
	if (!ec)
	{
		boost::property_tree::ptree history;
		response_l.put ("account", account.to_account ());
		nano::block_sideband sideband;
		auto block (node.store.block_get (transaction, hash, &sideband));
		while (block != nullptr && count > 0)
		{
			if (offset > 0)
			{
				--offset;
			}
			else
			{
				boost::property_tree::ptree entry;
				history_visitor visitor (*this, output_raw, transaction, entry, hash);
				block->visit (visitor);
				if (!entry.empty ())
				{
					entry.put ("local_timestamp", std::to_string (sideband.timestamp));
					entry.put ("hash", hash.to_string ());
					if (output_raw)
					{
						entry.put ("work", nano::to_string_hex (block->block_work ()));
						entry.put ("signature", block->block_signature ().to_string ());
					}
					history.push_back (std::make_pair ("", entry));
					--count;
				}
			}
			hash = block->previous ();
			block = node.store.block_get (transaction, hash, &sideband);
		}
		response_l.add_child ("history", history);
		if (!hash.is_zero ())
		{
			response_l.put ("previous", hash.to_string ());
		}
	}
	response_errors ();
}

void nano::rpc_handler::keepalive ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string address_text (request.get<std::string> ("address"));
		std::string port_text (request.get<std::string> ("port"));
		uint16_t port;
		if (!nano::parse_port (port_text, port))
		{
			node.keepalive (address_text, port);
			response_l.put ("started", "1");
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	response_errors ();
}

void nano::rpc_handler::key_create ()
{
	nano::keypair pair;
	response_l.put ("private", pair.prv.data.to_string ());
	response_l.put ("public", pair.pub.to_string ());
	response_l.put ("account", pair.pub.to_account ());
	response_errors ();
}

void nano::rpc_handler::key_expand ()
{
	std::string key_text (request.get<std::string> ("key"));
	nano::uint256_union prv;
	if (!prv.decode_hex (key_text))
	{
		nano::uint256_union pub (nano::pub_key (prv));
		response_l.put ("private", prv.to_string ());
		response_l.put ("public", pub.to_string ());
		response_l.put ("account", pub.to_account ());
	}
	else
	{
		ec = nano::error_common::bad_private_key;
	}
	response_errors ();
}

void nano::rpc_handler::ledger ()
{
	rpc_control_impl ();
	auto count (count_optional_impl ());
	if (!ec)
	{
		nano::account start (0);
		boost::optional<std::string> account_text (request.get_optional<std::string> ("account"));
		if (account_text.is_initialized ())
		{
			if (start.decode_account (account_text.get ()))
			{
				ec = nano::error_common::bad_account_number;
			}
		}
		uint64_t modified_since (0);
		boost::optional<std::string> modified_since_text (request.get_optional<std::string> ("modified_since"));
		if (modified_since_text.is_initialized ())
		{
			if (decode_unsigned (modified_since_text.get (), modified_since))
			{
				ec = nano::error_rpc::invalid_timestamp;
			}
		}
		const bool sorting = request.get<bool> ("sorting", false);
		const bool representative = request.get<bool> ("representative", false);
		const bool weight = request.get<bool> ("weight", false);
		const bool pending = request.get<bool> ("pending", false);
		boost::property_tree::ptree accounts;
		auto transaction (node.store.tx_begin_read ());
		if (!ec && !sorting) // Simple
		{
			for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n && accounts.size () < count; ++i)
			{
				nano::account_info info (i->second);
				if (info.modified >= modified_since)
				{
					nano::account account (i->first);
					boost::property_tree::ptree response_a;
					response_a.put ("frontier", info.head.to_string ());
					response_a.put ("open_block", info.open_block.to_string ());
					response_a.put ("representative_block", info.rep_block.to_string ());
					std::string balance;
					nano::uint128_union (info.balance).encode_dec (balance);
					response_a.put ("balance", balance);
					response_a.put ("modified_timestamp", std::to_string (info.modified));
					response_a.put ("block_count", std::to_string (info.block_count));
					if (representative)
					{
						auto block (node.store.block_get (transaction, info.rep_block));
						assert (block != nullptr);
						response_a.put ("representative", block->representative ().to_account ());
					}
					if (weight)
					{
						auto account_weight (node.ledger.weight (transaction, account));
						response_a.put ("weight", account_weight.convert_to<std::string> ());
					}
					if (pending)
					{
						auto account_pending (node.ledger.account_pending (transaction, account));
						response_a.put ("pending", account_pending.convert_to<std::string> ());
					}
					accounts.push_back (std::make_pair (account.to_account (), response_a));
				}
			}
		}
		else if (!ec) // Sorting
		{
			std::vector<std::pair<nano::uint128_union, nano::account>> ledger_l;
			for (auto i (node.store.latest_begin (transaction, start)), n (node.store.latest_end ()); i != n; ++i)
			{
				nano::account_info info (i->second);
				nano::uint128_union balance (info.balance);
				if (info.modified >= modified_since)
				{
					ledger_l.push_back (std::make_pair (balance, nano::account (i->first)));
				}
			}
			std::sort (ledger_l.begin (), ledger_l.end ());
			std::reverse (ledger_l.begin (), ledger_l.end ());
			nano::account_info info;
			for (auto i (ledger_l.begin ()), n (ledger_l.end ()); i != n && accounts.size () < count; ++i)
			{
				node.store.account_get (transaction, i->second, info);
				nano::account account (i->second);
				boost::property_tree::ptree response_a;
				response_a.put ("frontier", info.head.to_string ());
				response_a.put ("open_block", info.open_block.to_string ());
				response_a.put ("representative_block", info.rep_block.to_string ());
				std::string balance;
				(i->first).encode_dec (balance);
				response_a.put ("balance", balance);
				response_a.put ("modified_timestamp", std::to_string (info.modified));
				response_a.put ("block_count", std::to_string (info.block_count));
				if (representative)
				{
					auto block (node.store.block_get (transaction, info.rep_block));
					assert (block != nullptr);
					response_a.put ("representative", block->representative ().to_account ());
				}
				if (weight)
				{
					auto account_weight (node.ledger.weight (transaction, account));
					response_a.put ("weight", account_weight.convert_to<std::string> ());
				}
				if (pending)
				{
					auto account_pending (node.ledger.account_pending (transaction, account));
					response_a.put ("pending", account_pending.convert_to<std::string> ());
				}
				accounts.push_back (std::make_pair (account.to_account (), response_a));
			}
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void nano::rpc_handler::mFLR_from_raw (nano::uint128_t ratio)
{
	auto amount (amount_impl ());
	if (!ec)
	{
		auto result (amount.number () / ratio);
		response_l.put ("amount", result.convert_to<std::string> ());
	}
	response_errors ();
}

void nano::rpc_handler::mFLR_to_raw (nano::uint128_t ratio)
{
	auto amount (amount_impl ());
	if (!ec)
	{
		auto result (amount.number () * ratio);
		if (result > amount.number ())
		{
			response_l.put ("amount", result.convert_to<std::string> ());
		}
		else
		{
			ec = nano::error_common::invalid_amount_big;
		}
	}
	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::rpc_handler::node_id ()
{
	rpc_control_impl ();
	if (!ec)
	{
		response_l.put ("private", node.node_id.prv.data.to_string ());
		response_l.put ("public", node.node_id.pub.to_string ());
		response_l.put ("as_account", node.node_id.pub.to_account ());
	}
	response_errors ();
}

/*
 * @warning This is an internal/diagnostic RPC, do not rely on its interface being stable
 */
void nano::rpc_handler::node_id_delete ()
{
	rpc_control_impl ();
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		node.store.delete_node_id (transaction);
		response_l.put ("deleted", "1");
	}
	response_errors ();
}

void nano::rpc_handler::password_change ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_write ());
		std::string password_text (request.get<std::string> ("password"));
		auto error (wallet->store.rekey (transaction, password_text));
		response_l.put ("changed", error ? "0" : "1");
	}
	response_errors ();
}

void nano::rpc_handler::password_enter ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string password_text (request.get<std::string> ("password"));
		auto transaction (wallet->wallets.tx_begin_write ());
		auto error (wallet->enter_password (transaction, password_text));
		response_l.put ("valid", error ? "0" : "1");
	}
	response_errors ();
}

void nano::rpc_handler::password_valid (bool wallet_locked)
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		auto valid (wallet->store.valid_password (transaction));
		if (!wallet_locked)
		{
			response_l.put ("valid", valid ? "1" : "0");
		}
		else
		{
			response_l.put ("locked", valid ? "0" : "1");
		}
	}
	response_errors ();
}

void nano::rpc_handler::peers ()
{
	boost::property_tree::ptree peers_l;
	const bool peer_details = request.get<bool> ("peer_details", false);
	auto peers_list (node.peers.list_vector (std::numeric_limits<size_t>::max ()));
	std::sort (peers_list.begin (), peers_list.end ());
	for (auto i (peers_list.begin ()), n (peers_list.end ()); i != n; ++i)
	{
		std::stringstream text;
		text << i->endpoint;
		if (peer_details)
		{
			boost::property_tree::ptree pending_tree;
			pending_tree.put ("protocol_version", std::to_string (i->network_version));
			if (i->node_id.is_initialized ())
			{
				pending_tree.put ("node_id", i->node_id.get ().to_account ());
			}
			else
			{
				pending_tree.put ("node_id", "");
			}
			peers_l.push_back (boost::property_tree::ptree::value_type (text.str (), pending_tree));
		}
		else
		{
			peers_l.push_back (boost::property_tree::ptree::value_type (text.str (), boost::property_tree::ptree (std::to_string (i->network_version))));
		}
	}
	response_l.add_child ("peers", peers_l);
	response_errors ();
}

void nano::rpc_handler::pending ()
{
	auto account (account_impl ());
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	const bool source = request.get<bool> ("source", false);
	const bool min_version = request.get<bool> ("min_version", false);
	const bool include_active = request.get<bool> ("include_active", false);
	if (!ec)
	{
		boost::property_tree::ptree peers_l;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.pending_begin (transaction, nano::pending_key (account, 0))); nano::pending_key (i->first).account == account && peers_l.size () < count; ++i)
		{
			nano::pending_key key (i->first);
			std::shared_ptr<nano::block> block (include_active ? nullptr : node.store.block_get (transaction, key.hash));
			if (include_active || (block && !node.active.active (*block)))
			{
				if (threshold.is_zero () && !source && !min_version)
				{
					boost::property_tree::ptree entry;
					entry.put ("", key.hash.to_string ());
					peers_l.push_back (std::make_pair ("", entry));
				}
				else
				{
					nano::pending_info info (i->second);
					if (info.amount.number () >= threshold.number ())
					{
						if (source || min_version)
						{
							boost::property_tree::ptree pending_tree;
							pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
							if (source)
							{
								pending_tree.put ("source", info.source.to_account ());
							}
							if (min_version)
							{
								pending_tree.put ("min_version", info.epoch == nano::epoch::epoch_1 ? "1" : "0");
							}
							peers_l.add_child (key.hash.to_string (), pending_tree);
						}
						else
						{
							peers_l.put (key.hash.to_string (), info.amount.number ().convert_to<std::string> ());
						}
					}
				}
			}
		}
		response_l.add_child ("blocks", peers_l);
	}
	response_errors ();
}

void nano::rpc_handler::pending_exists ()
{
	auto hash (hash_impl ());
	const bool include_active = request.get<bool> ("include_active", false);
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		auto block (node.store.block_get (transaction, hash));
		if (block != nullptr)
		{
			auto exists (false);
			auto destination (node.ledger.block_destination (transaction, *block));
			if (!destination.is_zero ())
			{
				exists = node.store.pending_exists (transaction, nano::pending_key (destination, hash));
			}
			exists = exists && (include_active || !node.active.active (*block));
			response_l.put ("exists", exists ? "1" : "0");
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::payment_begin ()
{
	std::string id_text (request.get<std::string> ("wallet"));
	nano::uint256_union id;
	if (!id.decode_hex (id_text))
	{
		auto existing (node.wallets.items.find (id));
		if (existing != node.wallets.items.end ())
		{
			auto transaction (node.wallets.tx_begin_write ());
			std::shared_ptr<nano::wallet> wallet (existing->second);
			if (wallet->store.valid_password (transaction))
			{
				nano::account account (0);
				do
				{
					auto existing (wallet->free_accounts.begin ());
					if (existing != wallet->free_accounts.end ())
					{
						account = *existing;
						wallet->free_accounts.erase (existing);
						if (wallet->store.find (transaction, account) == wallet->store.end ())
						{
							BOOST_LOG (node.log) << boost::str (boost::format ("Transaction wallet %1% externally modified listing account %2% as free but no longer exists") % id.to_string () % account.to_account ());
							account.clear ();
						}
						else
						{
							auto block_transaction (node.store.tx_begin_read ());
							if (!node.ledger.account_balance (block_transaction, account).is_zero ())
							{
								BOOST_LOG (node.log) << boost::str (boost::format ("Skipping account %1% for use as a transaction account: non-zero balance") % account.to_account ());
								account.clear ();
							}
						}
					}
					else
					{
						account = wallet->deterministic_insert (transaction);
						break;
					}
				} while (account.is_zero ());
				if (!account.is_zero ())
				{
					response_l.put ("account", account.to_account ());
				}
				else
				{
					ec = nano::error_rpc::payment_unable_create_account;
				}
			}
			else
			{
				ec = nano::error_common::wallet_locked;
			}
		}
		else
		{
			ec = nano::error_common::wallet_not_found;
		}
	}
	else
	{
		ec = nano::error_common::bad_wallet_number;
	}
	response_errors ();
}

void nano::rpc_handler::payment_init ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_write ());
		if (wallet->store.valid_password (transaction))
		{
			wallet->init_free_accounts (transaction);
			response_l.put ("status", "Ready");
		}
		else
		{
			ec = nano::error_common::wallet_locked;
		}
	}
	response_errors ();
}

void nano::rpc_handler::payment_end ()
{
	auto account (account_impl ());
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		wallet_account_impl (transaction, wallet, account);
		if (!ec)
		{
			if (node.ledger.account_balance (block_transaction, account).is_zero ())
			{
				wallet->free_accounts.insert (account);
				response_l.put ("ended", "1");
			}
			else
			{
				ec = nano::error_rpc::payment_account_balance;
			}
		}
	}
	response_errors ();
}

void nano::rpc_handler::payment_wait ()
{
	std::string timeout_text (request.get<std::string> ("timeout"));
	auto account (account_impl ());
	auto amount (amount_impl ());
	if (!ec)
	{
		uint64_t timeout;
		if (!decode_unsigned (timeout_text, timeout))
		{
			{
				auto observer (std::make_shared<nano::payment_observer> (response, rpc, account, amount));
				observer->start (timeout);
				std::lock_guard<std::mutex> lock (rpc.mutex);
				assert (rpc.payment_observers.find (account) == rpc.payment_observers.end ());
				rpc.payment_observers[account] = observer;
			}
			rpc.observer_action (account);
		}
		else
		{
			ec = nano::error_rpc::bad_timeout;
		}
	}
	if (ec)
	{
		response_errors ();
	}
}

void nano::rpc_handler::process ()
{
	auto block (block_impl (true));
	// State blocks subtype check
	if (!ec && block->type () == nano::block_type::state)
	{
		std::string subtype_text (request.get<std::string> ("subtype", ""));
		if (!subtype_text.empty ())
		{
			std::shared_ptr<nano::state_block> block_state (std::static_pointer_cast<nano::state_block> (block));
			auto transaction (node.store.tx_begin_read ());
			if (!block_state->hashables.previous.is_zero () && !node.store.block_exists (transaction, block_state->hashables.previous))
			{
				ec = nano::error_process::gap_previous;
			}
			else
			{
				auto balance (node.ledger.account_balance (transaction, block_state->hashables.account));
				if (subtype_text == "send")
				{
					if (balance <= block_state->hashables.balance.number ())
					{
						ec = nano::error_rpc::invalid_subtype_balance;
					}
					// Send with previous == 0 fails balance check. No previous != 0 check required
				}
				else if (subtype_text == "receive")
				{
					if (balance > block_state->hashables.balance.number ())
					{
						ec = nano::error_rpc::invalid_subtype_balance;
					}
					// Receive can be point to open block. No previous != 0 check required
				}
				else if (subtype_text == "open")
				{
					if (!block_state->hashables.previous.is_zero ())
					{
						ec = nano::error_rpc::invalid_subtype_previous;
					}
				}
				else if (subtype_text == "change")
				{
					if (balance != block_state->hashables.balance.number ())
					{
						ec = nano::error_rpc::invalid_subtype_balance;
					}
					else if (block_state->hashables.previous.is_zero ())
					{
						ec = nano::error_rpc::invalid_subtype_previous;
					}
				}
				else if (subtype_text == "epoch")
				{
					if (balance != block_state->hashables.balance.number ())
					{
						ec = nano::error_rpc::invalid_subtype_balance;
					}
					else if (!node.ledger.is_epoch_link (block_state->hashables.link))
					{
						ec = ec = nano::error_rpc::invalid_subtype_epoch_link;
					}
				}
				else
				{
					ec = nano::error_rpc::invalid_subtype;
				}
			}
		}
	}
	if (!ec)
	{
		if (!nano::work_validate (*block))
		{
			auto hash (block->hash ());
			node.block_arrival.add (hash);
			nano::process_return result;
			{
				auto transaction (node.store.tx_begin_write ());
				// Set current time to trigger automatic rebroadcast and election
				nano::unchecked_info info (block, block->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown);
				result = node.block_processor.process_one (transaction, info);
			}
			switch (result.code)
			{
				case nano::process_result::progress:
				{
					response_l.put ("hash", hash.to_string ());
					break;
				}
				case nano::process_result::gap_previous:
				{
					ec = nano::error_process::gap_previous;
					break;
				}
				case nano::process_result::gap_source:
				{
					ec = nano::error_process::gap_source;
					break;
				}
				case nano::process_result::old:
				{
					ec = nano::error_process::old;
					break;
				}
				case nano::process_result::bad_signature:
				{
					ec = nano::error_process::bad_signature;
					break;
				}
				case nano::process_result::negative_spend:
				{
					// TODO once we get RPC versioning, this should be changed to "negative spend"
					ec = nano::error_process::negative_spend;
					break;
				}
				case nano::process_result::balance_mismatch:
				{
					ec = nano::error_process::balance_mismatch;
					break;
				}
				case nano::process_result::unreceivable:
				{
					ec = nano::error_process::unreceivable;
					break;
				}
				case nano::process_result::block_position:
				{
					ec = nano::error_process::block_position;
					break;
				}
				case nano::process_result::fork:
				{
					const bool force = request.get<bool> ("force", false);
					if (force && rpc.config.enable_control)
					{
						node.active.erase (*block);
						node.block_processor.force (block);
						response_l.put ("hash", hash.to_string ());
					}
					else
					{
						ec = nano::error_process::fork;
					}
					break;
				}
				default:
				{
					ec = nano::error_process::other;
					break;
				}
			}
		}
		else
		{
			ec = nano::error_blocks::work_low;
		}
	}
	response_errors ();
}

void nano::rpc_handler::receive ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	auto hash (hash_impl ("block"));
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		wallet_locked_impl (transaction, wallet);
		wallet_account_impl (transaction, wallet, account);
		if (!ec)
		{
			auto block_transaction (node.store.tx_begin_read ());
			auto block (node.store.block_get (block_transaction, hash));
			if (block != nullptr)
			{
				if (node.store.pending_exists (block_transaction, nano::pending_key (account, hash)))
				{
					auto work (work_optional_impl ());
					if (!ec && work)
					{
						nano::account_info info;
						nano::uint256_union head;
						if (!node.store.account_get (block_transaction, account, info))
						{
							head = info.head;
						}
						else
						{
							head = account;
						}
						if (nano::work_validate (head, work))
						{
							ec = nano::error_common::invalid_work;
						}
					}
					if (!ec)
					{
						bool generate_work (work == 0); // Disable work generation if "work" option is provided
						auto response_a (response);
						// clang-format off
						wallet->receive_async (std::move (block), account, nano::genesis_amount, [response_a](std::shared_ptr<nano::block> block_a) {
							nano::uint256_union hash_a (0);
							if (block_a != nullptr)
							{
								hash_a = block_a->hash ();
							}
							boost::property_tree::ptree response_l;
							response_l.put ("block", hash_a.to_string ());
							response_a (response_l);
						},
						work, generate_work);
						// clang-format on
					}
				}
				else
				{
					ec = nano::error_process::unreceivable;
				}
			}
			else
			{
				ec = nano::error_blocks::not_found;
			}
		}
	}
	// Because of receive_async
	if (ec)
	{
		response_errors ();
	}
}

void nano::rpc_handler::receive_minimum ()
{
	rpc_control_impl ();
	if (!ec)
	{
		response_l.put ("amount", node.config.receive_minimum.to_string_dec ());
	}
	response_errors ();
}

void nano::rpc_handler::receive_minimum_set ()
{
	rpc_control_impl ();
	auto amount (amount_impl ());
	if (!ec)
	{
		node.config.receive_minimum = amount;
		response_l.put ("success", "");
	}
	response_errors ();
}

void nano::rpc_handler::representatives ()
{
	auto count (count_optional_impl ());
	if (!ec)
	{
		const bool sorting = request.get<bool> ("sorting", false);
		boost::property_tree::ptree representatives;
		auto transaction (node.store.tx_begin_read ());
		if (!sorting) // Simple
		{
			for (auto i (node.store.representation_begin (transaction)), n (node.store.representation_end ()); i != n && representatives.size () < count; ++i)
			{
				nano::account account (i->first);
				auto amount (node.store.representation_get (transaction, account));
				representatives.put (account.to_account (), amount.convert_to<std::string> ());
			}
		}
		else // Sorting
		{
			std::vector<std::pair<nano::uint128_union, std::string>> representation;
			for (auto i (node.store.representation_begin (transaction)), n (node.store.representation_end ()); i != n; ++i)
			{
				nano::account account (i->first);
				auto amount (node.store.representation_get (transaction, account));
				representation.push_back (std::make_pair (amount, account.to_account ()));
			}
			std::sort (representation.begin (), representation.end ());
			std::reverse (representation.begin (), representation.end ());
			for (auto i (representation.begin ()), n (representation.end ()); i != n && representatives.size () < count; ++i)
			{
				representatives.put (i->second, (i->first).number ().convert_to<std::string> ());
			}
		}
		response_l.add_child ("representatives", representatives);
	}
	response_errors ();
}

void nano::rpc_handler::representatives_online ()
{
	const auto accounts_node = request.get_child_optional ("accounts");
	const bool weight = request.get<bool> ("weight", false);
	std::vector<nano::public_key> accounts_to_filter;
	if (accounts_node.is_initialized ())
	{
		for (auto & a : (*accounts_node))
		{
			nano::public_key account;
			auto error (account.decode_account (a.second.get<std::string> ("")));
			if (!error)
			{
				accounts_to_filter.push_back (account);
			}
			else
			{
				ec = nano::error_common::bad_account_number;
				break;
			}
		}
	}
	if (!ec)
	{
		boost::property_tree::ptree representatives;
		auto transaction (node.store.tx_begin_read ());
		auto reps (node.online_reps.list ());
		for (auto & i : reps)
		{
			if (accounts_node.is_initialized ())
			{
				if (accounts_to_filter.empty ())
				{
					break;
				}
				auto found_acc = std::find (accounts_to_filter.begin (), accounts_to_filter.end (), i);
				if (found_acc == accounts_to_filter.end ())
				{
					continue;
				}
				else
				{
					accounts_to_filter.erase (found_acc);
				}
			}
			if (weight)
			{
				boost::property_tree::ptree weight_node;
				auto account_weight (node.ledger.weight (transaction, i));
				weight_node.put ("weight", account_weight.convert_to<std::string> ());
				representatives.add_child (i.to_account (), weight_node);
			}
			else
			{
				boost::property_tree::ptree entry;
				entry.put ("", i.to_account ());
				representatives.push_back (std::make_pair ("", entry));
			}
		}
		response_l.add_child ("representatives", representatives);
	}
	response_errors ();
}

void nano::rpc_handler::republish ()
{
	auto count (count_optional_impl (1024U));
	uint64_t sources (0);
	uint64_t destinations (0);
	boost::optional<std::string> sources_text (request.get_optional<std::string> ("sources"));
	if (!ec && sources_text.is_initialized ())
	{
		if (decode_unsigned (sources_text.get (), sources))
		{
			ec = nano::error_rpc::invalid_sources;
		}
	}
	boost::optional<std::string> destinations_text (request.get_optional<std::string> ("destinations"));
	if (!ec && destinations_text.is_initialized ())
	{
		if (decode_unsigned (destinations_text.get (), destinations))
		{
			ec = nano::error_rpc::invalid_destinations;
		}
	}
	auto hash (hash_impl ());
	if (!ec)
	{
		boost::property_tree::ptree blocks;
		auto transaction (node.store.tx_begin_read ());
		auto block (node.store.block_get (transaction, hash));
		if (block != nullptr)
		{
			std::deque<std::shared_ptr<nano::block>> republish_bundle;
			for (auto i (0); !hash.is_zero () && i < count; ++i)
			{
				block = node.store.block_get (transaction, hash);
				if (sources != 0) // Republish source chain
				{
					nano::block_hash source (node.ledger.block_source (transaction, *block));
					auto block_a (node.store.block_get (transaction, source));
					std::vector<nano::block_hash> hashes;
					while (block_a != nullptr && hashes.size () < sources)
					{
						hashes.push_back (source);
						source = block_a->previous ();
						block_a = node.store.block_get (transaction, source);
					}
					std::reverse (hashes.begin (), hashes.end ());
					for (auto & hash_l : hashes)
					{
						block_a = node.store.block_get (transaction, hash_l);
						republish_bundle.push_back (std::move (block_a));
						boost::property_tree::ptree entry_l;
						entry_l.put ("", hash_l.to_string ());
						blocks.push_back (std::make_pair ("", entry_l));
					}
				}
				republish_bundle.push_back (std::move (block)); // Republish block
				boost::property_tree::ptree entry;
				entry.put ("", hash.to_string ());
				blocks.push_back (std::make_pair ("", entry));
				if (destinations != 0) // Republish destination chain
				{
					auto block_b (node.store.block_get (transaction, hash));
					auto destination (node.ledger.block_destination (transaction, *block_b));
					if (!destination.is_zero ())
					{
						if (!node.store.pending_exists (transaction, nano::pending_key (destination, hash)))
						{
							nano::block_hash previous (node.ledger.latest (transaction, destination));
							auto block_d (node.store.block_get (transaction, previous));
							nano::block_hash source;
							std::vector<nano::block_hash> hashes;
							while (block_d != nullptr && hash != source)
							{
								hashes.push_back (previous);
								source = node.ledger.block_source (transaction, *block_d);
								previous = block_d->previous ();
								block_d = node.store.block_get (transaction, previous);
							}
							std::reverse (hashes.begin (), hashes.end ());
							if (hashes.size () > destinations)
							{
								hashes.resize (destinations);
							}
							for (auto & hash_l : hashes)
							{
								block_d = node.store.block_get (transaction, hash_l);
								republish_bundle.push_back (std::move (block_d));
								boost::property_tree::ptree entry_l;
								entry_l.put ("", hash_l.to_string ());
								blocks.push_back (std::make_pair ("", entry_l));
							}
						}
					}
				}
				hash = node.store.block_successor (transaction, hash);
			}
			node.network.republish_block_batch (republish_bundle, 25);
			response_l.put ("success", ""); // obsolete
			response_l.add_child ("blocks", blocks);
		}
		else
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::search_pending ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto error (wallet->search_pending ());
		response_l.put ("started", !error);
	}
	response_errors ();
}

void nano::rpc_handler::search_pending_all ()
{
	rpc_control_impl ();
	if (!ec)
	{
		node.wallets.search_pending_all ();
		response_l.put ("success", "");
	}
	response_errors ();
}

void nano::rpc_handler::send ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto amount (amount_impl ());
	// Sending 0 amount is invalid with state blocks
	if (!ec && amount.is_zero ())
	{
		ec = nano::error_common::invalid_amount;
	}
	if (!ec)
	{
		std::string source_text (request.get<std::string> ("source"));
		nano::account source;
		if (!source.decode_account (source_text))
		{
			std::string destination_text (request.get<std::string> ("destination"));
			nano::account destination;
			if (!destination.decode_account (destination_text))
			{
				auto work (work_optional_impl ());
				nano::uint128_t balance (0);
				if (!ec)
				{
					auto transaction (node.wallets.tx_begin_read ());
					auto block_transaction (node.store.tx_begin_read ());
					if (wallet->store.valid_password (transaction))
					{
						if (wallet->store.find (transaction, source) != wallet->store.end ())
						{
							nano::account_info info;
							if (!node.store.account_get (block_transaction, source, info))
							{
								balance = (info.balance).number ();
							}
							else
							{
								ec = nano::error_common::account_not_found;
							}
							if (!ec && work)
							{
								if (nano::work_validate (info.head, work))
								{
									ec = nano::error_common::invalid_work;
								}
							}
						}
						else
						{
							ec = nano::error_common::account_not_found_wallet;
						}
					}
					else
					{
						ec = nano::error_common::wallet_locked;
					}
				}
				if (!ec)
				{
					bool generate_work (work == 0); // Disable work generation if "work" option is provided
					boost::optional<std::string> send_id (request.get_optional<std::string> ("id"));
					auto rpc_l (shared_from_this ());
					auto response_a (response);
					// clang-format off
					wallet->send_async (source, destination, amount.number (), [balance, amount, response_a](std::shared_ptr<nano::block> block_a) {
						if (block_a != nullptr)
						{
							nano::uint256_union hash (block_a->hash ());
							boost::property_tree::ptree response_l;
							response_l.put ("block", hash.to_string ());
							response_a (response_l);
						}
						else
						{
							if (balance >= amount.number ())
							{
								error_response (response_a, "Error generating block");
							}
							else
							{
								std::error_code ec (nano::error_common::insufficient_balance);
								error_response (response_a, ec.message ());
							}
						}
					},
					work, generate_work, send_id);
					// clang-format on
				}
			}
			else
			{
				ec = nano::error_rpc::bad_destination;
			}
		}
		else
		{
			ec = nano::error_rpc::bad_source;
		}
	}
	// Because of send_async
	if (ec)
	{
		response_errors ();
	}
}

void nano::rpc_handler::sign ()
{
	// Retrieving hash
	nano::block_hash hash (0);
	boost::optional<std::string> hash_text (request.get_optional<std::string> ("hash"));
	if (hash_text.is_initialized ())
	{
		hash = hash_impl ();
	}
	// Retrieving block
	std::shared_ptr<nano::block> block;
	boost::optional<std::string> block_text (request.get_optional<std::string> ("block"));
	if (!ec && block_text.is_initialized ())
	{
		block = block_impl (true);
		if (block != nullptr)
		{
			hash = block->hash ();
		}
	}
	// Hash or block are not initialized
	if (!ec && hash.is_zero ())
	{
		ec = nano::error_blocks::invalid_block;
	}
	// Hash is initialized without config permission
	else if (!ec && !hash.is_zero () && block == nullptr && !rpc.config.enable_sign_hash)
	{
		ec = nano::error_rpc::sign_hash_disabled;
	}
	if (!ec)
	{
		nano::raw_key prv;
		prv.data.clear ();
		// Retrieving private key from request
		boost::optional<std::string> key_text (request.get_optional<std::string> ("key"));
		if (key_text.is_initialized ())
		{
			if (prv.data.decode_hex (key_text.get ()))
			{
				ec = nano::error_common::bad_private_key;
			}
		}
		else
		{
			// Retrieving private key from wallet
			boost::optional<std::string> account_text (request.get_optional<std::string> ("account"));
			boost::optional<std::string> wallet_text (request.get_optional<std::string> ("wallet"));
			if (wallet_text.is_initialized () && account_text.is_initialized ())
			{
				auto account (account_impl ());
				auto wallet (wallet_impl ());
				if (!ec)
				{
					auto transaction (node.wallets.tx_begin_read ());
					wallet_locked_impl (transaction, wallet);
					wallet_account_impl (transaction, wallet, account);
					if (!ec)
					{
						wallet->store.fetch (transaction, account, prv);
					}
				}
			}
		}
		// Signing
		if (prv.data != 0)
		{
			nano::public_key pub (nano::pub_key (prv.data));
			nano::signature signature (nano::sign_message (prv, pub, hash));
			response_l.put ("signature", signature.to_string ());
			if (block != nullptr)
			{
				block->signature_set (signature);
				std::string contents;
				block->serialize_json (contents);
				response_l.put ("block", contents);
			}
		}
		else
		{
			ec = nano::error_rpc::block_create_key_required;
		}
	}
	response_errors ();
}

void nano::rpc_handler::stats ()
{
	auto sink = node.stats.log_sink_json ();
	std::string type (request.get<std::string> ("type", ""));
	bool use_sink = false;
	if (type == "counters")
	{
		node.stats.log_counters (*sink);
		use_sink = true;
	}
	else if (type == "objects")
	{
		rpc_control_impl ();
		if (!ec)
		{
			construct_json (collect_seq_con_info (node, "node").get (), response_l);
		}
	}
	else if (type == "samples")
	{
		node.stats.log_samples (*sink);
		use_sink = true;
	}
	else
	{
		ec = nano::error_rpc::invalid_missing_type;
	}
	if (!ec && use_sink)
	{
		auto stat_tree_l (*static_cast<boost::property_tree::ptree *> (sink->to_object ()));
		stat_tree_l.put ("stat_duration_seconds", node.stats.last_reset ().count ());
		response (stat_tree_l);
	}
	else
	{
		response_errors ();
	}
}

void nano::rpc_handler::stats_clear ()
{
	node.stats.clear ();
	response_l.put ("success", "");
	response (response_l);
}

void nano::rpc_handler::stop ()
{
	rpc_control_impl ();
	if (!ec)
	{
		response_l.put ("success", "");
	}
	response_errors ();
	if (!ec)
	{
		rpc.stop ();
		node.stop ();
	}
}

void nano::rpc_handler::unchecked ()
{
	auto count (count_optional_impl ());
	if (!ec)
	{
		boost::property_tree::ptree unchecked;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.unchecked_begin (transaction)), n (node.store.unchecked_end ()); i != n && unchecked.size () < count; ++i)
		{
			nano::unchecked_info info (i->second);
			std::string contents;
			info.block->serialize_json (contents);
			unchecked.put (info.block->hash ().to_string (), contents);
		}
		response_l.add_child ("blocks", unchecked);
	}
	response_errors ();
}

void nano::rpc_handler::unchecked_clear ()
{
	rpc_control_impl ();
	if (!ec)
	{
		auto transaction (node.store.tx_begin_write ());
		node.store.unchecked_clear (transaction);
		response_l.put ("success", "");
	}
	response_errors ();
}

void nano::rpc_handler::unchecked_get ()
{
	auto hash (hash_impl ());
	if (!ec)
	{
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.unchecked_begin (transaction)), n (node.store.unchecked_end ()); i != n; ++i)
		{
			nano::unchecked_key key (i->first);
			if (key.hash == hash)
			{
				nano::unchecked_info info (i->second);
				response_l.put ("modified_timestamp", std::to_string (info.modified));
				std::string contents;
				info.block->serialize_json (contents);
				response_l.put ("contents", contents);
				break;
			}
		}
		if (response_l.empty ())
		{
			ec = nano::error_blocks::not_found;
		}
	}
	response_errors ();
}

void nano::rpc_handler::unchecked_keys ()
{
	auto count (count_optional_impl ());
	nano::uint256_union key (0);
	boost::optional<std::string> hash_text (request.get_optional<std::string> ("key"));
	if (!ec && hash_text.is_initialized ())
	{
		if (key.decode_hex (hash_text.get ()))
		{
			ec = nano::error_rpc::bad_key;
		}
	}
	if (!ec)
	{
		boost::property_tree::ptree unchecked;
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.unchecked_begin (transaction, nano::unchecked_key (key, 0))), n (node.store.unchecked_end ()); i != n && unchecked.size () < count; ++i)
		{
			boost::property_tree::ptree entry;
			nano::unchecked_info info (i->second);
			std::string contents;
			info.block->serialize_json (contents);
			entry.put ("key", nano::block_hash (i->first.key ()).to_string ());
			entry.put ("hash", info.block->hash ().to_string ());
			entry.put ("modified_timestamp", std::to_string (info.modified));
			entry.put ("contents", contents);
			unchecked.push_back (std::make_pair ("", entry));
		}
		response_l.add_child ("unchecked", unchecked);
	}
	response_errors ();
}

void nano::rpc_handler::uptime ()
{
	response_l.put ("seconds", std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - node.startup_time).count ());
	response_errors ();
}

void nano::rpc_handler::version ()
{
	response_l.put ("rpc_version", "1");
	response_l.put ("store_version", std::to_string (node.store_version ()));
	response_l.put ("protocol_version", std::to_string (nano::protocol_version));
	if (NANO_VERSION_PATCH == 0)
	{
		response_l.put ("node_vendor", boost::str (boost::format ("Flairrcoin %1%") % NANO_MAJOR_MINOR_VERSION));
	}
	else
	{
		response_l.put ("node_vendor", boost::str (boost::format ("Flairrcoin %1%") % NANO_MAJOR_MINOR_RC_VERSION));
	}
	response_errors ();
}

void nano::rpc_handler::validate_account_number ()
{
	std::string account_text (request.get<std::string> ("account"));
	nano::uint256_union account;
	auto error (account.decode_account (account_text));
	response_l.put ("valid", error ? "0" : "1");
	response_errors ();
}

void nano::rpc_handler::wallet_add ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string key_text (request.get<std::string> ("key"));
		nano::raw_key key;
		if (!key.data.decode_hex (key_text))
		{
			const bool generate_work = request.get<bool> ("work", true);
			auto pub (wallet->insert_adhoc (key, generate_work));
			if (!pub.is_zero ())
			{
				response_l.put ("account", pub.to_account ());
			}
			else
			{
				ec = nano::error_common::wallet_locked;
			}
		}
		else
		{
			ec = nano::error_common::bad_private_key;
		}
	}
	response_errors ();
}

void nano::rpc_handler::wallet_add_watch ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_write ());
		if (wallet->store.valid_password (transaction))
		{
			for (auto & accounts : request.get_child ("accounts"))
			{
				auto account (account_impl (accounts.second.data ()));
				if (!ec)
				{
					wallet->insert_watch (transaction, account);
				}
			}
			response_l.put ("success", "");
		}
		else
		{
			ec = nano::error_common::wallet_locked;
		}
	}
	response_errors ();
}

void nano::rpc_handler::wallet_info ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		nano::uint128_t balance (0);
		nano::uint128_t pending (0);
		uint64_t count (0);
		uint64_t deterministic_count (0);
		uint64_t adhoc_count (0);
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			balance = balance + node.ledger.account_balance (block_transaction, account);
			pending = pending + node.ledger.account_pending (block_transaction, account);
			nano::key_type key_type (wallet->store.key_type (i->second));
			if (key_type == nano::key_type::deterministic)
			{
				deterministic_count++;
			}
			else if (key_type == nano::key_type::adhoc)
			{
				adhoc_count++;
			}
			count++;
		}
		uint32_t deterministic_index (wallet->store.deterministic_index_get (transaction));
		response_l.put ("balance", balance.convert_to<std::string> ());
		response_l.put ("pending", pending.convert_to<std::string> ());
		response_l.put ("accounts_count", std::to_string (count));
		response_l.put ("deterministic_count", std::to_string (deterministic_count));
		response_l.put ("adhoc_count", std::to_string (adhoc_count));
		response_l.put ("deterministic_index", std::to_string (deterministic_index));
	}
	response_errors ();
}

void nano::rpc_handler::wallet_balances ()
{
	auto wallet (wallet_impl ());
	auto threshold (threshold_optional_impl ());
	if (!ec)
	{
		boost::property_tree::ptree balances;
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			nano::uint128_t balance = node.ledger.account_balance (block_transaction, account);
			if (balance >= threshold.number ())
			{
				boost::property_tree::ptree entry;
				nano::uint128_t pending = node.ledger.account_pending (block_transaction, account);
				entry.put ("balance", balance.convert_to<std::string> ());
				entry.put ("pending", pending.convert_to<std::string> ());
				balances.push_back (std::make_pair (account.to_account (), entry));
			}
		}
		response_l.add_child ("balances", balances);
	}
	response_errors ();
}

void nano::rpc_handler::wallet_change_seed ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string seed_text (request.get<std::string> ("seed"));
		nano::raw_key seed;
		if (!seed.data.decode_hex (seed_text))
		{
			auto count (static_cast<uint32_t> (count_optional_impl (0)));
			auto transaction (node.wallets.tx_begin_write ());
			if (wallet->store.valid_password (transaction))
			{
				wallet->change_seed (transaction, seed, count);
				response_l.put ("success", "");
			}
			else
			{
				ec = nano::error_common::wallet_locked;
			}
		}
		else
		{
			ec = nano::error_common::bad_seed;
		}
	}
	response_errors ();
}

void nano::rpc_handler::wallet_contains ()
{
	auto account (account_impl ());
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		auto exists (wallet->store.find (transaction, account) != wallet->store.end ());
		response_l.put ("exists", exists ? "1" : "0");
	}
	response_errors ();
}

void nano::rpc_handler::wallet_create ()
{
	rpc_control_impl ();
	if (!ec)
	{
		nano::raw_key seed;
		auto seed_text (request.get_optional<std::string> ("seed"));
		if (seed_text.is_initialized () && seed.data.decode_hex (seed_text.get ()))
		{
			ec = nano::error_common::bad_seed;
		}
		if (!ec)
		{
			nano::keypair wallet_id;
			auto wallet (node.wallets.create (wallet_id.pub));
			auto existing (node.wallets.items.find (wallet_id.pub));
			if (existing != node.wallets.items.end ())
			{
				response_l.put ("wallet", wallet_id.pub.to_string ());
			}
			else
			{
				ec = nano::error_common::wallet_lmdb_max_dbs;
			}
			if (!ec && seed_text.is_initialized ())
			{
				auto transaction (node.wallets.tx_begin_write ());
				nano::public_key account (wallet->change_seed (transaction, seed));
				response_l.put ("account", account.to_account ());
			}
		}
	}
	response_errors ();
}

void nano::rpc_handler::wallet_destroy ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string wallet_text (request.get<std::string> ("wallet"));
		nano::uint256_union wallet;
		if (!wallet.decode_hex (wallet_text))
		{
			auto existing (node.wallets.items.find (wallet));
			if (existing != node.wallets.items.end ())
			{
				node.wallets.destroy (wallet);
				bool destroyed (node.wallets.items.find (wallet) == node.wallets.items.end ());
				response_l.put ("destroyed", destroyed ? "1" : "0");
			}
			else
			{
				ec = nano::error_common::wallet_not_found;
			}
		}
		else
		{
			ec = nano::error_common::bad_wallet_number;
		}
	}
	response_errors ();
}

void nano::rpc_handler::wallet_export ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		std::string json;
		wallet->store.serialize_json (transaction, json);
		response_l.put ("json", json);
	}
	response_errors ();
}

void nano::rpc_handler::wallet_frontiers ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree frontiers;
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			auto latest (node.ledger.latest (block_transaction, account));
			if (!latest.is_zero ())
			{
				frontiers.put (account.to_account (), latest.to_string ());
			}
		}
		response_l.add_child ("frontiers", frontiers);
	}
	response_errors ();
}

void nano::rpc_handler::wallet_history ()
{
	uint64_t modified_since (1);
	boost::optional<std::string> modified_since_text (request.get_optional<std::string> ("modified_since"));
	if (modified_since_text.is_initialized ())
	{
		if (decode_unsigned (modified_since_text.get (), modified_since))
		{
			ec = nano::error_rpc::invalid_timestamp;
		}
	}
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::multimap<uint64_t, boost::property_tree::ptree, std::greater<uint64_t>> entries;
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			nano::account_info info;
			if (!node.store.account_get (block_transaction, account, info))
			{
				auto timestamp (info.modified);
				auto hash (info.head);
				while (timestamp >= modified_since && !hash.is_zero ())
				{
					nano::block_sideband sideband;
					auto block (node.store.block_get (block_transaction, hash, &sideband));
					timestamp = sideband.timestamp;
					if (block != nullptr && timestamp >= modified_since)
					{
						boost::property_tree::ptree entry;
						history_visitor visitor (*this, false, block_transaction, entry, hash);
						block->visit (visitor);
						if (!entry.empty ())
						{
							entry.put ("block_account", account.to_account ());
							entry.put ("hash", hash.to_string ());
							entry.put ("local_timestamp", std::to_string (timestamp));
							entries.insert (std::make_pair (timestamp, entry));
						}
						hash = block->previous ();
					}
					else
					{
						hash.clear ();
					}
				}
			}
		}
		boost::property_tree::ptree history;
		for (auto i (entries.begin ()), n (entries.end ()); i != n; ++i)
		{
			history.push_back (std::make_pair ("", i->second));
		}
		response_l.add_child ("history", history);
	}
	response_errors ();
}

void nano::rpc_handler::wallet_key_valid ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		auto valid (wallet->store.valid_password (transaction));
		response_l.put ("valid", valid ? "1" : "0");
	}
	response_errors ();
}

void nano::rpc_handler::wallet_ledger ()
{
	const bool representative = request.get<bool> ("representative", false);
	const bool weight = request.get<bool> ("weight", false);
	const bool pending = request.get<bool> ("pending", false);
	uint64_t modified_since (0);
	boost::optional<std::string> modified_since_text (request.get_optional<std::string> ("modified_since"));
	if (modified_since_text.is_initialized ())
	{
		modified_since = strtoul (modified_since_text.get ().c_str (), NULL, 10);
	}
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree accounts;
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			nano::account_info info;
			if (!node.store.account_get (block_transaction, account, info))
			{
				if (info.modified >= modified_since)
				{
					boost::property_tree::ptree entry;
					entry.put ("frontier", info.head.to_string ());
					entry.put ("open_block", info.open_block.to_string ());
					entry.put ("representative_block", info.rep_block.to_string ());
					std::string balance;
					nano::uint128_union (info.balance).encode_dec (balance);
					entry.put ("balance", balance);
					entry.put ("modified_timestamp", std::to_string (info.modified));
					entry.put ("block_count", std::to_string (info.block_count));
					if (representative)
					{
						auto block (node.store.block_get (block_transaction, info.rep_block));
						assert (block != nullptr);
						entry.put ("representative", block->representative ().to_account ());
					}
					if (weight)
					{
						auto account_weight (node.ledger.weight (block_transaction, account));
						entry.put ("weight", account_weight.convert_to<std::string> ());
					}
					if (pending)
					{
						auto account_pending (node.ledger.account_pending (block_transaction, account));
						entry.put ("pending", account_pending.convert_to<std::string> ());
					}
					accounts.push_back (std::make_pair (account.to_account (), entry));
				}
			}
		}
		response_l.add_child ("accounts", accounts);
	}
	response_errors ();
}

void nano::rpc_handler::wallet_lock ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		nano::raw_key empty;
		empty.data.clear ();
		wallet->store.password.value_set (empty);
		response_l.put ("locked", "1");
	}
	response_errors ();
}

void nano::rpc_handler::wallet_pending ()
{
	auto wallet (wallet_impl ());
	auto count (count_optional_impl ());
	auto threshold (threshold_optional_impl ());
	const bool source = request.get<bool> ("source", false);
	const bool min_version = request.get<bool> ("min_version", false);
	const bool include_active = request.get<bool> ("include_active", false);
	if (!ec)
	{
		boost::property_tree::ptree pending;
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			boost::property_tree::ptree peers_l;
			for (auto ii (node.store.pending_begin (block_transaction, nano::pending_key (account, 0))); nano::pending_key (ii->first).account == account && peers_l.size () < count; ++ii)
			{
				nano::pending_key key (ii->first);
				std::shared_ptr<nano::block> block (include_active ? nullptr : node.store.block_get (block_transaction, key.hash));
				if (include_active || (block && !node.active.active (*block)))
				{
					if (threshold.is_zero () && !source)
					{
						boost::property_tree::ptree entry;
						entry.put ("", key.hash.to_string ());
						peers_l.push_back (std::make_pair ("", entry));
					}
					else
					{
						nano::pending_info info (ii->second);
						if (info.amount.number () >= threshold.number ())
						{
							if (source || min_version)
							{
								boost::property_tree::ptree pending_tree;
								pending_tree.put ("amount", info.amount.number ().convert_to<std::string> ());
								if (source)
								{
									pending_tree.put ("source", info.source.to_account ());
								}
								if (min_version)
								{
									pending_tree.put ("min_version", info.epoch == nano::epoch::epoch_1 ? "1" : "0");
								}
								peers_l.add_child (key.hash.to_string (), pending_tree);
							}
							else
							{
								peers_l.put (key.hash.to_string (), info.amount.number ().convert_to<std::string> ());
							}
						}
					}
				}
			}
			if (!peers_l.empty ())
			{
				pending.add_child (account.to_account (), peers_l);
			}
		}
		response_l.add_child ("blocks", pending);
	}
	response_errors ();
}

void nano::rpc_handler::wallet_representative ()
{
	auto wallet (wallet_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		response_l.put ("representative", wallet->store.representative (transaction).to_account ());
	}
	response_errors ();
}

void nano::rpc_handler::wallet_representative_set ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		std::string representative_text (request.get<std::string> ("representative"));
		nano::account representative;
		if (!representative.decode_account (representative_text))
		{
			bool update_existing_accounts (request.get<bool> ("update_existing_accounts", false));
			{
				auto transaction (node.wallets.tx_begin_write ());
				if (wallet->store.valid_password (transaction) || !update_existing_accounts)
				{
					wallet->store.representative_set (transaction, representative);
					response_l.put ("set", "1");
				}
				else
				{
					ec = nano::error_common::wallet_locked;
				}
			}
			// Change representative for all wallet accounts
			if (!ec && update_existing_accounts)
			{
				std::vector<nano::account> accounts;
				{
					auto transaction (node.wallets.tx_begin_read ());
					auto block_transaction (node.store.tx_begin_read ());
					for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
					{
						nano::account account (i->first);
						nano::account_info info;
						if (!node.store.account_get (block_transaction, account, info))
						{
							auto block (node.store.block_get (block_transaction, info.rep_block));
							assert (block != nullptr);
							if (block->representative () != representative)
							{
								accounts.push_back (account);
							}
						}
					}
				}
				for (auto & account : accounts)
				{
					// clang-format off
					wallet->change_async (account, representative, [](std::shared_ptr<nano::block>) {}, 0, false);
					// clang-format on
				}
			}
		}
		else
		{
			ec = nano::error_rpc::bad_representative_number;
		}
	}
	response_errors ();
}

void nano::rpc_handler::wallet_republish ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto count (count_impl ());
	if (!ec)
	{
		boost::property_tree::ptree blocks;
		std::deque<std::shared_ptr<nano::block>> republish_bundle;
		auto transaction (node.wallets.tx_begin_read ());
		auto block_transaction (node.store.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			auto latest (node.ledger.latest (block_transaction, account));
			std::shared_ptr<nano::block> block;
			std::vector<nano::block_hash> hashes;
			while (!latest.is_zero () && hashes.size () < count)
			{
				hashes.push_back (latest);
				block = node.store.block_get (block_transaction, latest);
				latest = block->previous ();
			}
			std::reverse (hashes.begin (), hashes.end ());
			for (auto & hash : hashes)
			{
				block = node.store.block_get (block_transaction, hash);
				republish_bundle.push_back (std::move (block));
				boost::property_tree::ptree entry;
				entry.put ("", hash.to_string ());
				blocks.push_back (std::make_pair ("", entry));
			}
		}
		node.network.republish_block_batch (republish_bundle, 25);
		response_l.add_child ("blocks", blocks);
	}
	response_errors ();
}

void nano::rpc_handler::wallet_work_get ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	if (!ec)
	{
		boost::property_tree::ptree works;
		auto transaction (node.wallets.tx_begin_read ());
		for (auto i (wallet->store.begin (transaction)), n (wallet->store.end ()); i != n; ++i)
		{
			nano::account account (i->first);
			uint64_t work (0);
			auto error_work (wallet->store.work_get (transaction, account, work));
			(void)error_work;
			works.put (account.to_account (), nano::to_string_hex (work));
		}
		response_l.add_child ("works", works);
	}
	response_errors ();
}

void nano::rpc_handler::work_generate ()
{
	rpc_control_impl ();
	auto hash (hash_impl ());
	if (!ec)
	{
		bool use_peers (request.get_optional<bool> ("use_peers") == true);
		auto rpc_l (shared_from_this ());
		auto callback = [rpc_l](boost::optional<uint64_t> const & work_a) {
			if (work_a)
			{
				boost::property_tree::ptree response_l;
				response_l.put ("work", nano::to_string_hex (work_a.value ()));
				rpc_l->response (response_l);
			}
			else
			{
				error_response (rpc_l->response, "Cancelled");
			}
		};
		if (!use_peers)
		{
			node.work.generate (hash, callback);
		}
		else
		{
			node.work_generate (hash, callback);
		}
	}
	// Because of callback
	if (ec)
	{
		response_errors ();
	}
}

void nano::rpc_handler::work_cancel ()
{
	rpc_control_impl ();
	auto hash (hash_impl ());
	if (!ec)
	{
		node.work.cancel (hash);
	}
	response_errors ();
}

void nano::rpc_handler::work_get ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_read ());
		wallet_account_impl (transaction, wallet, account);
		if (!ec)
		{
			uint64_t work (0);
			auto error_work (wallet->store.work_get (transaction, account, work));
			(void)error_work;
			response_l.put ("work", nano::to_string_hex (work));
		}
	}
	response_errors ();
}

void nano::rpc_handler::work_set ()
{
	rpc_control_impl ();
	auto wallet (wallet_impl ());
	auto account (account_impl ());
	auto work (work_optional_impl ());
	if (!ec)
	{
		auto transaction (node.wallets.tx_begin_write ());
		wallet_account_impl (transaction, wallet, account);
		if (!ec)
		{
			wallet->store.work_put (transaction, account, work);
			response_l.put ("success", "");
		}
	}
	response_errors ();
}

void nano::rpc_handler::work_validate ()
{
	auto hash (hash_impl ());
	auto work (work_optional_impl ());
	if (!ec)
	{
		auto validate (nano::work_validate (hash, work));
		response_l.put ("valid", validate ? "0" : "1");
	}
	response_errors ();
}

void nano::rpc_handler::work_peer_add ()
{
	rpc_control_impl ();
	if (!ec)
	{
		std::string address_text = request.get<std::string> ("address");
		std::string port_text = request.get<std::string> ("port");
		uint16_t port;
		if (!nano::parse_port (port_text, port))
		{
			node.config.work_peers.push_back (std::make_pair (address_text, port));
			response_l.put ("success", "");
		}
		else
		{
			ec = nano::error_common::invalid_port;
		}
	}
	response_errors ();
}

void nano::rpc_handler::work_peers ()
{
	rpc_control_impl ();
	if (!ec)
	{
		boost::property_tree::ptree work_peers_l;
		for (auto i (node.config.work_peers.begin ()), n (node.config.work_peers.end ()); i != n; ++i)
		{
			boost::property_tree::ptree entry;
			entry.put ("", boost::str (boost::format ("%1%:%2%") % i->first % i->second));
			work_peers_l.push_back (std::make_pair ("", entry));
		}
		response_l.add_child ("work_peers", work_peers_l);
	}
	response_errors ();
}

void nano::rpc_handler::work_peers_clear ()
{
	rpc_control_impl ();
	if (!ec)
	{
		node.config.work_peers.clear ();
		response_l.put ("success", "");
	}
	response_errors ();
}

nano::rpc_connection::rpc_connection (nano::node & node_a, nano::rpc & rpc_a) :
node (node_a.shared ()),
rpc (rpc_a),
socket (node_a.io_ctx)
{
	responded.clear ();
}

void nano::rpc_connection::parse_connection ()
{
	read ();
}

void nano::rpc_connection::prepare_head (unsigned version, boost::beast::http::status status)
{
	res.version (version);
	res.result (status);
	res.set (boost::beast::http::field::allow, "POST, OPTIONS");
	res.set (boost::beast::http::field::content_type, "application/json");
	res.set (boost::beast::http::field::access_control_allow_origin, "*");
	res.set (boost::beast::http::field::access_control_allow_methods, "POST, OPTIONS");
	res.set (boost::beast::http::field::access_control_allow_headers, "Accept, Accept-Language, Content-Language, Content-Type");
	res.set (boost::beast::http::field::connection, "close");
}

void nano::rpc_connection::write_result (std::string body, unsigned version, boost::beast::http::status status)
{
	if (!responded.test_and_set ())
	{
		prepare_head (version, status);
		res.body () = body;
		res.prepare_payload ();
	}
	else
	{
		assert (false && "RPC already responded and should only respond once");
		// Guards `res' from being clobbered while async_write is being serviced
	}
}

void nano::rpc_connection::read ()
{
	auto this_l (shared_from_this ());
	boost::beast::http::async_read (socket, buffer, request, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
		if (!ec)
		{
			this_l->node->background ([this_l]() {
				auto start (std::chrono::steady_clock::now ());
				auto version (this_l->request.version ());
				std::string request_id (boost::str (boost::format ("%1%") % boost::io::group (std::hex, std::showbase, reinterpret_cast<uintptr_t> (this_l.get ()))));
				auto response_handler ([this_l, version, start, request_id](boost::property_tree::ptree const & tree_a) {
					std::stringstream ostream;
					boost::property_tree::write_json (ostream, tree_a);
					ostream.flush ();
					auto body (ostream.str ());
					this_l->write_result (body, version);
					boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
					});

					if (this_l->node->config.logging.log_rpc ())
					{
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("RPC request %2% completed in: %1% microseconds") % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () % request_id);
					}
				});
				auto method = this_l->request.method ();
				switch (method)
				{
					case boost::beast::http::verb::post:
					{
						auto handler (std::make_shared<nano::rpc_handler> (*this_l->node, this_l->rpc, this_l->request.body (), request_id, response_handler));
						handler->process_request ();
						break;
					}
					case boost::beast::http::verb::options:
					{
						this_l->prepare_head (version);
						this_l->res.prepare_payload ();
						boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
						});
						break;
					}
					default:
					{
						error_response (response_handler, "Can only POST requests");
						break;
					}
				}
			});
		}
		else
		{
			BOOST_LOG (this_l->node->log) << "RPC read error: " << ec.message ();
		}
	});
}

namespace
{
std::string filter_request (boost::property_tree::ptree tree_a)
{
	// Replace password
	boost::optional<std::string> password_text (tree_a.get_optional<std::string> ("password"));
	if (password_text.is_initialized ())
	{
		tree_a.put ("password", "password");
	}
	// Save first 2 symbols of wallet, key, seed
	boost::optional<std::string> wallet_text (tree_a.get_optional<std::string> ("wallet"));
	if (wallet_text.is_initialized () && wallet_text.get ().length () > 2)
	{
		tree_a.put ("wallet", wallet_text.get ().replace (wallet_text.get ().begin () + 2, wallet_text.get ().end (), wallet_text.get ().length () - 2, 'X'));
	}
	boost::optional<std::string> key_text (tree_a.get_optional<std::string> ("key"));
	if (key_text.is_initialized () && key_text.get ().length () > 2)
	{
		tree_a.put ("key", key_text.get ().replace (key_text.get ().begin () + 2, key_text.get ().end (), key_text.get ().length () - 2, 'X'));
	}
	boost::optional<std::string> seed_text (tree_a.get_optional<std::string> ("seed"));
	if (seed_text.is_initialized () && seed_text.get ().length () > 2)
	{
		tree_a.put ("seed", seed_text.get ().replace (seed_text.get ().begin () + 2, seed_text.get ().end (), seed_text.get ().length () - 2, 'X'));
	}
	std::string result;
	std::stringstream stream;
	boost::property_tree::write_json (stream, tree_a, false);
	result = stream.str ();
	// removing std::endl
	if (result.length () > 1)
	{
		result.pop_back ();
	}
	return result;
}
}

void nano::rpc_handler::process_request ()
{
	try
	{
		auto max_depth_exceeded (false);
		auto max_depth_possible (0);
		for (auto ch : body)
		{
			if (ch == '[' || ch == '{')
			{
				if (max_depth_possible >= rpc.config.max_json_depth)
				{
					max_depth_exceeded = true;
					break;
				}
				++max_depth_possible;
			}
		}
		if (max_depth_exceeded)
		{
			error_response (response, "Max JSON depth exceeded");
		}
		else
		{
			std::stringstream istream (body);
			boost::property_tree::read_json (istream, request);
			std::string action (request.get<std::string> ("action"));
			if (node.config.logging.log_rpc ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("%1% ") % request_id) << filter_request (request);
			}
			if (action == "account_balance")
			{
				account_balance ();
			}
			else if (action == "account_block_count")
			{
				account_block_count ();
			}
			else if (action == "account_count")
			{
				account_count ();
			}
			else if (action == "account_create")
			{
				account_create ();
			}
			else if (action == "account_get")
			{
				account_get ();
			}
			else if (action == "account_history")
			{
				account_history ();
			}
			else if (action == "account_info")
			{
				account_info ();
			}
			else if (action == "account_key")
			{
				account_key ();
			}
			else if (action == "account_list")
			{
				account_list ();
			}
			else if (action == "account_move")
			{
				account_move ();
			}
			else if (action == "account_remove")
			{
				account_remove ();
			}
			else if (action == "account_representative")
			{
				account_representative ();
			}
			else if (action == "account_representative_set")
			{
				account_representative_set ();
			}
			else if (action == "account_weight")
			{
				account_weight ();
			}
			else if (action == "accounts_balances")
			{
				accounts_balances ();
			}
			else if (action == "accounts_create")
			{
				accounts_create ();
			}
			else if (action == "accounts_frontiers")
			{
				accounts_frontiers ();
			}
			else if (action == "accounts_pending")
			{
				accounts_pending ();
			}
			else if (action == "available_supply")
			{
				available_supply ();
			}
			else if (action == "block")
			{
				block_info ();
			}
			else if (action == "block_info")
			{
				block_info ();
			}
			else if (action == "block_confirm")
			{
				block_confirm ();
			}
			else if (action == "blocks")
			{
				blocks ();
			}
			else if (action == "blocks_info")
			{
				blocks_info ();
			}
			else if (action == "block_account")
			{
				block_account ();
			}
			else if (action == "block_count")
			{
				block_count ();
			}
			else if (action == "block_count_type")
			{
				block_count_type ();
			}
			else if (action == "block_create")
			{
				block_create ();
			}
			else if (action == "block_hash")
			{
				block_hash ();
			}
			else if (action == "successors")
			{
				chain (true);
			}
			else if (action == "bootstrap")
			{
				bootstrap ();
			}
			else if (action == "bootstrap_any")
			{
				bootstrap_any ();
			}
			else if (action == "bootstrap_lazy")
			{
				bootstrap_lazy ();
			}
			else if (action == "bootstrap_status")
			{
				bootstrap_status ();
			}
			else if (action == "chain")
			{
				chain ();
			}
			else if (action == "delegators")
			{
				delegators ();
			}
			else if (action == "delegators_count")
			{
				delegators_count ();
			}
			else if (action == "deterministic_key")
			{
				deterministic_key ();
			}
			else if (action == "confirmation_active")
			{
				confirmation_active ();
			}
			else if (action == "confirmation_history")
			{
				confirmation_history ();
			}
			else if (action == "confirmation_info")
			{
				confirmation_info ();
			}
			else if (action == "confirmation_quorum")
			{
				confirmation_quorum ();
			}
			else if (action == "frontiers")
			{
				frontiers ();
			}
			else if (action == "frontier_count")
			{
				account_count ();
			}
			else if (action == "history")
			{
				request.put ("head", request.get<std::string> ("hash"));
				account_history ();
			}
			else if (action == "keepalive")
			{
				keepalive ();
			}
			else if (action == "key_create")
			{
				key_create ();
			}
			else if (action == "key_expand")
			{
				key_expand ();
			}
			else if (action == "k_from_raw" || action == "kflr_from_raw")
			{
				mFLR_from_raw (nano::kFLR_ratio);
			}
			else if (action == "k_to_raw" || action == "kflr_to_raw")
			{
				mFLR_to_raw (nano::kFLR_ratio);
			}
			else if (action == "ledger")
			{
				ledger ();
			}
			else if (action == "m_from_raw" || action == "flr_from_raw")
			{
				mFLR_from_raw ();
			}
			else if (action == "m_to_raw" || action == "flr_to_raw")
			{
				mFLR_to_raw ();
			}
			else if (action == "node_id")
			{
				node_id ();
			}
			else if (action == "node_id_delete")
			{
				node_id_delete ();
			}
			else if (action == "password_change")
			{
				password_change ();
			}
			else if (action == "password_enter")
			{
				password_enter ();
			}
			else if (action == "password_valid")
			{
				password_valid ();
			}
			else if (action == "payment_begin")
			{
				payment_begin ();
			}
			else if (action == "payment_init")
			{
				payment_init ();
			}
			else if (action == "payment_end")
			{
				payment_end ();
			}
			else if (action == "payment_wait")
			{
				payment_wait ();
			}
			else if (action == "peers")
			{
				peers ();
			}
			else if (action == "pending")
			{
				pending ();
			}
			else if (action == "pending_exists")
			{
				pending_exists ();
			}
			else if (action == "process")
			{
				process ();
			}
			else if (action == "nano_from_raw" || action == "raw_from_raw")
			{
				mFLR_from_raw (nano::FLR_ratio);
			}
			else if (action == "nano_to_raw" || action == "raw_to_raw")
			{
				mFLR_to_raw (nano::FLR_ratio);
			}
			else if (action == "receive")
			{
				receive ();
			}
			else if (action == "receive_minimum")
			{
				receive_minimum ();
			}
			else if (action == "receive_minimum_set")
			{
				receive_minimum_set ();
			}
			else if (action == "representatives")
			{
				representatives ();
			}
			else if (action == "representatives_online")
			{
				representatives_online ();
			}
			else if (action == "republish")
			{
				republish ();
			}
			else if (action == "search_pending")
			{
				search_pending ();
			}
			else if (action == "search_pending_all")
			{
				search_pending_all ();
			}
			else if (action == "send")
			{
				send ();
			}
			else if (action == "sign")
			{
				sign ();
			}
			else if (action == "stats")
			{
				stats ();
			}
			else if (action == "stats_clear")
			{
				stats_clear ();
			}
			else if (action == "stop")
			{
				stop ();
			}
			else if (action == "unchecked")
			{
				unchecked ();
			}
			else if (action == "unchecked_clear")
			{
				unchecked_clear ();
			}
			else if (action == "unchecked_get")
			{
				unchecked_get ();
			}
			else if (action == "unchecked_keys")
			{
				unchecked_keys ();
			}
			else if (action == "uptime")
			{
				uptime ();
			}
			else if (action == "validate_account_number")
			{
				validate_account_number ();
			}
			else if (action == "version")
			{
				version ();
			}
			else if (action == "wallet_add")
			{
				wallet_add ();
			}
			else if (action == "wallet_add_watch")
			{
				wallet_add_watch ();
			}
			// Obsolete
			else if (action == "wallet_balance_total")
			{
				wallet_info ();
			}
			else if (action == "wallet_balances")
			{
				wallet_balances ();
			}
			else if (action == "wallet_change_seed")
			{
				wallet_change_seed ();
			}
			else if (action == "wallet_contains")
			{
				wallet_contains ();
			}
			else if (action == "wallet_create")
			{
				wallet_create ();
			}
			else if (action == "wallet_destroy")
			{
				wallet_destroy ();
			}
			else if (action == "wallet_export")
			{
				wallet_export ();
			}
			else if (action == "wallet_frontiers")
			{
				wallet_frontiers ();
			}
			else if (action == "wallet_history")
			{
				wallet_history ();
			}
			else if (action == "wallet_info")
			{
				wallet_info ();
			}
			else if (action == "wallet_key_valid")
			{
				wallet_key_valid ();
			}
			else if (action == "wallet_ledger")
			{
				wallet_ledger ();
			}
			else if (action == "wallet_lock")
			{
				wallet_lock ();
			}
			else if (action == "wallet_locked")
			{
				password_valid (true);
			}
			else if (action == "wallet_pending")
			{
				wallet_pending ();
			}
			else if (action == "wallet_representative")
			{
				wallet_representative ();
			}
			else if (action == "wallet_representative_set")
			{
				wallet_representative_set ();
			}
			else if (action == "wallet_republish")
			{
				wallet_republish ();
			}
			else if (action == "wallet_unlock")
			{
				password_enter ();
			}
			else if (action == "wallet_work_get")
			{
				wallet_work_get ();
			}
			else if (action == "work_generate")
			{
				work_generate ();
			}
			else if (action == "work_cancel")
			{
				work_cancel ();
			}
			else if (action == "work_get")
			{
				work_get ();
			}
			else if (action == "work_set")
			{
				work_set ();
			}
			else if (action == "work_validate")
			{
				work_validate ();
			}
			else if (action == "work_peer_add")
			{
				work_peer_add ();
			}
			else if (action == "work_peers")
			{
				work_peers ();
			}
			else if (action == "work_peers_clear")
			{
				work_peers_clear ();
			}
			else
			{
				error_response (response, "Unknown command");
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_response (response, "Unable to parse JSON");
	}
	catch (...)
	{
		error_response (response, "Internal server error in RPC");
	}
}

nano::payment_observer::payment_observer (std::function<void(boost::property_tree::ptree const &)> const & response_a, nano::rpc & rpc_a, nano::account const & account_a, nano::amount const & amount_a) :
rpc (rpc_a),
account (account_a),
amount (amount_a),
response (response_a)
{
	completed.clear ();
}

void nano::payment_observer::start (uint64_t timeout)
{
	auto this_l (shared_from_this ());
	rpc.node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout), [this_l]() {
		this_l->complete (nano::payment_status::nothing);
	});
}

nano::payment_observer::~payment_observer ()
{
}

void nano::payment_observer::observe ()
{
	if (rpc.node.balance (account) >= amount.number ())
	{
		complete (nano::payment_status::success);
	}
}

void nano::payment_observer::complete (nano::payment_status status)
{
	auto already (completed.test_and_set ());
	if (!already)
	{
		if (rpc.node.config.logging.log_rpc ())
		{
			BOOST_LOG (rpc.node.log) << boost::str (boost::format ("Exiting payment_observer for account %1% status %2%") % account.to_account () % static_cast<unsigned> (status));
		}
		switch (status)
		{
			case nano::payment_status::nothing:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "nothing");
				response (response_l);
				break;
			}
			case nano::payment_status::success:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "success");
				response (response_l);
				break;
			}
			default:
			{
				error_response (response, "Internal payment error");
				break;
			}
		}
		std::lock_guard<std::mutex> lock (rpc.mutex);
		assert (rpc.payment_observers.find (account) != rpc.payment_observers.end ());
		rpc.payment_observers.erase (account);
	}
}

std::unique_ptr<nano::rpc> nano::get_rpc (boost::asio::io_context & io_ctx_a, nano::node & node_a, nano::rpc_config const & config_a)
{
	std::unique_ptr<rpc> impl;

	if (config_a.secure.enable)
	{
#ifdef NANO_SECURE_RPC
		impl.reset (new rpc_secure (io_ctx_a, node_a, config_a));
#else
		std::cerr << "RPC configured for TLS, but the node is not compiled with TLS support" << std::endl;
#endif
	}
	else
	{
		impl.reset (new rpc (io_ctx_a, node_a, config_a));
	}

	return impl;
}

namespace
{
void construct_json (nano::seq_con_info_component * component, boost::property_tree::ptree & parent)
{
	// We are a leaf node, print name and exit
	if (!component->is_composite ())
	{
		auto & leaf_info = static_cast<nano::seq_con_info_leaf *> (component)->get_info ();
		boost::property_tree::ptree child;
		child.put ("count", leaf_info.count);
		child.put ("size", leaf_info.count * leaf_info.sizeof_element);
		parent.add_child (leaf_info.name, child);
		return;
	}

	auto composite = static_cast<nano::seq_con_info_composite *> (component);

	boost::property_tree::ptree current;
	for (auto & child : composite->get_children ())
	{
		construct_json (child.get (), current);
	}

	parent.add_child (composite->get_name (), current);
}
}
