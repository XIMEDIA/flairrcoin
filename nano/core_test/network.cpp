#include <boost/thread.hpp>
#include <gtest/gtest.h>
#include <nano/core_test/testutil.hpp>
#include <nano/node/testing.hpp>

using namespace std::chrono_literals;

TEST (network, tcp_connection)
{
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor (io_ctx);
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), 24000);
	acceptor.open (endpoint.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
	acceptor.bind (endpoint);
	acceptor.listen ();
	boost::asio::ip::tcp::socket incoming (io_ctx);
	auto done1 (false);
	std::string message1;
	acceptor.async_accept (incoming,
	[&done1, &message1](boost::system::error_code const & ec_a) {
		   if (ec_a)
		   {
			   message1 = ec_a.message ();
			   std::cerr << message1;
		   }
		   done1 = true; });
	boost::asio::ip::tcp::socket connector (io_ctx);
	auto done2 (false);
	std::string message2;
	connector.async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 24000),
	[&done2, &message2](boost::system::error_code const & ec_a) {
		if (ec_a)
		{
			message2 = ec_a.message ();
			std::cerr << message2;
		}
		done2 = true;
	});
	while (!done1 || !done2)
	{
		io_ctx.poll ();
	}
	ASSERT_EQ (0, message1.size ());
	ASSERT_EQ (0, message2.size ());
}

TEST (network, construction)
{
	nano::system system (24000, 1);
	ASSERT_EQ (1, system.nodes.size ());
	ASSERT_EQ (24000, system.nodes[0]->network.socket.local_endpoint ().port ());
}

TEST (network, self_discard)
{
	nano::system system (24000, 1);
	nano::udp_data data;
	data.endpoint = system.nodes[0]->network.endpoint ();
	ASSERT_EQ (0, system.nodes[0]->stats.count (nano::stat::type::error, nano::stat::detail::bad_sender));
	system.nodes[0]->network.receive_action (&data, system.nodes[0]->network.endpoint ());
	ASSERT_EQ (1, system.nodes[0]->stats.count (nano::stat::type::error, nano::stat::detail::bad_sender));
}

TEST (network, send_node_id_handshake)
{
	nano::system system (24000, 1);
	auto list1 (system.nodes[0]->peers.list ());
	ASSERT_EQ (0, list1.size ());
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	node1->start ();
	system.nodes.push_back (node1);
	auto initial (system.nodes[0]->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in));
	auto initial_node1 (node1->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in));
	system.nodes[0]->network.send_keepalive (node1->network.endpoint ());
	ASSERT_EQ (0, system.nodes[0]->peers.list ().size ());
	ASSERT_EQ (0, node1->peers.list ().size ());
	system.deadline_set (10s);
	while (node1->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in) == initial_node1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, system.nodes[0]->peers.list ().size ());
	ASSERT_EQ (1, node1->peers.list ().size ());
	system.deadline_set (10s);
	while (system.nodes[0]->stats.count (nano::stat::type::message, nano::stat::detail::node_id_handshake, nano::stat::dir::in) < initial + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto peers1 (system.nodes[0]->peers.list ());
	auto peers2 (node1->peers.list ());
	ASSERT_EQ (1, peers1.size ());
	ASSERT_EQ (1, peers2.size ());
	ASSERT_EQ (node1->network.endpoint (), peers1[0]);
	ASSERT_EQ (system.nodes[0]->network.endpoint (), peers2[0]);
	node1->stop ();
}

TEST (network, keepalive_ipv4)
{
	nano::system system (24000, 1);
	auto list1 (system.nodes[0]->peers.list ());
	ASSERT_EQ (0, list1.size ());
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	node1->start ();
	system.nodes.push_back (node1);
	node1->send_keepalive (nano::endpoint (boost::asio::ip::address_v4::loopback (), 24000));
	auto initial (system.nodes[0]->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
	system.deadline_set (10s);
	while (system.nodes[0]->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in) == initial)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (network, last_contacted)
{
	nano::system system (24000, 1);
	auto list1 (system.nodes[0]->peers.list ());
	ASSERT_EQ (0, list1.size ());
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	node1->start ();
	system.nodes.push_back (node1);
	node1->send_keepalive (nano::endpoint (boost::asio::ip::address_v4::loopback (), 24000));
	system.deadline_set (10s);

	// Wait until the handshake is complete
	while (system.nodes[0]->peers.size () < 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->peers.size (), 1);

	// Make sure last_contact gets updated on receiving a non-handshake message
	auto timestamp_before_keepalive = system.nodes[0]->peers.list_vector (1).front ().last_contact;
	node1->send_keepalive (nano::endpoint (boost::asio::ip::address_v4::loopback (), 24000));
	while (system.nodes[0]->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in) < 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->peers.size (), 1);
	auto timestamp_after_keepalive = system.nodes[0]->peers.list_vector (1).front ().last_contact;
	ASSERT_GT (timestamp_after_keepalive, timestamp_before_keepalive);

	node1->stop ();
}

