import NativeLocationConfidence, {
  type SpikeEcho,
  type SpikeInput,
} from './NativeLocationConfidence';

export type { SpikeEcho, SpikeInput };

/**
 * Phase 0.5 (integration spike) surface. Not the public API of the library:
 * it only proves the JS -> native -> C core path on both platforms.
 */
export function getAbiVersion(): Promise<number> {
  return NativeLocationConfidence.getAbiVersion();
}

export function spikeEcho(
  requestId: string,
  input: SpikeInput
): Promise<SpikeEcho> {
  return NativeLocationConfidence.spikeEcho(requestId, input);
}
