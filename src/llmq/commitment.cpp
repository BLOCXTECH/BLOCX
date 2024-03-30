// Copyright (c) 2018-2022 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/commitment.h>

#include <evo/deterministicmns.h>
#include <evo/specialtx.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <llmq/utils.h>
#include <logging.h>
#include <validation.h>
#include <util/underlying.h>

namespace llmq
{

CFinalCommitment::CFinalCommitment(const Consensus::LLMQParams& params, const uint256& _quorumHash) :
        llmqType(params.type),
        quorumHash(_quorumHash),
        signers(params.size),
        validMembers(params.size)
{
}

template<typename... Types>
void LogPrintfFinalCommitment(Types... out) {
    if (LogAcceptCategory(BCLog::LLMQ)) {
        LogInstance().LogPrintStr(strprintf("CFinalCommitment::%s -- %s", __func__, tinyformat::format(out...)));
    }
}

bool CFinalCommitment::Verify(const CBlockIndex* pQuorumBaseBlockIndex, bool checkSigs) const
{
    const auto& llmq_params_opt = GetLLMQParams(llmqType);
    if (!llmq_params_opt.has_value()) {
        LogPrintfFinalCommitment("q[%s] invalid llmqType=%d\n", quorumHash.ToString(), ToUnderlying(llmqType));
        return false;
    }
    const auto& llmq_params = llmq_params_opt.value();

    uint16_t expected_nversion{CFinalCommitment::LEGACY_BLS_NON_INDEXED_QUORUM_VERSION};
    if (utils::IsQuorumRotationEnabled(llmq_params, pQuorumBaseBlockIndex)) {
        expected_nversion = utils::IsV19Active(pQuorumBaseBlockIndex) ? CFinalCommitment::BASIC_BLS_INDEXED_QUORUM_VERSION : CFinalCommitment::LEGACY_BLS_INDEXED_QUORUM_VERSION;
    }
    else {
        expected_nversion = utils::IsV19Active(pQuorumBaseBlockIndex) ? CFinalCommitment::BASIC_BLS_NON_INDEXED_QUORUM_VERSION : CFinalCommitment::LEGACY_BLS_NON_INDEXED_QUORUM_VERSION;
    }
    if (nVersion == 0 || nVersion != expected_nversion) {
        LogPrintfFinalCommitment("q[%s] invalid nVersion=%d expectednVersion\n", quorumHash.ToString(), nVersion, expected_nversion);
        return false;
    }

    if (pQuorumBaseBlockIndex->GetBlockHash() != quorumHash) {
        LogPrintfFinalCommitment("q[%s] invalid quorumHash\n", quorumHash.ToString());
        return false;
    }

    if ((pQuorumBaseBlockIndex->nHeight % llmq_params.dkgInterval) != quorumIndex) {
        LogPrintfFinalCommitment("q[%s] invalid quorumIndex=%d\n", quorumHash.ToString(), quorumIndex);
        return false;
    }

    if (!VerifySizes(llmq_params)) {
        return false;
    }

    if (CountValidMembers() < llmq_params.minSize) {
        LogPrintfFinalCommitment("q[%s] invalid validMembers count. validMembersCount=%d\n", quorumHash.ToString(), CountValidMembers());
        return false;
    }
    if (CountSigners() < llmq_params.minSize) {
        LogPrintfFinalCommitment("q[%s] invalid signers count. signersCount=%d\n", quorumHash.ToString(), CountSigners());
        return false;
    }
    if (!quorumPublicKey.IsValid()) {
        LogPrintfFinalCommitment("q[%s] invalid quorumPublicKey\n", quorumHash.ToString());
        return false;
    }
    if (quorumVvecHash.IsNull()) {
        LogPrintfFinalCommitment("q[%s] invalid quorumVvecHash\n", quorumHash.ToString());
        return false;
    }
    if (!membersSig.IsValid()) {
        LogPrintfFinalCommitment("q[%s] invalid membersSig\n", quorumHash.ToString());
        return false;
    }
    if (!quorumSig.IsValid()) {
        LogPrintfFinalCommitment("q[%s] invalid vvecSig\n");
        return false;
    }
    auto members = utils::GetAllQuorumMembers(llmqType, pQuorumBaseBlockIndex);
    if (LogAcceptCategory(BCLog::LLMQ)) {
        std::stringstream ss;
        std::stringstream ss2;
        for (const auto i: irange::range(llmq_params.size)) {
            ss << "v[" << i << "]=" << validMembers[i];
            ss2 << "s[" << i << "]=" << signers[i];
        }
        LogPrintfFinalCommitment("CFinalCommitment::%s mns[%d] validMembers[%s] signers[%s]\n", __func__, members.size(), ss.str(), ss2.str());
    }

    for (const auto i : irange::range(members.size(), size_t(llmq_params.size))) {
        if (validMembers[i]) {
            LogPrintfFinalCommitment("q[%s] invalid validMembers bitset. bit %d should not be set\n", quorumHash.ToString(), i);
            return false;
        }
        if (signers[i]) {
            LogPrintfFinalCommitment("q[%s] invalid signers bitset. bit %d should not be set\n", quorumHash.ToString(), i);
            return false;
        }
    }

    // sigs are only checked when the block is processed
    if (checkSigs) {
        uint256 commitmentHash = utils::BuildCommitmentHash(llmq_params.type, quorumHash, validMembers, quorumPublicKey, quorumVvecHash);
        if (LogAcceptCategory(BCLog::LLMQ)) {
            std::stringstream ss3;
            for (const auto &mn: members) {
                ss3 << mn->proTxHash.ToString().substr(0, 4) << " | ";
            }
            LogPrintfFinalCommitment("CFinalCommitment::%s members[%s] quorumPublicKey[%s] commitmentHash[%s]\n",
                                     __func__, ss3.str(), quorumPublicKey.ToString(), commitmentHash.ToString());
        }
        std::vector<CBLSPublicKey> memberPubKeys;
        for (const auto i : irange::range(members.size())) {
            if (!signers[i]) {
                continue;
            }
            memberPubKeys.emplace_back(members[i]->pdmnState->pubKeyOperator.Get());
        }

        if (!membersSig.VerifySecureAggregated(memberPubKeys, commitmentHash)) {
            LogPrintfFinalCommitment("q[%s] invalid aggregated members signature\n", quorumHash.ToString());
            return false;
        }

        if (!quorumSig.VerifyInsecure(quorumPublicKey, commitmentHash)) {
            LogPrintfFinalCommitment("q[%s] invalid quorum signature\n", quorumHash.ToString());
            return false;
        }
    }

    LogPrintfFinalCommitment("q[%s] VALID\n", quorumHash.ToString());

    return true;
}

bool CFinalCommitment::VerifyNull() const
{
    const auto& llmq_params_opt = GetLLMQParams(llmqType);
    if (!llmq_params_opt.has_value()) {
        LogPrintfFinalCommitment("q[%s]invalid llmqType=%d\n", quorumHash.ToString(), ToUnderlying(llmqType));
        return false;
    }

    if (!IsNull() || !VerifySizes(llmq_params_opt.value())) {
        return false;
    }

    return true;
}

bool CFinalCommitment::VerifySizes(const Consensus::LLMQParams& params) const
{
    if (signers.size() != size_t(params.size)) {
        LogPrintfFinalCommitment("q[%s] invalid signers.size=%d\n", quorumHash.ToString(), signers.size());
        return false;
    }
    if (validMembers.size() != size_t(params.size)) {
        LogPrintfFinalCommitment("q[%s] invalid signers.size=%d\n", quorumHash.ToString(), signers.size());
        return false;
    }
    return true;
}

bool CheckLLMQCommitment(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state)
{
    CFinalCommitmentTxPayload qcTx;
    if (!GetTxPayload(tx, qcTx)) {
        LogPrintfFinalCommitment("h[%d] GetTxPayload failed\n", pindexPrev->nHeight);
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-payload");
    }

    const auto& llmq_params_opt = GetLLMQParams(qcTx.commitment.llmqType);
    if (!llmq_params_opt.has_value()) {
        LogPrintfFinalCommitment("h[%d] GetLLMQParams failed for llmqType[%d]\n", pindexPrev->nHeight, ToUnderlying(qcTx.commitment.llmqType));
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-commitment-type");
    }

    if (LogAcceptCategory(BCLog::LLMQ)) {
        std::stringstream ss;
        for (const auto i: irange::range(llmq_params_opt->size)) {
            ss << "v[" << i << "]=" << qcTx.commitment.validMembers[i];
        }
        LogPrintfFinalCommitment("%s llmqType[%d] validMembers[%s] signers[]\n", __func__,
                                 int(qcTx.commitment.llmqType), ss.str());
    }

    if (qcTx.nVersion == 0 || qcTx.nVersion > CFinalCommitmentTxPayload::CURRENT_VERSION) {
        LogPrintfFinalCommitment("h[%d] invalid qcTx.nVersion[%d]\n", pindexPrev->nHeight, qcTx.nVersion);
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-version");
    }

    if (qcTx.nHeight != uint32_t(pindexPrev->nHeight + 1)) {
        LogPrintfFinalCommitment("h[%d] invalid qcTx.nHeight[%d]\n", pindexPrev->nHeight, qcTx.nHeight);
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-height");
    }

    const CBlockIndex* pQuorumBaseBlockIndex = WITH_LOCK(cs_main, return LookupBlockIndex(qcTx.commitment.quorumHash));
    if (pQuorumBaseBlockIndex == nullptr) {
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-quorum-hash");
    }


    if (pQuorumBaseBlockIndex != pindexPrev->GetAncestor(pQuorumBaseBlockIndex->nHeight)) {
        // not part of active chain
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-quorum-hash");
    }

    if (qcTx.commitment.IsNull()) {
        if (!qcTx.commitment.VerifyNull()) {
            LogPrintfFinalCommitment("h[%d] invalid qcTx.commitment[%s] VerifyNull failed\n", pindexPrev->nHeight, qcTx.commitment.quorumHash.ToString());
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-invalid-null");
        }
        return true;
    }

    if (!qcTx.commitment.Verify(pQuorumBaseBlockIndex, false)) {
        LogPrintfFinalCommitment("h[%d] invalid qcTx.commitment[%s] Verify failed\n", pindexPrev->nHeight, qcTx.commitment.quorumHash.ToString());
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-qc-invalid");
    }

    LogPrintfFinalCommitment("h[%d] CheckLLMQCommitment VALID\n", pindexPrev->nHeight);

    return true;
}

} // namespace llmq