TEST (network, multi_keepalive)
{
	nano::system system (24000, 1);
	auto list1 (system.nodes[0]->peers.list ());
	ASSERT_EQ (0, list1.size ());
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	node1->start ();
	system.nodes.push_back (node1);
	ASSERT_EQ (0, node1->peers.size ());
	node1->network.send_keepalive (system.nodes[0]->network.endpoint ());
	ASSERT_EQ (0, node1->peers.size ());
	ASSERT_EQ (0, system.nodes[0]->peers.size ());
	system.deadline_set (10s);
	while (system.nodes[0]->peers.size () != 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	nano::node_init init2;
	auto node2 (std::make_shared<nano::node> (init2, system.io_ctx, 24002, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init2.error ());
	node2->start ();
	system.nodes.push_back (node2);
	node2->network.send_keepalive (system.nodes[0]->network.endpoint ());
	system.deadline_set (10s);
	while (node1->peers.size () != 2 || system.nodes[0]->peers.size () != 2 || node2->peers.size () != 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
	node2->stop ();
}

TEST (network, send_discarded_publish)
{
	nano::system system (24000, 2);
	auto block (std::make_shared<nano::send_block> (1, 1, 2, nano::keypair ().prv, 4, system.work.generate (1)));
	nano::genesis genesis;
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		system.nodes[0]->network.republish_block (block);
		ASSERT_EQ (genesis.hash (), system.nodes[0]->ledger.latest (transaction, nano::test_genesis_key.pub));
		ASSERT_EQ (genesis.hash (), system.nodes[1]->latest (nano::test_genesis_key.pub));
	}
	system.deadline_set (10s);
	while (system.nodes[1]->stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto transaction (system.nodes[0]->store.tx_begin ());
	ASSERT_EQ (genesis.hash (), system.nodes[0]->ledger.latest (transaction, nano::test_genesis_key.pub));
	ASSERT_EQ (genesis.hash (), system.nodes[1]->latest (nano::test_genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
	nano::system system (24000, 2);
	nano::genesis genesis;
	auto block (std::make_shared<nano::send_block> (1, 1, 20, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (1)));
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		system.nodes[0]->network.republish_block (block);
		ASSERT_EQ (genesis.hash (), system.nodes[0]->ledger.latest (transaction, nano::test_genesis_key.pub));
		ASSERT_EQ (genesis.hash (), system.nodes[1]->latest (nano::test_genesis_key.pub));
	}
	system.deadline_set (10s);
	while (system.nodes[1]->stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto transaction (system.nodes[0]->store.tx_begin ());
	ASSERT_EQ (genesis.hash (), system.nodes[0]->ledger.latest (transaction, nano::test_genesis_key.pub));
	ASSERT_EQ (genesis.hash (), system.nodes[1]->latest (nano::test_genesis_key.pub));
}

TEST (network, send_valid_confirm_ack)
{
	nano::system system (24000, 2);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	nano::block_hash latest1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	nano::send_block block2 (latest1, key2.pub, 50, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (latest1));
	nano::block_hash latest2 (system.nodes[1]->latest (nano::test_genesis_key.pub));
	system.nodes[0]->process_active (std::make_shared<nano::send_block> (block2));
	system.deadline_set (10s);
	// Keep polling until latest block changes
	while (system.nodes[1]->latest (nano::test_genesis_key.pub) == latest2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Make sure the balance has decreased after processing the block.
	ASSERT_EQ (50, system.nodes[1]->balance (nano::test_genesis_key.pub));
}

TEST (network, send_valid_publish)
{
	nano::system system (24000, 2);
	system.nodes[0]->bootstrap_initiator.stop ();
	system.nodes[1]->bootstrap_initiator.stop ();
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	nano::block_hash latest1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	nano::send_block block2 (latest1, key2.pub, 50, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (latest1));
	auto hash2 (block2.hash ());
	nano::block_hash latest2 (system.nodes[1]->latest (nano::test_genesis_key.pub));
	system.nodes[1]->process_active (std::make_shared<nano::send_block> (block2));
	system.deadline_set (10s);
	while (system.nodes[0]->stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_NE (hash2, latest2);
	system.deadline_set (10s);
	while (system.nodes[1]->latest (nano::test_genesis_key.pub) == latest2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (50, system.nodes[1]->balance (nano::test_genesis_key.pub));
}

TEST (network, send_insufficient_work)
{
	nano::system system (24000, 2);
	auto block (std::make_shared<nano::send_block> (0, 1, 20, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0));
	nano::publish publish (std::move (block));
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		nano::vectorstream stream (*bytes);
		publish.serialize (stream);
	}
	auto node1 (system.nodes[1]->shared ());
	system.nodes[0]->network.send_buffer (bytes->data (), bytes->size (), system.nodes[1]->network.endpoint (), [bytes, node1](boost::system::error_code const & ec, size_t size) {});
	ASSERT_EQ (0, system.nodes[0]->stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
	system.deadline_set (10s);
	while (system.nodes[1]->stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (1, system.nodes[1]->stats.count (nano::stat::type::error, nano::stat::detail::insufficient_work));
}

TEST (receivable_processor, confirm_insufficient_pos)
{
	nano::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	nano::genesis genesis;
	auto block1 (std::make_shared<nano::send_block> (genesis.hash (), 0, 0, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*block1);
	ASSERT_EQ (nano::process_result::progress, node1.process (*block1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (block1);
	nano::keypair key1;
	auto vote (std::make_shared<nano::vote> (key1.pub, key1.prv, 0, block1));
	nano::confirm_ack con1 (vote);
	node1.process_message (con1, node1.network.endpoint ());
}

TEST (receivable_processor, confirm_sufficient_pos)
{
	nano::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	nano::genesis genesis;
	auto block1 (std::make_shared<nano::send_block> (genesis.hash (), 0, 0, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*block1);
	ASSERT_EQ (nano::process_result::progress, node1.process (*block1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (block1);
	auto vote (std::make_shared<nano::vote> (nano::test_genesis_key.pub, nano::test_genesis_key.prv, 0, block1));
	nano::confirm_ack con1 (vote);
	node1.process_message (con1, node1.network.endpoint ());
}

TEST (receivable_processor, send_with_receive)
{
	auto amount (std::numeric_limits<nano::uint128_t>::max ());
	nano::system system (24000, 2);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::block_hash latest1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	system.wallet (1)->insert_adhoc (key2.prv);
	auto block1 (std::make_shared<nano::send_block> (latest1, key2.pub, amount - system.nodes[0]->config.receive_minimum.number (), nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (latest1)));
	ASSERT_EQ (amount, system.nodes[0]->balance (nano::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes[0]->balance (key2.pub));
	ASSERT_EQ (amount, system.nodes[1]->balance (nano::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes[1]->balance (key2.pub));
	system.nodes[0]->process_active (block1);
	system.nodes[0]->block_processor.flush ();
	system.nodes[1]->process_active (block1);
	system.nodes[1]->block_processor.flush ();
	ASSERT_EQ (amount - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (nano::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes[0]->balance (key2.pub));
	ASSERT_EQ (amount - system.nodes[0]->config.receive_minimum.number (), system.nodes[1]->balance (nano::test_genesis_key.pub));
	ASSERT_EQ (0, system.nodes[1]->balance (key2.pub));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (amount - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (nano::test_genesis_key.pub));
	ASSERT_EQ (system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (key2.pub));
	ASSERT_EQ (amount - system.nodes[0]->config.receive_minimum.number (), system.nodes[1]->balance (nano::test_genesis_key.pub));
	ASSERT_EQ (system.nodes[0]->config.receive_minimum.number (), system.nodes[1]->balance (key2.pub));
}

TEST (network, receive_weight_change)
{
	nano::system system (24000, 2);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	{
		auto transaction (system.nodes[1]->wallets.tx_begin (true));
		system.wallet (1)->store.representative_set (transaction, key2.pub);
	}
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<nano::node> const & node_a) { return node_a->weight (key2.pub) != system.nodes[0]->config.receive_minimum.number (); }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (parse_endpoint, valid)
{
	std::string string ("::1:24000");
	nano::endpoint endpoint;
	ASSERT_FALSE (nano::parse_endpoint (string, endpoint));
	ASSERT_EQ (boost::asio::ip::address_v6::loopback (), endpoint.address ());
	ASSERT_EQ (24000, endpoint.port ());
}

TEST (parse_endpoint, invalid_port)
{
	std::string string ("::1:24a00");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, invalid_address)
{
	std::string string ("::q:24000");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_address)
{
	std::string string (":24000");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_port)
{
	std::string string ("::1:");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_colon)
{
	std::string string ("::1");
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (string, endpoint));
}

// If the account doesn't exist, current == end so there's no iteration
TEST (bulk_pull, no_address)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull);
	req->start = 1;
	req->end = 2;
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	ASSERT_EQ (request->current, request->request->end);
	ASSERT_TRUE (request->current.is_zero ());
}

TEST (bulk_pull, genesis_to_end)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = nano::test_genesis_key.pub;
	req->end.clear ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	ASSERT_EQ (system.nodes[0]->latest (nano::test_genesis_key.pub), request->current);
	ASSERT_EQ (request->request->end, request->request->end);
}

// If we can't find the end block, send everything
TEST (bulk_pull, no_end)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = nano::test_genesis_key.pub;
	req->end = 1;
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	ASSERT_EQ (system.nodes[0]->latest (nano::test_genesis_key.pub), request->current);
	ASSERT_TRUE (request->request->end.is_zero ());
}

TEST (bulk_pull, end_not_owned)
{
	nano::system system (24000, 1);
	nano::keypair key2;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, 100));
	nano::block_hash latest (system.nodes[0]->latest (nano::test_genesis_key.pub));
	nano::open_block open (0, 1, 2, nano::keypair ().prv, 4, 5);
	open.hashables.account = key2.pub;
	open.hashables.representative = key2.pub;
	open.hashables.source = latest;
	open.signature = nano::sign_message (key2.prv, key2.pub, open.hash ());
	ASSERT_EQ (nano::process_result::progress, system.nodes[0]->process (open).code);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	nano::genesis genesis;
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = key2.pub;
	req->end = genesis.hash ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk_pull, none)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	nano::genesis genesis;
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = nano::test_genesis_key.pub;
	req->end = genesis.hash ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	auto block (request->get_next ());
	ASSERT_EQ (nullptr, block);
}

TEST (bulk_pull, get_next_on_open)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = nano::test_genesis_key.pub;
	req->end.clear ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	auto block (request->get_next ());
	ASSERT_NE (nullptr, block);
	ASSERT_TRUE (block->previous ().is_zero ());
	ASSERT_FALSE (connection->requests.empty ());
	ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk_pull, by_block)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	nano::genesis genesis;
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = genesis.hash ();
	req->end.clear ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	auto block (request->get_next ());
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (block->hash (), genesis.hash ());

	block = request->get_next ();
	ASSERT_EQ (nullptr, block);
}

TEST (bulk_pull, by_block_single)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	nano::genesis genesis;
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = genesis.hash ();
	req->end = genesis.hash ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));
	auto block (request->get_next ());
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (block->hash (), genesis.hash ());

	block = request->get_next ();
	ASSERT_EQ (nullptr, block);
}

TEST (bulk_pull, count_limit)
{
	nano::system system (24000, 1);
	nano::genesis genesis;

	auto send1 (std::make_shared<nano::send_block> (system.nodes[0]->latest (nano::test_genesis_key.pub), nano::test_genesis_key.pub, 1, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (system.nodes[0]->latest (nano::test_genesis_key.pub))));
	ASSERT_EQ (nano::process_result::progress, system.nodes[0]->process (*send1).code);
	auto receive1 (std::make_shared<nano::receive_block> (send1->hash (), send1->hash (), nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	ASSERT_EQ (nano::process_result::progress, system.nodes[0]->process (*receive1).code);

	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::bulk_pull> req (new nano::bulk_pull{});
	req->start = receive1->hash ();
	req->set_count_present (true);
	req->count = 2;
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::bulk_pull_server> (connection, std::move (req)));

	ASSERT_EQ (request->max_count, 2);
	ASSERT_EQ (request->sent_count, 0);

	auto block (request->get_next ());
	ASSERT_EQ (receive1->hash (), block->hash ());

	block = request->get_next ();
	ASSERT_EQ (send1->hash (), block->hash ());

	block = request->get_next ();
	ASSERT_EQ (nullptr, block);
}

TEST (bootstrap_processor, DISABLED_process_none)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	auto done (false);
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	while (!done)
	{
		system.io_ctx.run_one ();
	}
	node1->stop ();
}

// Bootstrap can pull one basic block
TEST (bootstrap_processor, process_one)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, nano::test_genesis_key.pub, 100));
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	nano::block_hash hash1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	nano::block_hash hash2 (node1->latest (nano::test_genesis_key.pub));
	ASSERT_NE (hash1, hash2);
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	ASSERT_NE (node1->latest (nano::test_genesis_key.pub), system.nodes[0]->latest (nano::test_genesis_key.pub));
	system.deadline_set (10s);
	while (node1->latest (nano::test_genesis_key.pub) != system.nodes[0]->latest (nano::test_genesis_key.pub))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node1->active.size ());
	node1->stop ();
}

TEST (bootstrap_processor, process_two)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::block_hash hash1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, nano::test_genesis_key.pub, 50));
	nano::block_hash hash2 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, nano::test_genesis_key.pub, 50));
	nano::block_hash hash3 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	ASSERT_NE (hash1, hash2);
	ASSERT_NE (hash1, hash3);
	ASSERT_NE (hash2, hash3);
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	ASSERT_NE (node1->latest (nano::test_genesis_key.pub), system.nodes[0]->latest (nano::test_genesis_key.pub));
	system.deadline_set (10s);
	while (node1->latest (nano::test_genesis_key.pub) != system.nodes[0]->latest (nano::test_genesis_key.pub))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

