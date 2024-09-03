// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <autolykos/src/AutolykosPowScheme.h>
#include <autolykos/src/Header.h>
#include <chain.h>
#include <primitives/block.cpp>
#include <primitives/block.h>
#include <rpc/mining.cpp>
#include <uint256.h>
#include <validation.h>

#include <math.h>

unsigned int static DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    /* current difficulty formula, blocx - DarkGravity v3, written by Evan Duffield - evan@blocx.org */
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    int64_t nPastBlocks = 24;

    if (pindexLast->nHeight == params.AutolykosForkHeight) {
        return 0x1d093ab9;
    }

    // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return powLimit
    if (!pindexLast || pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex *pindex = pindexLast;
    arith_uint256 bnPastTargetAvg;

    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if(nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t blockTime = params.GetCurrentPowTargetSpacing(pindexLast->nHeight + 1);

    int64_t nTargetTimespan = nPastBlocks * blockTime;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 2.5 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 1 day worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

   return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    assert(pblock != nullptr);
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    // this is only active on devnets
    if (pindexLast->nHeight < params.nMinimumDifficultyBlocks) {
        return bnPowLimit.GetCompact();
    }

    if (pindexLast->nHeight + 1 < params.nPowKGWHeight) {
        return GetNextWorkRequiredBTC(pindexLast, pblock, params);
    }

    // Note: GetNextWorkRequiredBTC has it's own special difficulty rule,
    // so we only apply this to post-BTC algos.
    if (params.fPowNoRetargeting) {
        return bnPowLimit.GetCompact();
    }

    if (params.fPowAllowMinDifficultyBlocks) {
        // recent block is more than 2 hours old
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + 2 * 60 * 60) {
            return bnPowLimit.GetCompact();
        }
        // recent block is more than 10 minutes old
        int64_t blockTime = params.GetCurrentPowTargetSpacing(pindexLast->nHeight + 1);
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + blockTime * 4) {
            arith_uint256 bnNew = arith_uint256().SetCompact(pindexLast->nBits) * 10;
            if (bnNew > bnPowLimit) {
                return bnPowLimit.GetCompact();
            }
            return bnNew.GetCompact();
        }
    }

    return DarkGravityWave(pindexLast, params);
}

// for DIFF_BTC only!
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(const CBlockHeader& b_h, uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    // currently just returning HashX11 here
    if (b_h.nVersion != params.AutolykosForkSwitchVersion) {
        bool fNegative;
        bool fOverflow;
        arith_uint256 bnTarget;

        bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

        // Check range
        if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
            return false;

        // Check proof of work matches claimed amount
        if (UintToArith256(hash) > bnTarget)
            return false;

        return true;
    }

    AutolykosPowScheme powScheme;

    Version version = params.AutolykosForkSwitchVersion;

    std::vector<uint8_t> nonce = powScheme.uint64ToBytes(b_h.nNewNonce);
    ModifierId parentId = powScheme.hexToBytesModifierId(b_h.hashPrevBlock.ToString());
    Digest32 transactionsRoot = powScheme.hexToArrayDigest32(b_h.hashMerkleRoot.ToString());

    AutolykosSolution powSolution = {nonce};

    Header h(
        params.AutolykosForkSwitchVersion,
        parentId,
        transactionsRoot,
        b_h.nTime,
        nBits,
        b_h.aHeight,
        powSolution
    );

    bool valid = powScheme.validate(h);
    return valid;
}
