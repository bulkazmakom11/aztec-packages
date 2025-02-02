#include "./eccvm_recursive_verifier.hpp"
#include "barretenberg/commitment_schemes/zeromorph/zeromorph.hpp"
#include "barretenberg/sumcheck/sumcheck.hpp"
#include "barretenberg/transcript/transcript.hpp"

namespace bb {

template <typename Flavor>
ECCVMRecursiveVerifier_<Flavor>::ECCVMRecursiveVerifier_(
    Builder* builder, const std::shared_ptr<NativeVerificationKey>& native_verifier_key)
    : key(std::make_shared<VerificationKey>(builder, native_verifier_key))
    , builder(builder)
{}

/**
 * @brief This function verifies an ECCVM Honk proof for given program settings up to sumcheck.
 */
// TODO(https://github.com/AztecProtocol/barretenberg/issues/1007): Finish this
template <typename Flavor> void ECCVMRecursiveVerifier_<Flavor>::verify_proof(const HonkProof& proof)
{
    using Curve = typename Flavor::Curve;
    using ZeroMorph = ZeroMorphVerifier_<Curve>;
    RelationParameters<FF> relation_parameters;

    StdlibProof<Builder> stdlib_proof = bb::convert_proof_to_witness(builder, proof);
    transcript = std::make_shared<Transcript>(stdlib_proof);

    VerifierCommitments commitments{ key };
    CommitmentLabels commitment_labels;

    const auto circuit_size = transcript->template receive_from_prover<BF>("circuit_size");
    for (auto [comm, label] : zip_view(commitments.get_wires(), commitment_labels.get_wires())) {
        comm = transcript->template receive_from_prover<Commitment>(label);
        // TODO(https://github.com/AztecProtocol/barretenberg/issues/1017): This is a hack to ensure zero commitments
        // are still on curve as the transcript doesn't currently support a point at infinity representation for
        // cycle_group
        if (!comm.get_value().on_curve()) {
            comm.set_point_at_infinity(true);
        }
    }

    // Get challenge for sorted list batching and wire four memory records
    auto [beta, gamma] = transcript->template get_challenges<FF>("beta", "gamma");

    auto beta_sqr = beta * beta;

    relation_parameters.gamma = gamma;
    relation_parameters.beta = beta;
    relation_parameters.beta_sqr = beta * beta;
    relation_parameters.beta_cube = beta_sqr * beta;
    relation_parameters.eccvm_set_permutation_delta =
        gamma * (gamma + beta_sqr) * (gamma + beta_sqr + beta_sqr) * (gamma + beta_sqr + beta_sqr + beta_sqr);
    relation_parameters.eccvm_set_permutation_delta = relation_parameters.eccvm_set_permutation_delta.invert();

    // Get commitment to permutation and lookup grand products
    commitments.lookup_inverses =
        transcript->template receive_from_prover<Commitment>(commitment_labels.lookup_inverses);
    commitments.z_perm = transcript->template receive_from_prover<Commitment>(commitment_labels.z_perm);

    // Execute Sumcheck Verifier
    // TODO(https://github.com/AztecProtocol/barretenberg/issues/1009): probably the size of this should be fixed to the
    // maximum possible size of an ECCVM circuit otherwise we might run into problem because the number of rounds of
    // sumcheck is dependent on circuit size.
    const size_t log_circuit_size = numeric::get_msb(static_cast<uint32_t>(circuit_size.get_value()));
    auto sumcheck = SumcheckVerifier<Flavor>(log_circuit_size, transcript, FF(0));
    FF alpha = transcript->template get_challenge<FF>("Sumcheck:alpha");
    std::vector<FF> gate_challenges(static_cast<size_t>(numeric::get_msb(key->circuit_size)));
    for (size_t idx = 0; idx < gate_challenges.size(); idx++) {
        gate_challenges[idx] = transcript->template get_challenge<FF>("Sumcheck:gate_challenge_" + std::to_string(idx));
    }

    auto [multivariate_challenge, claimed_evaluations, sumcheck_verified] =
        sumcheck.verify(relation_parameters, alpha, gate_challenges);

    auto opening_claim = ZeroMorph::verify(commitments.get_unshifted(),
                                           commitments.get_to_be_shifted(),
                                           claimed_evaluations.get_unshifted(),
                                           claimed_evaluations.get_shifted(),
                                           multivariate_challenge,
                                           key->pcs_verification_key->get_g1_identity(),
                                           transcript);
    auto multivariate_opening_verified = PCS::reduce_verify(key->pcs_verification_key, opening_claim, transcript);

    // Execute transcript consistency univariate opening round
    // TODO(#768): Find a better way to do this. See issue for details.
    bool univariate_opening_verified = false;
    {
        auto hack_commitment = transcript->template receive_from_prover<Commitment>("Translation:hack_commitment");

        FF evaluation_challenge_x = transcript->template get_challenge<FF>("Translation:evaluation_challenge_x");

        // Construct arrays of commitments and evaluations to be batched
        const size_t NUM_UNIVARIATES = 6;
        std::array<Commitment, NUM_UNIVARIATES> transcript_commitments = {
            commitments.transcript_op, commitments.transcript_Px, commitments.transcript_Py,
            commitments.transcript_z1, commitments.transcript_z2, hack_commitment
        };
        std::array<FF, NUM_UNIVARIATES> transcript_evaluations = {
            transcript->template receive_from_prover<FF>("Translation:op"),
            transcript->template receive_from_prover<FF>("Translation:Px"),
            transcript->template receive_from_prover<FF>("Translation:Py"),
            transcript->template receive_from_prover<FF>("Translation:z1"),
            transcript->template receive_from_prover<FF>("Translation:z2"),
            transcript->template receive_from_prover<FF>("Translation:hack_evaluation")
        };

        // Get another challenge for batching the univariate claims
        FF ipa_batching_challenge = transcript->template get_challenge<FF>("Translation:ipa_batching_challenge");

        // Construct batched commitment and batched evaluation
        auto batched_commitment = transcript_commitments[0];
        auto batched_transcript_eval = transcript_evaluations[0];
        auto batching_scalar = ipa_batching_challenge;
        for (size_t idx = 1; idx < transcript_commitments.size(); ++idx) {
            batched_commitment = batched_commitment + transcript_commitments[idx] * batching_scalar;
            batched_transcript_eval += batching_scalar * transcript_evaluations[idx];
            batching_scalar *= ipa_batching_challenge;
        }

        // Construct and verify batched opening claim
        OpeningClaim<Curve> batched_opening_claim = { { evaluation_challenge_x, batched_transcript_eval },
                                                      batched_commitment };
        univariate_opening_verified = PCS::reduce_verify(key->pcs_verification_key, batched_opening_claim, transcript);
    }
    ASSERT(sumcheck_verified && multivariate_opening_verified && univariate_opening_verified);
}

template class ECCVMRecursiveVerifier_<ECCVMRecursiveFlavor_<UltraCircuitBuilder>>;
} // namespace bb