// Bootstrap can pull universal blocks
TEST (bootstrap_processor, process_state)
{
	nano::system system (24000, 1);
	nano::genesis genesis;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto node0 (system.nodes[0]);
	auto block1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, node0->latest (nano::test_genesis_key.pub), nano::test_genesis_key.pub, nano::genesis_amount - 100, nano::test_genesis_key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0));
	auto block2 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, block1->hash (), nano::test_genesis_key.pub, nano::genesis_amount, block1->hash (), nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0));
	node0->work_generate_blocking (*block1);
	node0->work_generate_blocking (*block2);
	node0->process (*block1);
	node0->process (*block2);
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_EQ (node0->latest (nano::test_genesis_key.pub), block2->hash ());
	ASSERT_NE (node1->latest (nano::test_genesis_key.pub), block2->hash ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	ASSERT_NE (node1->latest (nano::test_genesis_key.pub), node0->latest (nano::test_genesis_key.pub));
	system.deadline_set (10s);
	while (node1->latest (nano::test_genesis_key.pub) != node0->latest (nano::test_genesis_key.pub))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node1->active.size ());
	node1->stop ();
}

TEST (bootstrap_processor, process_new)
{
	nano::system system (24000, 2);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	nano::uint128_t balance1 (system.nodes[0]->balance (nano::test_genesis_key.pub));
	nano::uint128_t balance2 (system.nodes[0]->balance (key2.pub));
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24002, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	system.deadline_set (10s);
	while (node1->balance (key2.pub) != balance2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (balance1, node1->balance (nano::test_genesis_key.pub));
	node1->stop ();
}

TEST (bootstrap_processor, pull_diamond)
{
	nano::system system (24000, 1);
	nano::keypair key;
	auto send1 (std::make_shared<nano::send_block> (system.nodes[0]->latest (nano::test_genesis_key.pub), key.pub, 0, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (system.nodes[0]->latest (nano::test_genesis_key.pub))));
	ASSERT_EQ (nano::process_result::progress, system.nodes[0]->process (*send1).code);
	auto open (std::make_shared<nano::open_block> (send1->hash (), 1, key.pub, key.prv, key.pub, system.work.generate (key.pub)));
	ASSERT_EQ (nano::process_result::progress, system.nodes[0]->process (*open).code);
	auto send2 (std::make_shared<nano::send_block> (open->hash (), nano::test_genesis_key.pub, std::numeric_limits<nano::uint128_t>::max () - 100, key.prv, key.pub, system.work.generate (open->hash ())));
	ASSERT_EQ (nano::process_result::progress, system.nodes[0]->process (*send2).code);
	auto receive (std::make_shared<nano::receive_block> (send1->hash (), send2->hash (), nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	ASSERT_EQ (nano::process_result::progress, system.nodes[0]->process (*receive).code);
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24002, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	system.deadline_set (10s);
	while (node1->balance (nano::test_genesis_key.pub) != 100)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (100, node1->balance (nano::test_genesis_key.pub));
	node1->stop ();
}

TEST (bootstrap_processor, push_diamond)
{
	nano::system system (24000, 1);
	nano::keypair key;
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24002, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	auto wallet1 (node1->wallets.create (100));
	wallet1->insert_adhoc (nano::test_genesis_key.prv);
	wallet1->insert_adhoc (key.prv);
	auto send1 (std::make_shared<nano::send_block> (system.nodes[0]->latest (nano::test_genesis_key.pub), key.pub, 0, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (system.nodes[0]->latest (nano::test_genesis_key.pub))));
	ASSERT_EQ (nano::process_result::progress, node1->process (*send1).code);
	auto open (std::make_shared<nano::open_block> (send1->hash (), 1, key.pub, key.prv, key.pub, system.work.generate (key.pub)));
	ASSERT_EQ (nano::process_result::progress, node1->process (*open).code);
	auto send2 (std::make_shared<nano::send_block> (open->hash (), nano::test_genesis_key.pub, std::numeric_limits<nano::uint128_t>::max () - 100, key.prv, key.pub, system.work.generate (open->hash ())));
	ASSERT_EQ (nano::process_result::progress, node1->process (*send2).code);
	auto receive (std::make_shared<nano::receive_block> (send1->hash (), send2->hash (), nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (send1->hash ())));
	ASSERT_EQ (nano::process_result::progress, node1->process (*receive).code);
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (nano::test_genesis_key.pub) != 100)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (100, system.nodes[0]->balance (nano::test_genesis_key.pub));
	node1->stop ();
}

TEST (bootstrap_processor, push_one)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	nano::keypair key1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	auto wallet (node1->wallets.create (nano::uint256_union ()));
	ASSERT_NE (nullptr, wallet);
	wallet->insert_adhoc (nano::test_genesis_key.prv);
	nano::uint128_t balance1 (node1->balance (nano::test_genesis_key.pub));
	ASSERT_NE (nullptr, wallet->send_action (nano::test_genesis_key.pub, key1.pub, 100));
	ASSERT_NE (balance1, node1->balance (nano::test_genesis_key.pub));
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (nano::test_genesis_key.pub) == balance1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (bootstrap_processor, lazy_hash)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	nano::genesis genesis;
	nano::keypair key1;
	nano::keypair key2;
	// Generating test chain
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::gFLR_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	auto receive1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, nano::gFLR_ratio, send1->hash (), key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (key1.pub)));
	auto send2 (std::make_shared<nano::state_block> (key1.pub, receive1->hash (), key1.pub, 0, key2.pub, key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (receive1->hash ())));
	auto receive2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, nano::gFLR_ratio, send2->hash (), key2.prv, key2.pub, system.nodes[0]->work_generate_blocking (key2.pub)));
	// Processing test chain
	system.nodes[0]->block_processor.add (send1);
	system.nodes[0]->block_processor.add (receive1);
	system.nodes[0]->block_processor.add (send2);
	system.nodes[0]->block_processor.add (receive2);
	system.nodes[0]->block_processor.flush ();
	// Start lazy bootstrap with last block in chain known
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	node1->peers.insert (system.nodes[0]->network.endpoint (), nano::protocol_version);
	node1->bootstrap_initiator.bootstrap_lazy (receive2->hash ());
	// Check processed blocks
	system.deadline_set (10s);
	while (node1->balance (key2.pub) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (bootstrap_processor, lazy_max_pull_count)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	nano::genesis genesis;
	nano::keypair key1;
	nano::keypair key2;
	// Generating test chain
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::gFLR_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	auto receive1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, nano::gFLR_ratio, send1->hash (), key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (key1.pub)));
	auto send2 (std::make_shared<nano::state_block> (key1.pub, receive1->hash (), key1.pub, 0, key2.pub, key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (receive1->hash ())));
	auto receive2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, nano::gFLR_ratio, send2->hash (), key2.prv, key2.pub, system.nodes[0]->work_generate_blocking (key2.pub)));
	auto change1 (std::make_shared<nano::state_block> (key2.pub, receive2->hash (), key1.pub, nano::gFLR_ratio, 0, key2.prv, key2.pub, system.nodes[0]->work_generate_blocking (receive2->hash ())));
	auto change2 (std::make_shared<nano::state_block> (key2.pub, change1->hash (), nano::test_genesis_key.pub, nano::gFLR_ratio, 0, key2.prv, key2.pub, system.nodes[0]->work_generate_blocking (change1->hash ())));
	auto change3 (std::make_shared<nano::state_block> (key2.pub, change2->hash (), key2.pub, nano::gFLR_ratio, 0, key2.prv, key2.pub, system.nodes[0]->work_generate_blocking (change2->hash ())));
	// Processing test chain
	system.nodes[0]->block_processor.add (send1);
	system.nodes[0]->block_processor.add (receive1);
	system.nodes[0]->block_processor.add (send2);
	system.nodes[0]->block_processor.add (receive2);
	system.nodes[0]->block_processor.add (change1);
	system.nodes[0]->block_processor.add (change2);
	system.nodes[0]->block_processor.add (change3);
	system.nodes[0]->block_processor.flush ();
	// Start lazy bootstrap with last block in chain known
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	node1->peers.insert (system.nodes[0]->network.endpoint (), nano::protocol_version);
	node1->bootstrap_initiator.bootstrap_lazy (change3->hash ());
	// Check processed blocks
	system.deadline_set (10s);
	while (node1->block (change3->hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (bootstrap_processor, wallet_lazy_frontier)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	nano::genesis genesis;
	nano::keypair key1;
	nano::keypair key2;
	// Generating test chain
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::gFLR_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	auto receive1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, nano::gFLR_ratio, send1->hash (), key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (key1.pub)));
	auto send2 (std::make_shared<nano::state_block> (key1.pub, receive1->hash (), key1.pub, 0, key2.pub, key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (receive1->hash ())));
	auto receive2 (std::make_shared<nano::state_block> (key2.pub, 0, key2.pub, nano::gFLR_ratio, send2->hash (), key2.prv, key2.pub, system.nodes[0]->work_generate_blocking (key2.pub)));
	// Processing test chain
	system.nodes[0]->block_processor.add (send1);
	system.nodes[0]->block_processor.add (receive1);
	system.nodes[0]->block_processor.add (send2);
	system.nodes[0]->block_processor.add (receive2);
	system.nodes[0]->block_processor.flush ();
	// Start wallet lazy bootstrap
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	node1->peers.insert (system.nodes[0]->network.endpoint (), nano::protocol_version);
	auto wallet (node1->wallets.create (nano::uint256_union ()));
	ASSERT_NE (nullptr, wallet);
	wallet->insert_adhoc (key2.prv);
	node1->bootstrap_wallet ();
	// Check processed blocks
	system.deadline_set (10s);
	while (!node1->ledger.block_exists (receive2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (bootstrap_processor, wallet_lazy_pending)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	nano::genesis genesis;
	nano::keypair key1;
	nano::keypair key2;
	// Generating test chain
	auto send1 (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::gFLR_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	auto receive1 (std::make_shared<nano::state_block> (key1.pub, 0, key1.pub, nano::gFLR_ratio, send1->hash (), key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (key1.pub)));
	auto send2 (std::make_shared<nano::state_block> (key1.pub, receive1->hash (), key1.pub, 0, key2.pub, key1.prv, key1.pub, system.nodes[0]->work_generate_blocking (receive1->hash ())));
	// Processing test chain
	system.nodes[0]->block_processor.add (send1);
	system.nodes[0]->block_processor.add (receive1);
	system.nodes[0]->block_processor.add (send2);
	system.nodes[0]->block_processor.flush ();
	// Start wallet lazy bootstrap
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	node1->peers.insert (system.nodes[0]->network.endpoint (), nano::protocol_version);
	auto wallet (node1->wallets.create (nano::uint256_union ()));
	ASSERT_NE (nullptr, wallet);
	wallet->insert_adhoc (key2.prv);
	node1->bootstrap_wallet ();
	// Check processed blocks
	system.deadline_set (10s);
	while (!node1->ledger.block_exists (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (frontier_req_response, DISABLED_destruction)
{
	{
		std::shared_ptr<nano::frontier_req_server> hold; // Destructing tcp acceptor on non-existent io_context
		{
			nano::system system (24000, 1);
			auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
			std::unique_ptr<nano::frontier_req> req (new nano::frontier_req);
			req->start.clear ();
			req->age = std::numeric_limits<decltype (req->age)>::max ();
			req->count = std::numeric_limits<decltype (req->count)>::max ();
			connection->requests.push (std::unique_ptr<nano::message>{});
			hold = std::make_shared<nano::frontier_req_server> (connection, std::move (req));
		}
	}
	ASSERT_TRUE (true);
}

TEST (frontier_req, begin)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::frontier_req> req (new nano::frontier_req);
	req->start.clear ();
	req->age = std::numeric_limits<decltype (req->age)>::max ();
	req->count = std::numeric_limits<decltype (req->count)>::max ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::frontier_req_server> (connection, std::move (req)));
	ASSERT_EQ (nano::test_genesis_key.pub, request->current);
	nano::genesis genesis;
	ASSERT_EQ (genesis.hash (), request->frontier);
}

TEST (frontier_req, end)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::frontier_req> req (new nano::frontier_req);
	req->start = nano::test_genesis_key.pub.number () + 1;
	req->age = std::numeric_limits<decltype (req->age)>::max ();
	req->count = std::numeric_limits<decltype (req->count)>::max ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::frontier_req_server> (connection, std::move (req)));
	ASSERT_TRUE (request->current.is_zero ());
}

TEST (frontier_req, count)
{
	nano::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	nano::genesis genesis;
	// Public key FB93... after genesis in accounts table
	nano::keypair key1 ("ED5AE0A6505B14B67435C29FD9FEEBC26F597D147BC92F6D795FFAD7AFD3D967");
	nano::state_block send1 (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::gFLR_ratio, key1.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0);
	node1.work_generate_blocking (send1);
	ASSERT_EQ (nano::process_result::progress, node1.process (send1).code);
	nano::state_block receive1 (key1.pub, 0, nano::test_genesis_key.pub, nano::gFLR_ratio, send1.hash (), key1.prv, key1.pub, 0);
	node1.work_generate_blocking (receive1);
	ASSERT_EQ (nano::process_result::progress, node1.process (receive1).code);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::frontier_req> req (new nano::frontier_req);
	req->start.clear ();
	req->age = std::numeric_limits<decltype (req->age)>::max ();
	req->count = 1;
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::frontier_req_server> (connection, std::move (req)));
	ASSERT_EQ (nano::test_genesis_key.pub, request->current);
	ASSERT_EQ (send1.hash (), request->frontier);
}

