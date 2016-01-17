#include <gtest/gtest.h>
#include <rai/node/testing.hpp>

#include <thread>

TEST (system, generate_mass_activity)
{
    rai::system system (24000, 1);
    system.wallet (0)->insert (rai::test_genesis_key.prv);
    size_t count (20);
    system.generate_mass_activity (count, *system.nodes [0]);
    size_t accounts (0);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    for (auto i (system.nodes [0]->store.latest_begin (transaction)), n (system.nodes [0]->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}

TEST (system, generate_mass_activity_long)
{
	std::vector <std::thread> threads;
	{
		rai::system system (24000, 1);
		for (auto i (0), n (4); i != n; ++i)
		{
			threads.push_back (std::thread ([&system] ()
			{
				system.service->run ();
			}));
		}
		for (auto i (0), n (4); i != n; ++i)
		{
			threads.push_back (std::thread ([&system] ()
			{
				system.processor.run ();
			}));
		}
		system.wallet (0)->insert (rai::test_genesis_key.prv);
		size_t count (10000);
		system.generate_mass_activity (count, *system.nodes [0]);
		size_t accounts (0);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		for (auto i (system.nodes [0]->store.latest_begin (transaction)), n (system.nodes [0]->store.latest_end ()); i != n; ++i)
		{
			++accounts;
		}
		ASSERT_GT (accounts, count / 10);
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (ledger, deep_account_compute)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::ledger ledger (store);
	rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
	genesis.initialize (transaction, store);
	rai::keypair key;
	auto balance (rai::genesis_amount - 1);
	rai::send_block send (genesis.hash (), key.pub, balance, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
	rai::open_block open (send.hash (), rai::test_genesis_key.pub, key.pub, key.prv, key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open).code);
	auto sprevious (send.hash ());
	auto rprevious (open.hash ());
	for (auto i (0), n (100000); i != n; ++i)
	{
		balance -= 1;
		rai::send_block send (sprevious, key.pub, balance, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
		sprevious = send.hash ();
		rai::receive_block receive (rprevious, send.hash (), key.prv, key.pub, 0);
		ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, receive).code);
		rprevious = receive.hash ();
		if (i % 100 == 0)
		{
			std::cerr << i << ' ';
		}
		auto account (ledger.account (transaction, sprevious));
		auto balance (ledger.balance (transaction, rprevious));
	}
}

TEST (wallet, multithreaded_send)
{
	std::vector <std::thread> threads;
	{
		rai::system system (24000, 1);
		rai::keypair key;
		auto wallet_l (system.wallet (0));
		wallet_l->insert (rai::test_genesis_key.prv);
		for (auto i (0); i < 20; ++i)
		{
			threads.push_back (std::thread ([wallet_l, &key] ()
			{
				for (auto i (0); i < 1000; ++i)
				{
					wallet_l->send_sync (rai::test_genesis_key.pub, key.pub, 1000);
				}
			}));
		}
		while (system.nodes [0]->balance(rai::test_genesis_key.pub) != (rai::genesis_amount - 20 * 1000 * 1000))
		{
			system.poll ();
		}
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (store, load)
{
	rai::system system (24000, 1);
	std::vector <std::thread> threads;
	for (auto i (0); i < 100; ++i)
	{
		threads.push_back (std::thread ([&system] ()
		{
			for (auto i (0); i != 1000; ++i)
			{
				rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
				for (auto j (0); j != 10; ++j)
				{
					rai::block_hash hash;
					rai::random_pool.GenerateBlock (hash.bytes.data (), hash.bytes.size ());
					system.nodes [0]->store.account_put (transaction, hash, rai::account_info ());
				}
			}
		}));
	}
	for (auto &i: threads)
	{
		i.join ();
	}
}

TEST (node, fork_storm)
{
	rai::system system (24000, 64);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	auto previous (system.nodes [0]->latest (rai::test_genesis_key.pub));
	auto balance (system.nodes [0]->balance (rai::test_genesis_key.pub));
	ASSERT_FALSE (previous.is_zero ());
	for (auto j (0); j != system.nodes.size (); ++j)
	{
		balance -= 1;
		rai::keypair key;
		rai::send_block send (previous, key.pub, balance, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
		previous = send.hash ();
		for (auto i (0); i != system.nodes.size (); ++i)
		{
			auto send_result (system.nodes [i]->process (send));
			ASSERT_EQ (rai::process_result::progress, send_result.code);
			rai::keypair rep;
			rai::open_block open (previous, rep.pub, key.pub, key.prv, key.pub, 0);
			system.nodes [i]->generate_work (open);
			auto open_result (system.nodes [i]->process (open));
			ASSERT_EQ (rai::process_result::progress, open_result.code);
			system.nodes [i]->network.republish_block (open.clone (), 0);
		}
	}
	auto again (true);
	
	int empty (0);
	int single (0);
	while (again)
	{
		empty = 0;
		single = 0;
		std::for_each (system.nodes.begin (), system.nodes.end (), [&] (std::shared_ptr <rai::node> const & node_a)
		{
			if (node_a->conflicts.roots.empty ())
			{
				++empty;
			}
			else
			{
				if (node_a->conflicts.roots.begin ()->election->votes.rep_votes.size () == 1)
				{
					++single;
				}
			}
		});
		system.poll ();
		std::cerr << "Empty: " << empty << " single: " << single << std::endl;
		again = empty != 0 || single != 0;
	}
	ASSERT_TRUE (true);
}

TEST (gap_cache, limit)
{
    rai::system system (24000, 1);
    rai::gap_cache cache (*system.nodes [0]);
    for (auto i (0); i < cache.max * 2; ++i)
    {
        rai::send_block block1 (i, 0, 1, rai::keypair ().prv, 3, 4);
        auto previous (block1.previous ());
        cache.add (rai::send_block (block1), previous);
    }
    ASSERT_EQ (cache.max, cache.blocks.size ());
}