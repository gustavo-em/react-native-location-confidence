import { TurboModuleRegistry, type TurboModule } from 'react-native';

/*
 * Codegen spec.
 *
 * Phase 0.5 (integration spike): this spec only exists to prove which
 * transport shapes the installed Codegen really compiles on both platforms:
 * nested object, array of strings, array of objects, nullable field and a
 * union of string literals, in arguments and in Promise results.
 * The `spike*` members are replaced by the real DTOs in later phases.
 */

export type SpikeMode = 'alpha' | 'beta';

export type SpikeStatus = 'ok' | 'degraded';

export type SpikeInput = {
  sessionId: string | null;
  radiusMeters: number;
  enabled: boolean;
  mode: SpikeMode;
  codes: string[];
};

export type SpikeItem = {
  label: string;
  value: number | null;
};

export type SpikeEcho = {
  requestId: string;
  abiVersion: number;
  platform: string;
  status: SpikeStatus;
  mode: SpikeMode;
  sessionId: string | null;
  maybeNumber: number | null;
  flag: boolean;
  tags: string[];
  items: SpikeItem[];
};

export interface Spec extends TurboModule {
  getAbiVersion(): Promise<number>;
  spikeEcho(requestId: string, input: SpikeInput): Promise<SpikeEcho>;
  cancelRequest(requestId: string): void;
}

export default TurboModuleRegistry.getEnforcing<Spec>('LocationConfidence');