TEST (frontier_req, time_bound)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::frontier_req> req (new nano::frontier_req);
	req->start.clear ();
	req->age = 1;
	req->count = std::numeric_limits<decltype (req->count)>::max ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::frontier_req_server> (connection, std::move (req)));
	ASSERT_EQ (nano::test_genesis_key.pub, request->current);
	// Wait 2 seconds until age of account will be > 1 seconds
	std::this_thread::sleep_for (std::chrono::milliseconds (2100));
	std::unique_ptr<nano::frontier_req> req2 (new nano::frontier_req);
	req2->start.clear ();
	req2->age = 1;
	req2->count = std::numeric_limits<decltype (req->count)>::max ();
	auto connection2 (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	connection2->requests.push (std::unique_ptr<nano::message>{});
	auto request2 (std::make_shared<nano::frontier_req_server> (connection, std::move (req2)));
	ASSERT_TRUE (request2->current.is_zero ());
}

TEST (frontier_req, time_cutoff)
{
	nano::system system (24000, 1);
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	std::unique_ptr<nano::frontier_req> req (new nano::frontier_req);
	req->start.clear ();
	req->age = 3;
	req->count = std::numeric_limits<decltype (req->count)>::max ();
	connection->requests.push (std::unique_ptr<nano::message>{});
	auto request (std::make_shared<nano::frontier_req_server> (connection, std::move (req)));
	ASSERT_EQ (nano::test_genesis_key.pub, request->current);
	nano::genesis genesis;
	ASSERT_EQ (genesis.hash (), request->frontier);
	// Wait 4 seconds until age of account will be > 3 seconds
	std::this_thread::sleep_for (std::chrono::milliseconds (4100));
	std::unique_ptr<nano::frontier_req> req2 (new nano::frontier_req);
	req2->start.clear ();
	req2->age = 3;
	req2->count = std::numeric_limits<decltype (req->count)>::max ();
	auto connection2 (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));
	connection2->requests.push (std::unique_ptr<nano::message>{});
	auto request2 (std::make_shared<nano::frontier_req_server> (connection, std::move (req2)));
	ASSERT_TRUE (request2->frontier.is_zero ());
}

