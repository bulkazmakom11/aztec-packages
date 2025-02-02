import { type AvmSimulationStats } from '@aztec/circuit-types/stats';
import { Fr, type Gas, type GlobalVariables, type Header, type Nullifier, type TxContext } from '@aztec/circuits.js';
import { createDebugLogger } from '@aztec/foundation/log';
import { Timer } from '@aztec/foundation/timer';

import { AvmContext } from '../avm/avm_context.js';
import { AvmMachineState } from '../avm/avm_machine_state.js';
import { AvmSimulator } from '../avm/avm_simulator.js';
import { HostStorage } from '../avm/journal/host_storage.js';
import { AvmPersistableStateManager } from '../avm/journal/index.js';
import { type CommitmentsDB, type PublicContractsDB, type PublicStateDB } from './db_interfaces.js';
import { type PublicExecution, type PublicExecutionResult, checkValidStaticCall } from './execution.js';
import { convertAvmResultsToPxResult, createAvmExecutionEnvironment } from './transitional_adaptors.js';

/**
 * Handles execution of public functions.
 */
export class PublicExecutor {
  constructor(
    private readonly stateDb: PublicStateDB,
    private readonly contractsDb: PublicContractsDB,
    private readonly commitmentsDb: CommitmentsDB,
    private readonly header: Header,
  ) {}

  static readonly log = createDebugLogger('aztec:simulator:public_executor');

  /**
   * Executes a public execution request.
   * @param execution - The execution to run.
   * @param globalVariables - The global variables to use.
   * @returns The result of the run plus all nested runs.
   */
  public async simulate(
    execution: PublicExecution,
    globalVariables: GlobalVariables,
    availableGas: Gas,
    txContext: TxContext,
    pendingNullifiers: Nullifier[],
    transactionFee: Fr = Fr.ZERO,
    startSideEffectCounter: number = 0,
  ): Promise<PublicExecutionResult> {
    const address = execution.contractAddress;
    const selector = execution.functionSelector;
    const startGas = availableGas;
    const fnName = (await this.contractsDb.getDebugFunctionName(address, selector)) ?? `${address}:${selector}`;

    PublicExecutor.log.verbose(`[AVM] Executing public external function ${fnName}.`);
    const timer = new Timer();

    // Temporary code to construct the AVM context
    // These data structures will permeate across the simulator when the public executor is phased out
    const hostStorage = new HostStorage(this.stateDb, this.contractsDb, this.commitmentsDb);

    const worldStateJournal = new AvmPersistableStateManager(hostStorage);
    for (const nullifier of pendingNullifiers) {
      worldStateJournal.nullifiers.cache.appendSiloed(nullifier.value);
    }
    worldStateJournal.trace.accessCounter = startSideEffectCounter;

    const executionEnv = createAvmExecutionEnvironment(
      execution,
      this.header,
      globalVariables,
      txContext.gasSettings,
      transactionFee,
    );

    const machineState = new AvmMachineState(startGas);
    const avmContext = new AvmContext(worldStateJournal, executionEnv, machineState);
    const simulator = new AvmSimulator(avmContext);
    const avmResult = await simulator.execute();
    const bytecode = simulator.getBytecode();

    // Commit the journals state to the DBs since this is a top-level execution.
    // Observe that this will write all the state changes to the DBs, not only the latest for each slot.
    // However, the underlying DB keep a cache and will only write the latest state to disk.
    await avmContext.persistableState.publicStorage.commitToDB();

    PublicExecutor.log.verbose(
      `[AVM] ${fnName} returned, reverted: ${avmResult.reverted}${
        avmResult.reverted ? ', reason: ' + avmResult.revertReason : ''
      }.`,
      {
        eventName: 'avm-simulation',
        appCircuitName: fnName,
        duration: timer.ms(),
        bytecodeSize: bytecode!.length,
      } satisfies AvmSimulationStats,
    );

    const executionResult = convertAvmResultsToPxResult(
      avmResult,
      startSideEffectCounter,
      execution,
      startGas,
      avmContext,
      bytecode,
      fnName,
    );

    // TODO(https://github.com/AztecProtocol/aztec-packages/issues/5818): is this really needed?
    // should already be handled in simulation.
    if (execution.callContext.isStaticCall) {
      checkValidStaticCall(
        executionResult.newNoteHashes,
        executionResult.newNullifiers,
        executionResult.contractStorageUpdateRequests,
        executionResult.newL2ToL1Messages,
        executionResult.unencryptedLogs,
      );
    }

    return executionResult;
  }
}
