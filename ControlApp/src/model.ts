// SPDX-License-Identifier: GPL-3.0-only
export type FlowDirection = "receive" | "transmit";

export interface FlowConfiguration {
  id: string;
  name: string;
  enabled: boolean;
  direction: FlowDirection;
  multicastAddress: string;
  sourceAddress: string;
  port: number;
  payloadType: number;
  encoding: string;
  sampleRate: number;
  channels: number;
  framesPerPacket: number;
  packetTimeMicroseconds: number;
  coreAudioStartChannel: number;
  jitterPackets: number;
}

export interface BridgeConfiguration {
  schemaVersion: number;
  name: string;
  profileId: string;
  interfaceName: string;
  interfaceAddress: string;
  ptpEnabled: boolean;
  ptpDomain: number;
  sapDiscovery: boolean;
  sapPublication: boolean;
  flows: FlowConfiguration[];
}

export interface ConfigurationProfile {
  id: string;
  title: string;
  description: string;
  configuration: BridgeConfiguration;
}

export interface ValidationIssue {
  field: string;
  message: string;
  flowId?: string;
}

export interface ValidationReport {
  valid: boolean;
  issues: ValidationIssue[];
  receiveChannels: number;
  transmitChannels: number;
  receiveFlows: number;
  transmitFlows: number;
}

export interface NetworkInterface {
  name: string;
  address: string;
}

export interface DiscoveredSession {
  id: string;
  name: string;
  originAddress: string;
  sourceAddress: string;
  multicastAddress: string;
  port: number;
  channels: number;
  sampleRate: number;
  framesPerPacket: number;
  payloadType: number;
  ptpDomain: number;
  lastSeenUnixMilliseconds: number;
}

export interface RuntimeStatus {
  engineRunning: boolean;
  virtualChannels: number;
  activeStreamCount: number;
  rxPackets: number;
  txPackets: number;
  lostPackets: number;
  malformedPackets: number;
  sapMalformedPackets: number;
  reconnects: number;
  inputUnderruns: number;
  outputUnderruns: number;
  ringOverruns: number;
  ptpMessages: number;
  ptpErrors: number;
  ptpOffsetNanoseconds: number;
  ptpMeanPathDelayNanoseconds: number;
  rxActive: boolean;
  txActive: boolean;
  coreAudioRunning: boolean;
  ptpLocked: boolean;
  sessions: DiscoveredSession[];
}

export interface EngineStatus {
  available: boolean;
  running: boolean;
  executable?: string;
  status?: RuntimeStatus;
  message: string;
}

export interface EngineCompatibility {
  supported: boolean;
  message: string;
}