TEST (bulk, genesis)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	nano::block_hash latest1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	nano::block_hash latest2 (node1->latest (nano::test_genesis_key.pub));
	ASSERT_EQ (latest1, latest2);
	nano::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, 100));
	nano::block_hash latest3 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	ASSERT_NE (latest1, latest3);
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	system.deadline_set (10s);
	while (node1->latest (nano::test_genesis_key.pub) != system.nodes[0]->latest (nano::test_genesis_key.pub))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (node1->latest (nano::test_genesis_key.pub), system.nodes[0]->latest (nano::test_genesis_key.pub));
	node1->stop ();
}

TEST (bulk, offline_send)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::node_init init1;
	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, 24001, nano::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	node1->start ();
	system.nodes.push_back (node1);
	nano::keypair key2;
	auto wallet (node1->wallets.create (nano::uint256_union ()));
	wallet->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (std::numeric_limits<nano::uint256_t>::max (), system.nodes[0]->balance (nano::test_genesis_key.pub));
	// Wait to finish election background tasks
	system.deadline_set (10s);
	while (!system.nodes[0]->active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Initiate bootstrap
	node1->bootstrap_initiator.bootstrap (system.nodes[0]->network.endpoint ());
	// Nodes should find each other
	do
	{
		ASSERT_NO_ERROR (system.poll ());
	} while (system.nodes[0]->peers.empty () || node1->peers.empty ());
	// Send block arrival via bootstrap
	while (node1->balance (nano::test_genesis_key.pub) == std::numeric_limits<nano::uint256_t>::max ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Receiving send block
	system.deadline_set (20s);
	while (node1->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (network, ipv6)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::address_v6::from_string ("::ffff:127.0.0.1"));
	ASSERT_TRUE (address.is_v4_mapped ());
	nano::endpoint endpoint1 (address, 16384);
	std::vector<uint8_t> bytes1;
	{
		nano::vectorstream stream (bytes1);
		nano::write (stream, address.to_bytes ());
	}
	ASSERT_EQ (16, bytes1.size ());
	for (auto i (bytes1.begin ()), n (bytes1.begin () + 10); i != n; ++i)
	{
		ASSERT_EQ (0, *i);
	}
	ASSERT_EQ (0xff, bytes1[10]);
	ASSERT_EQ (0xff, bytes1[11]);
	std::array<uint8_t, 16> bytes2;
	nano::bufferstream stream (bytes1.data (), bytes1.size ());
	auto error (nano::try_read (stream, bytes2));
	ASSERT_FALSE (error);
	nano::endpoint endpoint2 (boost::asio::ip::address_v6 (bytes2), 16384);
	ASSERT_EQ (endpoint1, endpoint2);
}

TEST (network, ipv6_from_ipv4)
{
	nano::endpoint endpoint1 (boost::asio::ip::address_v4::loopback (), 16000);
	ASSERT_TRUE (endpoint1.address ().is_v4 ());
	nano::endpoint endpoint2 (boost::asio::ip::address_v6::v4_mapped (endpoint1.address ().to_v4 ()), 16000);
	ASSERT_TRUE (endpoint2.address ().is_v6 ());
}

TEST (network, ipv6_bind_send_ipv4)
{
	boost::asio::io_context io_ctx;
	nano::endpoint endpoint1 (boost::asio::ip::address_v6::any (), 24000);
	nano::endpoint endpoint2 (boost::asio::ip::address_v4::any (), 24001);
	std::array<uint8_t, 16> bytes1;
	auto finish1 (false);
	nano::endpoint endpoint3;
	boost::asio::ip::udp::socket socket1 (io_ctx, endpoint1);
	socket1.async_receive_from (boost::asio::buffer (bytes1.data (), bytes1.size ()), endpoint3, [&finish1](boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (error);
		ASSERT_EQ (16, size_a);
		finish1 = true;
	});
	boost::asio::ip::udp::socket socket2 (io_ctx, endpoint2);
	nano::endpoint endpoint5 (boost::asio::ip::address_v4::loopback (), 24000);
	nano::endpoint endpoint6 (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4::loopback ()), 24001);
	socket2.async_send_to (boost::asio::buffer (std::array<uint8_t, 16>{}, 16), endpoint5, [](boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (error);
		ASSERT_EQ (16, size_a);
	});
	auto iterations (0);
	while (!finish1)
	{
		io_ctx.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	ASSERT_EQ (endpoint6, endpoint3);
	std::array<uint8_t, 16> bytes2;
	nano::endpoint endpoint4;
	socket2.async_receive_from (boost::asio::buffer (bytes2.data (), bytes2.size ()), endpoint4, [](boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (!error);
		ASSERT_EQ (16, size_a);
	});
	socket1.async_send_to (boost::asio::buffer (std::array<uint8_t, 16>{}, 16), endpoint6, [](boost::system::error_code const & error, size_t size_a) {
		ASSERT_FALSE (error);
		ASSERT_EQ (16, size_a);
	});
}

TEST (network, endpoint_bad_fd)
{
	nano::system system (24000, 1);
	system.nodes[0]->stop ();
	auto endpoint (system.nodes[0]->network.endpoint ());
	ASSERT_TRUE (endpoint.address ().is_loopback ());
	ASSERT_EQ (0, endpoint.port ());
}

TEST (network, reserved_address)
{
	ASSERT_FALSE (nano::reserved_address (nano::endpoint (boost::asio::ip::address_v6::from_string ("2001::"), 0), true));
	nano::endpoint loopback (boost::asio::ip::address_v6::from_string ("::1"), 1);
	ASSERT_FALSE (nano::reserved_address (loopback, false));
	ASSERT_TRUE (nano::reserved_address (loopback, true));
}

TEST (node, port_mapping)
{
	nano::system system (24000, 1);
	auto node0 (system.nodes[0]);
	node0->port_mapping.refresh_devices ();
	node0->port_mapping.start ();
	auto end (std::chrono::steady_clock::now () + std::chrono::seconds (500));
	(void)end;
	//while (std::chrono::steady_clock::now () < end)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (udp_buffer, one_buffer)
{
	nano::stat stats;
	nano::udp_buffer buffer (stats, 512, 1);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.dequeue ());
	ASSERT_EQ (buffer1, buffer2);
	buffer.release (buffer2);
	auto buffer3 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer3);
}

