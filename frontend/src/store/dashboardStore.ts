import { create } from 'zustand';
import { gatewayApi } from '../services/api';
import { mockDashboardSnapshot } from '../mockData';
import type { DashboardSnapshot } from '../types';

interface DashboardState {
  snapshot: DashboardSnapshot;
  loading: boolean;
  error: string | null;
  fetchDashboard: () => Promise<void>;
}

export const useDashboardStore = create<DashboardState>((set) => ({
  snapshot: mockDashboardSnapshot,
  loading: false,
  error: null,
  fetchDashboard: async () => {
    set({ loading: true, error: null });
    try {
      const data = await gatewayApi.fetchDashboard();
      set({ snapshot: data, loading: false });
    } catch (error) {
      set({
        error: 'Gateway unavailable — showing offline SOC snapshot.',
        loading: false,
        snapshot: mockDashboardSnapshot,
      });
    }
  },
}));
