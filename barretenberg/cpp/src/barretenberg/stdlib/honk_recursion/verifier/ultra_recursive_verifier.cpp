#include "barretenberg/stdlib/honk_recursion/verifier/ultra_recursive_verifier.hpp"
#include "barretenberg/commitment_schemes/zeromorph/zeromorph.hpp"
#include "barretenberg/numeric/bitop/get_msb.hpp"
#include "barretenberg/plonk_honk_shared/library/grand_product_delta.hpp"
#include "barretenberg/transcript/transcript.hpp"

namespace bb::stdlib::recursion::honk {

template <typename Flavor>
UltraRecursiveVerifier_<Flavor>::UltraRecursiveVerifier_(
    Builder* builder, const std::shared_ptr<NativeVerificationKey>& native_verifier_key)
    : key(std::make_shared<VerificationKey>(builder, native_verifier_key))
    , builder(builder)
{}

template <typename Flavor>
UltraRecursiveVerifier_<Flavor>::UltraRecursiveVerifier_(Builder* builder, const std::shared_ptr<VerificationKey>& vkey)
    : key(vkey)
    , builder(builder)
{}

/**
 * @brief This function constructs a recursive verifier circuit for a native Ultra Honk proof of a given flavor.
 *
 */
template <typename Flavor>
std::array<typename Flavor::GroupElement, 2> UltraRecursiveVerifier_<Flavor>::verify_proof(const HonkProof& proof)
{
    StdlibProof<Builder> stdlib_proof = bb::convert_proof_to_witness(builder, proof);
    return verify_proof(stdlib_proof);
}

/**
 * @brief This function constructs a recursive verifier circuit for a native Ultra Honk proof of a given flavor.
 *
 */
template <typename Flavor>
std::array<typename Flavor::GroupElement, 2> UltraRecursiveVerifier_<Flavor>::verify_proof(
    const StdlibProof<Builder>& proof)
{
    using Sumcheck = ::bb::SumcheckVerifier<Flavor>;
    using PCS = typename Flavor::PCS;
    using Curve = typename Flavor::Curve;
    using ZeroMorph = ::bb::ZeroMorphVerifier_<Curve>;
    using VerifierCommitments = typename Flavor::VerifierCommitments;
    using CommitmentLabels = typename Flavor::CommitmentLabels;
    using RelationParams = ::bb::RelationParameters<FF>;
    using Transcript = typename Flavor::Transcript;

    transcript = std::make_shared<Transcript>(proof);

    RelationParams relation_parameters;
    VerifierCommitments commitments{ key };
    CommitmentLabels commitment_labels;

    transcript->template receive_from_prover<FF>("circuit_size");
    transcript->template receive_from_prover<FF>("public_input_size");
    transcript->template receive_from_prover<FF>("pub_inputs_offset");

    // TODO(https://github.com/AztecProtocol/barretenberg/issues/1032): Uncomment these once it doesn't cause issues
    // with the flows
    // ASSERT(static_cast<uint32_t>(circuit_size.get_value()) == key->circuit_size);
    // ASSERT(static_cast<uint32_t>(public_input_size.get_value()) == key->num_public_inputs);
    // ASSERT(static_cast<uint32_t>(pub_inputs_offset.get_value()) == key->pub_inputs_offset);

    std::vector<FF> public_inputs;
    for (size_t i = 0; i < key->num_public_inputs; ++i) {
        public_inputs.emplace_back(transcript->template receive_from_prover<FF>("public_input_" + std::to_string(i)));
    }

    // Get commitments to first three wire polynomials
    commitments.w_l = transcript->template receive_from_prover<Commitment>(commitment_labels.w_l);
    commitments.w_r = transcript->template receive_from_prover<Commitment>(commitment_labels.w_r);
    commitments.w_o = transcript->template receive_from_prover<Commitment>(commitment_labels.w_o);

    // If Goblin, get commitments to ECC op wire polynomials and DataBus columns
    if constexpr (IsGoblinFlavor<Flavor>) {
        commitments.ecc_op_wire_1 =
            transcript->template receive_from_prover<Commitment>(commitment_labels.ecc_op_wire_1);
        commitments.ecc_op_wire_2 =
            transcript->template receive_from_prover<Commitment>(commitment_labels.ecc_op_wire_2);
        commitments.ecc_op_wire_3 =
            transcript->template receive_from_prover<Commitment>(commitment_labels.ecc_op_wire_3);
        commitments.ecc_op_wire_4 =
            transcript->template receive_from_prover<Commitment>(commitment_labels.ecc_op_wire_4);
        commitments.calldata = transcript->template receive_from_prover<Commitment>(commitment_labels.calldata);
        commitments.calldata_read_counts =
            transcript->template receive_from_prover<Commitment>(commitment_labels.calldata_read_counts);
        commitments.return_data = transcript->template receive_from_prover<Commitment>(commitment_labels.return_data);
        commitments.return_data_read_counts =
            transcript->template receive_from_prover<Commitment>(commitment_labels.return_data_read_counts);
    }

    // Get challenge for sorted list batching and wire four memory records
    auto [eta, eta_two, eta_three] = transcript->template get_challenges<FF>("eta", "eta_two", "eta_three");
    relation_parameters.eta = eta;
    relation_parameters.eta_two = eta_two;
    relation_parameters.eta_three = eta_three;

    // Get commitments to sorted list accumulator and fourth wire
    commitments.sorted_accum = transcript->template receive_from_prover<Commitment>(commitment_labels.sorted_accum);
    commitments.w_4 = transcript->template receive_from_prover<Commitment>(commitment_labels.w_4);

    // Get permutation challenges
    auto [beta, gamma] = transcript->template get_challenges<FF>("beta", "gamma");

    // If Goblin (i.e. using DataBus) receive commitments to log-deriv inverses polynomial
    if constexpr (IsGoblinFlavor<Flavor>) {
        commitments.calldata_inverses =
            transcript->template receive_from_prover<Commitment>(commitment_labels.calldata_inverses);
        commitments.return_data_inverses =
            transcript->template receive_from_prover<Commitment>(commitment_labels.return_data_inverses);
    }

    const FF public_input_delta = compute_public_input_delta<Flavor>(
        public_inputs, beta, gamma, key->circuit_size, static_cast<uint32_t>(key->pub_inputs_offset));
    const FF lookup_grand_product_delta = compute_lookup_grand_product_delta<FF>(beta, gamma, key->circuit_size);

    relation_parameters.beta = beta;
    relation_parameters.gamma = gamma;
    relation_parameters.public_input_delta = public_input_delta;
    relation_parameters.lookup_grand_product_delta = lookup_grand_product_delta;

    // Get commitment to permutation and lookup grand products
    commitments.z_perm = transcript->template receive_from_prover<Commitment>(commitment_labels.z_perm);
    commitments.z_lookup = transcript->template receive_from_prover<Commitment>(commitment_labels.z_lookup);

    // Execute Sumcheck Verifier and extract multivariate opening point u = (u_0, ..., u_{d-1}) and purported
    // multivariate evaluations at u
    const size_t log_circuit_size = numeric::get_msb(static_cast<uint32_t>(key->circuit_size));
    auto sumcheck = Sumcheck(log_circuit_size, transcript);
    RelationSeparator alpha;
    for (size_t idx = 0; idx < alpha.size(); idx++) {
        alpha[idx] = transcript->template get_challenge<FF>("alpha_" + std::to_string(idx));
    }

    auto gate_challenges = std::vector<FF>(log_circuit_size);
    for (size_t idx = 0; idx < log_circuit_size; idx++) {
        gate_challenges[idx] = transcript->template get_challenge<FF>("Sumcheck:gate_challenge_" + std::to_string(idx));
    }
    auto [multivariate_challenge, claimed_evaluations, sumcheck_verified] =
        sumcheck.verify(relation_parameters, alpha, gate_challenges);

    // Execute ZeroMorph to produce an opening claim subsequently verified by a univariate PCS
    auto opening_claim = ZeroMorph::verify(commitments.get_unshifted(),
                                           commitments.get_to_be_shifted(),
                                           claimed_evaluations.get_unshifted(),
                                           claimed_evaluations.get_shifted(),
                                           multivariate_challenge,
                                           Commitment::one(builder),
                                           transcript);
    auto pairing_points = PCS::reduce_verify(opening_claim, transcript);

    return pairing_points;
}

template class UltraRecursiveVerifier_<bb::UltraRecursiveFlavor_<UltraCircuitBuilder>>;
template class UltraRecursiveVerifier_<bb::UltraRecursiveFlavor_<MegaCircuitBuilder>>;
template class UltraRecursiveVerifier_<bb::MegaRecursiveFlavor_<UltraCircuitBuilder>>;
template class UltraRecursiveVerifier_<bb::MegaRecursiveFlavor_<MegaCircuitBuilder>>;
template class UltraRecursiveVerifier_<bb::UltraRecursiveFlavor_<CircuitSimulatorBN254>>;
template class UltraRecursiveVerifier_<bb::MegaRecursiveFlavor_<CircuitSimulatorBN254>>;
} // namespace bb::stdlib::recursion::honk