TEST (udp_buffer, two_buffers)
{
	nano::stat stats;
	nano::udp_buffer buffer (stats, 512, 2);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer2);
	ASSERT_NE (buffer1, buffer2);
	buffer.enqueue (buffer2);
	buffer.enqueue (buffer1);
	auto buffer3 (buffer.dequeue ());
	ASSERT_EQ (buffer2, buffer3);
	auto buffer4 (buffer.dequeue ());
	ASSERT_EQ (buffer1, buffer4);
	buffer.release (buffer3);
	buffer.release (buffer4);
	auto buffer5 (buffer.allocate ());
	ASSERT_EQ (buffer2, buffer5);
	auto buffer6 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer6);
}

TEST (udp_buffer, one_overflow)
{
	nano::stat stats;
	nano::udp_buffer buffer (stats, 512, 1);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer2);
}

TEST (udp_buffer, two_overflow)
{
	nano::stat stats;
	nano::udp_buffer buffer (stats, 512, 2);
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer2);
	ASSERT_NE (buffer1, buffer2);
	buffer.enqueue (buffer2);
	auto buffer3 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer3);
	auto buffer4 (buffer.allocate ());
	ASSERT_EQ (buffer2, buffer4);
}

TEST (udp_buffer, one_buffer_multithreaded)
{
	nano::stat stats;
	nano::udp_buffer buffer (stats, 512, 1);
	boost::thread thread ([&buffer]() {
		auto done (false);
		while (!done)
		{
			auto item (buffer.dequeue ());
			done = item == nullptr;
			if (item != nullptr)
			{
				buffer.release (item);
			}
		}
	});
	auto buffer1 (buffer.allocate ());
	ASSERT_NE (nullptr, buffer1);
	buffer.enqueue (buffer1);
	auto buffer2 (buffer.allocate ());
	ASSERT_EQ (buffer1, buffer2);
	buffer.stop ();
	thread.join ();
}

