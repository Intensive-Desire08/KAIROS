import axios, { AxiosError } from 'axios';
import { mockDashboardSnapshot } from '../mockData';
import type { DashboardSnapshot } from '../types';

const gatewayClient = axios.create({
  baseURL: import.meta.env.VITE_GATEWAY_URL || 'http://localhost:8080',
  timeout: 2000,
});

const FALLBACK_ENABLED = true;

async function safeGatewayRequest<T>(request: () => Promise<T>): Promise<T> {
  try {
    return await request();
  } catch (error) {
    const axiosError = error as AxiosError;
    if (axiosError.code === 'ERR_NETWORK' || axiosError.code === 'ECONNABORTED') {
      if (FALLBACK_ENABLED) {
        return mockDashboardSnapshot as T;
      }
    }
    throw error;
  }
}

export const gatewayApi = {
  async fetchDashboard(): Promise<DashboardSnapshot> {
    return safeGatewayRequest(async () => {
      const [status, health] = await Promise.all([
        gatewayClient.get('/status'),
        gatewayClient.get('/health'),
      ]);

      const snapshot: DashboardSnapshot = {
        status: {
          status: health.data.status === 'healthy' ? 'ok' : 'degraded',
          version: status.data.version || 'KAIROS Gateway',
          timestamp: status.data.timestamp || Date.now(),
          entropy: {
            initialized: status.data.entropy?.initialized ?? false,
            source: status.data.entropy?.source || 'unavailable',
            source_type: status.data.entropy?.source_type || 'unknown',
            available_bytes: status.data.entropy?.available_bytes ?? 0,
            total_generated: status.data.entropy?.total_generated ?? 0,
            pool_size: status.data.entropy?.pool_size ?? 0,
            hardware_available: status.data.entropy?.hardware_available ?? false,
            openssl_available: status.data.entropy?.openssl_available ?? false,
          },
          server: {
            running: status.data.server?.running ?? false,
            port: status.data.server?.port ?? 8080,
            active_connections: status.data.server?.active_connections ?? 0,
            total_requests: status.data.server?.total_requests ?? 0,
          },
        },
        threatHistory: mockDashboardSnapshot.threatHistory,
        events: mockDashboardSnapshot.events,
        sessions: mockDashboardSnapshot.sessions,
        devices: mockDashboardSnapshot.devices,
        actions: mockDashboardSnapshot.actions,
        topology: mockDashboardSnapshot.topology,
        connections: mockDashboardSnapshot.connections,
      };

      return snapshot;
    });
  },
};
