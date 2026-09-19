import { useEffect, useMemo, useRef } from 'react';
import { Area, AreaChart, CartesianGrid, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { AnimatePresence, motion } from 'framer-motion';
import { Terminal } from 'xterm';
import { useDashboardStore } from './store/dashboardStore';
import type { ThreatLevel } from './types';

const healthColors: Record<ThreatLevel, string> = {
  green: 'bg-emerald-500/15 text-emerald-300 border-emerald-500/25',
  yellow: 'bg-amber-500/15 text-amber-300 border-amber-500/25',
  orange: 'bg-orange-500/15 text-orange-300 border-orange-500/25',
  red: 'bg-rose-500/15 text-rose-300 border-rose-500/25',
};

const severityColors = {
  low: 'text-sky-300',
  medium: 'text-amber-300',
  high: 'text-orange-300',
  critical: 'text-rose-300',
};

function formatTimestamp(value: number) {
  return new Date(value).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function App() {
  const { snapshot, loading, error, fetchDashboard } = useDashboardStore();

  useEffect(() => {
    void fetchDashboard();
    const interval = window.setInterval(() => {
      void fetchDashboard();
    }, 15000);
    return () => window.clearInterval(interval);
  }, [fetchDashboard]);

  const threatLevel = useMemo(() => {
    const latest = snapshot.threatHistory[snapshot.threatHistory.length - 1]?.score ?? 0;
    if (latest >= 0.85) return 'red';
    if (latest >= 0.7) return 'orange';
    if (latest >= 0.45) return 'yellow';
    return 'green';
  }, [snapshot.threatHistory]);

  return (
    <div className="min-h-screen bg-slate-950 bg-[radial-gradient(circle_at_top,_rgba(14,116,144,0.18),transparent_42%)] text-slate-100">
      <header className="border-b border-slate-800 bg-slate-950/80 backdrop-blur supports-[backdrop-filter]:bg-slate-950/60">
        <div className="mx-auto flex max-w-7xl items-center justify-between gap-4 px-6 py-4">
          <div>
            <p className="text-[0.68rem] uppercase tracking-[0.32em] text-cyan-300">KAIROS</p>
            <h1 className="text-xl font-semibold text-slate-50">Security Operations Center</h1>
          </div>

          <div className="flex items-center gap-3">
            <div className={`rounded-full border px-3 py-1 text-xs font-medium ${healthColors[threatLevel]}`}>
              {threatLevel.toUpperCase()} THREAT LEVEL
            </div>
            <div className="rounded-full border border-slate-700 bg-slate-900 px-3 py-1 text-xs text-slate-300">
              {snapshot.status.server.running ? 'Gateway online' : 'Gateway offline'}
            </div>
          </div>
        </div>
      </header>

      <main className="mx-auto max-w-7xl space-y-6 px-6 py-6">
        <section className="grid gap-4 md:grid-cols-2 xl:grid-cols-4">
          <MetricCard label="Threat score" value={`${((snapshot.threatHistory[snapshot.threatHistory.length - 1]?.score ?? 0)).toFixed(2)}`} detail="latest evaluation" tone="amber" />
          <MetricCard label="Entropy health" value={`${snapshot.status.entropy.available_bytes.toLocaleString()} B`} detail={`${snapshot.status.entropy.source}`} tone="cyan" />
          <MetricCard label="Active sessions" value={`${snapshot.sessions.filter((s) => s.status === 'active').length}`} detail="managed clients" tone="emerald" />
          <MetricCard label="Requests" value={`${snapshot.status.server.total_requests.toLocaleString()}`} detail={`last sync ${formatTimestamp(snapshot.status.timestamp)}`} tone="purple" />
        </section>

        {error ? (
          <div className="border border-amber-500/30 bg-amber-500/10 px-4 py-3 text-sm text-amber-200">
            {error}
          </div>
        ) : null}

        <section className="grid gap-6 xl:grid-cols-[1.7fr_1fr]">
          <Panel title="Threat telemetry" subtitle="Score over time">
            <div className="h-72">
              <ResponsiveContainer width="100%" height="100%">
                <AreaChart data={snapshot.threatHistory} margin={{ top: 18, right: 16, bottom: 0, left: -10 }}>
                  <defs>
                    <linearGradient id="threatGradient" x1="0" x2="0" y1="0" y2="1">
                      <stop offset="0%" stopColor="#38bdf8" stopOpacity={0.75} />
                      <stop offset="100%" stopColor="#38bdf8" stopOpacity={0.05} />
                    </linearGradient>
                  </defs>
                  <CartesianGrid stroke="#1e293b" strokeDasharray="4 4" vertical={false} />
                  <XAxis dataKey="time" tick={{ fill: '#94a3b8', fontSize: 12 }} axisLine={false} tickLine={false} />
                  <YAxis tick={{ fill: '#94a3b8', fontSize: 12 }} axisLine={false} tickLine={false} domain={[0, 1]} />
                  <Tooltip
                    contentStyle={{ background: '#0f172a', border: '1px solid #334155', borderRadius: 12 }}
                    labelStyle={{ color: '#e2e8f0' }}
                  />
                  <Area type="monotone" dataKey="score" stroke="#38bdf8" fill="url(#threatGradient)" strokeWidth={3} />
                </AreaChart>
              </ResponsiveContainer>
            </div>
          </Panel>

          <Panel title="Threat posture" subtitle="Current assessment">
            <div className="space-y-5">
              <div className="rounded-2xl border border-slate-800 bg-slate-900/80 p-4">
                <div className="mb-3 flex items-center justify-between">
                  <span className="text-sm text-slate-400">Risk trend</span>
                  <span className="text-sm font-medium text-cyan-300">{threatLevel.toUpperCase()}</span>
                </div>
                <div className="h-2 overflow-hidden rounded-full bg-slate-800">
                  <div className="h-full rounded-full bg-gradient-to-r from-emerald-400 via-amber-400 to-rose-500" style={{ width: `${Math.min(((snapshot.threatHistory[snapshot.threatHistory.length - 1]?.score ?? 0) * 100), 100)}%` }} />
                </div>
                <div className="mt-3 flex justify-between text-[11px] text-slate-400">
                  <span>0.00</span>
                  <span>0.50</span>
                  <span>1.00</span>
                </div>
              </div>

              <div className="grid gap-3 sm:grid-cols-2">
                <StatusMini label="Gateway" value={snapshot.status.server.running ? 'online' : 'offline'} tone={snapshot.status.server.running ? 'success' : 'danger'} />
                <StatusMini label="ML model" value={snapshot.status.entropy.initialized ? 'loaded' : 'resting'} tone={snapshot.status.entropy.initialized ? 'success' : 'warning'} />
                <StatusMini label="Entropy source" value={snapshot.status.entropy.source} tone="info" />
                <StatusMini label="Threat events" value={`${snapshot.events.length}`} tone="warning" />
              </div>
            </div>
          </Panel>
        </section>

        <section className="grid gap-6 xl:grid-cols-[1.2fr_0.8fr]">
          <Panel title="Live event feed" subtitle="Latest detections and response actions">
            <div className="space-y-3">
              <AnimatePresence initial={false}>
                {snapshot.events.map((event) => (
                  <motion.div
                    key={event.id}
                    initial={{ opacity: 0, y: 8 }}
                    animate={{ opacity: 1, y: 0 }}
                    exit={{ opacity: 0 }}
                    className="rounded-2xl border border-slate-800 bg-slate-900/80 p-3"
                  >
                    <div className="mb-2 flex items-center justify-between gap-3">
                      <div className="flex items-center gap-2">
                        <span className={`inline-flex rounded-full border px-2 py-0.5 text-[10px] font-semibold uppercase ${healthColors[event.level]}`}>
                          {event.level}
                        </span>
                        <span className="text-sm font-medium text-slate-100">{event.type}</span>
                      </div>
                      <span className="text-[11px] text-slate-400">{new Date(event.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}</span>
                    </div>
                    <p className="text-sm text-slate-300">{event.description}</p>
                    <div className="mt-3 flex flex-wrap gap-3 text-[11px] text-slate-400">
                      <span>src: {event.source}</span>
                      <span>dst: {event.target}</span>
                      <span>score: {event.score.toFixed(2)}</span>
                      <span>confidence: {event.confidence.toFixed(2)}</span>
                    </div>
                  </motion.div>
                ))}
              </AnimatePresence>
            </div>
          </Panel>

          <Panel title="Security actions" subtitle="Policy engine queue">
            <div className="space-y-3">
              {snapshot.actions.map((action) => (
                <div key={action.id} className="rounded-2xl border border-slate-800 bg-slate-900/80 p-3">
                  <div className="mb-2 flex items-center justify-between">
                    <span className="text-sm font-medium text-slate-100">{action.type}</span>
                    <span className={`text-[10px] uppercase ${severityColors[action.severity]}`}>{action.severity}</span>
                  </div>
                  <p className="text-sm text-slate-300">{action.summary}</p>
                  <div className="mt-3 flex items-center justify-between text-[11px] text-slate-400">
                    <span>{action.target}</span>
                    <span className="rounded-full border border-slate-700 px-2 py-0.5 uppercase">{action.status}</span>
                  </div>
                </div>
              ))}
            </div>
          </Panel>
        </section>

        <section className="grid gap-6 xl:grid-cols-[1.2fr_0.8fr]">
          <Panel title="Session matrix" subtitle="Protected channels">
            <div className="overflow-hidden rounded-2xl border border-slate-800">
              <table className="min-w-full divide-y divide-slate-800 text-left text-sm">
                <thead className="bg-slate-950/80 text-slate-300">
                  <tr>
                    <th className="px-3 py-3 font-medium">Session</th>
                    <th className="px-3 py-3 font-medium">Device</th>
                    <th className="px-3 py-3 font-medium">Status</th>
                    <th className="px-3 py-3 font-medium">Key age</th>
                    <th className="px-3 py-3 font-medium">Risk</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-800 bg-slate-900/60">
                  {snapshot.sessions.map((session) => (
                    <tr key={session.sessionId} className="hover:bg-slate-800/60">
                      <td className="px-3 py-3 text-slate-200">{session.sessionId}</td>
                      <td className="px-3 py-3 text-slate-300">{session.device}</td>
                      <td className="px-3 py-3">
                        <span className={`rounded-full border px-2 py-0.5 text-[10px] uppercase ${healthColors[session.threatScore >= 0.85 ? 'red' : session.threatScore >= 0.7 ? 'orange' : session.threatScore >= 0.45 ? 'yellow' : 'green']}`}>
                          {session.status}
                        </span>
                      </td>
                      <td className="px-3 py-3 text-slate-300">{session.keyAge}s</td>
                      <td className="px-3 py-3 text-slate-300">{session.threatScore.toFixed(2)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </Panel>

          <Panel title="Topology" subtitle="Distribution map">
            <div className="relative h-72 overflow-hidden rounded-2xl border border-slate-800 bg-slate-950/80">
              <svg viewBox="0 0 100 100" className="h-full w-full">
                <defs>
                  <marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="3.5" orient="auto">
                    <path d="M0,0 L0,7 L7,3.5 z" fill="#38bdf8" opacity="0.7" />
                  </marker>
                </defs>
                {snapshot.connections.map((connection) => {
                  const from = snapshot.topology.find((node) => node.id === connection.from);
                  const to = snapshot.topology.find((node) => node.id === connection.to);
                  if (!from || !to) return null;
                  return (
                    <line
                      key={`${connection.from}-${connection.to}`}
                      x1={from.x}
                      y1={from.y}
                      x2={to.x}
                      y2={to.y}
                      stroke="#38bdf8"
                      opacity={0.35 + connection.strength * 0.65}
                      strokeWidth={1 + connection.strength}
                      markerEnd="url(#arrow)"
                    />
                  );
                })}
                {snapshot.topology.map((node) => (
                  <g key={node.id} transform={`translate(${node.x}, ${node.y})`}>
                    <circle r={8 + (node.type === 'gateway' ? 4 : 0)} fill={node.type === 'gateway' ? '#22d3ee' : node.type === 'policy' ? '#60a5fa' : node.type === 'ml' ? '#f59e0b' : node.type === 'sensor' ? '#34d399' : '#a78bfa'} opacity={0.9} />
                    <text y={16} textAnchor="middle" fill="#e2e8f0" fontSize="4">{node.label}</text>
                  </g>
                ))}
              </svg>
            </div>
          </Panel>
        </section>

        <section className="grid gap-6 xl:grid-cols-[0.9fr_1.1fr]">
          <Panel title="Device health" subtitle="Cryptographic endpoints">
            <div className="space-y-3">
              {snapshot.devices.map((device) => (
                <div key={device.id} className="rounded-2xl border border-slate-800 bg-slate-900/80 p-3">
                  <div className="mb-2 flex items-center justify-between">
                    <span className="font-medium text-slate-100">{device.name}</span>
                    <span className={`rounded-full border px-2 py-0.5 text-[10px] uppercase ${device.status === 'online' ? 'border-emerald-500/40 bg-emerald-500/10 text-emerald-300' : device.status === 'warning' ? 'border-amber-500/40 bg-amber-500/10 text-amber-300' : 'border-rose-500/40 bg-rose-500/10 text-rose-300'}`}>
                      {device.status}
                    </span>
                  </div>
                  <div className="grid grid-cols-3 gap-2 text-[11px] text-slate-400">
                    <div>
                      <span className="block text-slate-500">entropy</span>
                      <strong className="text-slate-200">{device.entropy}%</strong>
                    </div>
                    <div>
                      <span className="block text-slate-500">latency</span>
                      <strong className="text-slate-200">{device.latency} ms</strong>
                    </div>
                    <div>
                      <span className="block text-slate-500">risk</span>
                      <strong className="text-slate-200">{device.risk.toFixed(2)}</strong>
                    </div>
                  </div>
                </div>
              ))}
            </div>
          </Panel>

          <Panel title="Admin terminal" subtitle="Gateway command shell">
            <TerminalConsole />
          </Panel>
        </section>
      </main>
    </div>
  );
}

function MetricCard({ label, value, detail, tone }: { label: string; value: string; detail: string; tone: 'cyan' | 'amber' | 'emerald' | 'purple' }) {
  const border = {
    cyan: 'border-cyan-500/20 bg-cyan-500/5',
    amber: 'border-amber-500/20 bg-amber-500/5',
    emerald: 'border-emerald-500/20 bg-emerald-500/5',
    purple: 'border-violet-500/20 bg-violet-500/5',
  }[tone];

  return (
    <div className={`rounded-2xl border p-4 shadow-panel ${border}`}>
      <div className="mb-3 text-[11px] uppercase tracking-[0.22em] text-slate-400">{label}</div>
      <div className="text-3xl font-semibold text-slate-50">{value}</div>
      <div className="mt-2 text-sm text-slate-400">{detail}</div>
    </div>
  );
}

function Panel({ title, subtitle, children }: { title: string; subtitle: string; children: React.ReactNode }) {
  return (
    <div className="rounded-3xl border border-slate-800 bg-slate-900/60 p-4 shadow-panel">
      <div className="mb-4 flex items-end justify-between gap-4">
        <div>
          <h2 className="text-base font-semibold text-slate-100">{title}</h2>
          <p className="text-xs text-slate-400">{subtitle}</p>
        </div>
        <div className="h-2 w-12 rounded-full bg-gradient-to-r from-cyan-400 to-violet-500" />
      </div>
      {children}
    </div>
  );
}

function StatusMini({ label, value, tone }: { label: string; value: string; tone: 'success' | 'warning' | 'danger' | 'info' }) {
  const classes = {
    success: 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300',
    warning: 'border-amber-500/30 bg-amber-500/10 text-amber-300',
    danger: 'border-rose-500/30 bg-rose-500/10 text-rose-300',
    info: 'border-cyan-500/30 bg-cyan-500/10 text-cyan-300',
  }[tone];

  return (
    <div className={`rounded-xl border p-3 ${classes}`}>
      <div className="text-[11px] uppercase tracking-[0.2em] opacity-80">{label}</div>
      <div className="mt-2 text-sm font-medium">{value}</div>
    </div>
  );
}

function TerminalConsole() {
  const terminalRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const terminal = new Terminal({
      cursorBlink: true,
      rows: 14,
      cols: 90,
      theme: {
        foreground: '#e2e8f0',
        background: '#020817',
        cursor: '#38bdf8',
        brightGreen: '#34d399',
      },
    });

    if (terminalRef.current) {
      terminal.open(terminalRef.current);
      terminal.write('kairo$ gateway status --live\r\n');
      terminal.write('gateway: online\r\n');
      terminal.write('ml-model: healthy\r\n');
      terminal.write('entropy-pool: 18.4KB available\r\n');
      terminal.write('dtre: evaluating policy queue\r\n');
    }

    return () => terminal.dispose();
  }, []);

  return <div ref={terminalRef} className="h-[290px] overflow-hidden rounded-2xl border border-slate-800 bg-slate-950" />;
}

export default App;