TEST (udp_buffer, many_buffers_multithreaded)
{
	nano::stat stats;
	nano::udp_buffer buffer (stats, 512, 16);
	std::vector<boost::thread> threads;
	for (auto i (0); i < 4; ++i)
	{
		threads.push_back (boost::thread ([&buffer]() {
			auto done (false);
			while (!done)
			{
				auto item (buffer.dequeue ());
				done = item == nullptr;
				if (item != nullptr)
				{
					buffer.release (item);
				}
			}
		}));
	}
	std::atomic_int count (0);
	for (auto i (0); i < 4; ++i)
	{
		threads.push_back (boost::thread ([&buffer, &count]() {
			auto done (false);
			for (auto i (0); !done && i < 1000; ++i)
			{
				auto item (buffer.allocate ());
				done = item == nullptr;
				if (item != nullptr)
				{
					buffer.enqueue (item);
					++count;
					if (count > 3000)
					{
						buffer.stop ();
					}
				}
			}
		}));
	}
	buffer.stop ();
	for (auto & i : threads)
	{
		i.join ();
	}
}

TEST (udp_buffer, stats)
{
	nano::stat stats;
	nano::udp_buffer buffer (stats, 512, 1);
	auto buffer1 (buffer.allocate ());
	buffer.enqueue (buffer1);
	buffer.allocate ();
	ASSERT_EQ (1, stats.count (nano::stat::type::udp, nano::stat::detail::overflow));
}

