export type ThreatLevel = 'green' | 'yellow' | 'orange' | 'red';

export interface GatewayStatus {
  status: 'ok' | 'degraded' | 'unhealthy';
  version: string;
  timestamp: number;
  entropy: {
    initialized: boolean;
    source: string;
    source_type: string;
    available_bytes: number;
    total_generated: number;
    pool_size: number;
    hardware_available: boolean;
    openssl_available: boolean;
  };
  server: {
    running: boolean;
    port: number;
    active_connections: number;
    total_requests: number;
  };
}

export interface ThreatPoint {
  time: string;
  score: number;
  events: number;
}

export interface ThreatEvent {
  id: string;
  type: string;
  level: ThreatLevel;
  source: string;
  target: string;
  score: number;
  confidence: number;
  timestamp: string;
  description: string;
}

export interface SessionEntry {
  sessionId: string;
  device: string;
  region: string;
  status: 'active' | 'isolated' | 'rotating' | 'blocked';
  keyAge: number;
  threatScore: number;
  bytes: number;
}

export interface DeviceHealth {
  id: string;
  name: string;
  status: 'online' | 'warning' | 'offline';
  entropy: number;
  latency: number;
  risk: number;
}

export interface ActionEntry {
  id: string;
  type: string;
  target: string;
  severity: 'low' | 'medium' | 'high' | 'critical';
  status: 'queued' | 'executing' | 'complete';
  summary: string;
}

export interface DashboardSnapshot {
  status: GatewayStatus;
  threatHistory: ThreatPoint[];
  events: ThreatEvent[];
  sessions: SessionEntry[];
  devices: DeviceHealth[];
  actions: ActionEntry[];
  topology: { id: string; x: number; y: number; label: string; type: 'gateway' | 'ml' | 'policy' | 'device' | 'sensor'; }[];
  connections: { from: string; to: string; strength: number }[];
}
