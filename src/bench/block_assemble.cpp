// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <script/standard.h>
#include <test/util.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <validation.h>


#include <vector>

static void AssembleBlock(benchmark::Bench& bench)
{
    RegTestingSetup test_setup;
    const CScript redeemScript = CScript() << OP_DROP << OP_TRUE;
    const CScript SCRIPT_PUB =
        CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemScript))
                  << OP_EQUAL;

    const CScript scriptSig = CScript() << std::vector<uint8_t>(100, 0xff)
                                        << ToByteVector(redeemScript);

    // Collect some loose transactions that spend the coinbases of our mined blocks
    int nHeight = WITH_LOCK(cs_main, return ::ChainActive().Height());
    constexpr size_t NUM_BLOCKS{200};
    std::vector<CTransactionRef> txs(NUM_BLOCKS - GetCurrentCoinBaseMaturity(nHeight) + 1);
    for (size_t b{0}; b < NUM_BLOCKS; ++b) {
        CMutableTransaction tx;
        tx.vin.push_back(MineBlock(test_setup.m_node, SCRIPT_PUB));
        tx.vin.back().scriptSig = scriptSig;
        tx.vout.emplace_back(1337, SCRIPT_PUB);
        if (NUM_BLOCKS - b >= GetCurrentCoinBaseMaturity(nHeight))
            txs.at(b) = MakeTransactionRef(tx);
    }
    {
        LOCK(::cs_main); // Required for ::AcceptToMemoryPool.

        for (const auto& txr : txs) {
            CValidationState state;
            bool ret{::AcceptToMemoryPool(*test_setup.m_node.mempool, state, txr, nullptr /* pfMissingInputs */, false /* bypass_limits */, /* nAbsurdFee */ 0)};
            assert(ret);
        }
    }

    bench.minEpochIterations(700).run([&] {
        PrepareBlock(test_setup.m_node, SCRIPT_PUB);
    });
}

BENCHMARK(AssembleBlock);