TEST (bulk_pull_account, basics)
{
	nano::system system (24000, 1);
	system.nodes[0]->config.receive_minimum = nano::uint128_union (20);
	nano::keypair key1;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key1.prv);
	auto send1 (system.wallet (0)->send_action (nano::genesis_account, key1.pub, 25));
	auto send2 (system.wallet (0)->send_action (nano::genesis_account, key1.pub, 10));
	auto send3 (system.wallet (0)->send_action (nano::genesis_account, key1.pub, 2));
	system.deadline_set (5s);
	while (system.nodes[0]->balance (key1.pub) != 25)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto connection (std::make_shared<nano::bootstrap_server> (nullptr, system.nodes[0]));

	{
		std::unique_ptr<nano::bulk_pull_account> req (new nano::bulk_pull_account{});
		req->account = key1.pub;
		req->minimum_amount = 5;
		req->flags = nano::bulk_pull_account_flags ();
		connection->requests.push (std::unique_ptr<nano::message>{});
		auto request (std::make_shared<nano::bulk_pull_account_server> (connection, std::move (req)));
		ASSERT_FALSE (request->invalid_request);
		ASSERT_FALSE (request->pending_include_address);
		ASSERT_FALSE (request->pending_address_only);
		ASSERT_EQ (request->current_key.account, key1.pub);
		ASSERT_EQ (request->current_key.hash, 0);
		auto block_data (request->get_next ());
		ASSERT_EQ (send2->hash (), block_data.first.get ()->hash);
		ASSERT_EQ (nano::uint128_union (10), block_data.second.get ()->amount);
		ASSERT_EQ (nano::genesis_account, block_data.second.get ()->source);
		ASSERT_EQ (nullptr, request->get_next ().first.get ());
	}

	{
		std::unique_ptr<nano::bulk_pull_account> req (new nano::bulk_pull_account{});
		req->account = key1.pub;
		req->minimum_amount = 0;
		req->flags = nano::bulk_pull_account_flags::pending_address_only;
		auto request (std::make_shared<nano::bulk_pull_account_server> (connection, std::move (req)));
		ASSERT_TRUE (request->pending_address_only);
		auto block_data (request->get_next ());
		ASSERT_NE (nullptr, block_data.first.get ());
		ASSERT_NE (nullptr, block_data.second.get ());
		ASSERT_EQ (nano::genesis_account, block_data.second.get ()->source);
		block_data = request->get_next ();
		ASSERT_EQ (nullptr, block_data.first.get ());
		ASSERT_EQ (nullptr, block_data.second.get ());
	}
}

TEST (bootstrap, keepalive)
{
	nano::system system (24000, 1);
	auto socket (std::make_shared<nano::socket> (system.nodes[0]));
	nano::keepalive keepalive;
	auto input (keepalive.to_bytes ());
	socket->async_connect (system.nodes[0]->bootstrap.endpoint (), [&input, socket](boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		socket->async_write (input, [&input](boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
			ASSERT_EQ (input->size (), size_a);
		});
	});

	auto output (keepalive.to_bytes ());
	bool done (false);
	socket->async_read (output, output->size (), [&output, &done](boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
		ASSERT_EQ (output->size (), size_a);
		done = true;
	});
	system.deadline_set (std::chrono::seconds (5));
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}
