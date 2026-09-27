import { useCallback, useEffect, useState, type FormEvent } from 'react';
import {
  clearKey, getKey, getSettings, getStatus, isDemoMode, saveSettings, setKey,
  uploadFirmware, type Settings, type Status,
} from './api';

type Tab = 'overview' | 'settings' | 'firmware';

const formatUptime = (seconds: number) => {
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  return hours ? `${hours}h ${minutes}m` : `${minutes}m ${seconds % 60}s`;
};

export default function App() {
  const [keyInput, setKeyInput] = useState('');
  const [authenticated, setAuthenticated] = useState(isDemoMode() || Boolean(getKey()));
  const [tab, setTab] = useState<Tab>('overview');
  const [status, setStatus] = useState<Status | null>(null);
  const [settings, setSettings] = useState<Settings | null>(null);
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [file, setFile] = useState<File | null>(null);
  const [progress, setProgress] = useState(0);

  const refresh = useCallback(async () => {
    try {
      const [device, saved] = await Promise.all([getStatus(), getSettings()]);
      setStatus(device);
      setSettings(saved);
      setError('');
    } catch (err) {
      const reason = err instanceof Error ? err.message : 'Device unavailable';
      setError(reason);
      if (reason.includes('admin password')) {
        clearKey();
        setAuthenticated(false);
      }
    }
  }, []);

  useEffect(() => {
    if (!authenticated) return;
    void refresh();
    const timer = window.setInterval(() => {
      void getStatus().then(setStatus).catch(err => setError(err instanceof Error ? err.message : 'Device unavailable'));
    }, 10000);
    return () => window.clearInterval(timer);
  }, [authenticated, refresh]);

  function signIn(event: FormEvent) {
    event.preventDefault();
    setKey(keyInput);
    setAuthenticated(true);
    setKeyInput('');
  }

  async function save(event: FormEvent) {
    event.preventDefault();
    if (!settings) return;
    setBusy(true);
    setMessage('');
    setError('');
    try {
      const saved = await saveSettings(settings);
      setSettings(saved);
      setMessage('Settings saved on the ESP32.');
      await refresh();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Could not save settings');
    } finally {
      setBusy(false);
    }
  }

  async function flash() {
    if (!file) return;
    setBusy(true);
    setProgress(0);
    setMessage('');
    setError('');
    try {
      await uploadFirmware(file, setProgress);
      setMessage('Firmware received. The ESP32 is restarting; reconnect in a moment.');
      setFile(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Firmware upload failed');
    } finally {
      setBusy(false);
    }
  }

  if (!authenticated) return (
    <main className="login-page">
      <div className="login-card">
        <div className="eyebrow">ESP32 · HOSTED UI DEMO</div>
        <h1>Device sign in</h1>
        <p>Enter the device admin password from the captive portal. Your clientproxy.io tunnel API key is never used in this browser.</p>
        {error && <div className="alert error" role="alert">{error}</div>}
        <form onSubmit={signIn}>
          <label htmlFor="device-key">Device admin password</label>
          <input id="device-key" type="password" minLength={8} required autoFocus value={keyInput}
            onChange={event => setKeyInput(event.target.value)} />
          <button type="submit" className="primary full">Open dashboard</button>
        </form>
      </div>
    </main>
  );

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand"><span className="brand-mark">◎</span><span>ESP32<span className="brand-muted"> / hosted UI</span></span></div>
        <div className="sidebar-label">DEVICE</div>
        <div className="device-identity"><span className="online-dot" />{status?.deviceName || 'Connecting…'}</div>
        <nav aria-label="Device pages">
          <button className={tab === 'overview' ? 'nav active' : 'nav'} onClick={() => { setTab('overview'); setMessage(''); }}>Overview</button>
          <button className={tab === 'settings' ? 'nav active' : 'nav'} onClick={() => { setTab('settings'); setMessage(''); }}>Settings</button>
          <button className={tab === 'firmware' ? 'nav active' : 'nav'} onClick={() => { setTab('firmware'); setMessage(''); }}>Firmware update</button>
        </nav>
        <div className="sidebar-bottom">
          <span className="small">{isDemoMode() ? 'Local preview · sample data' : 'Static UI on proxy · API on ESP32'}</span>
          {!isDemoMode() && <button className="text-button" onClick={() => { clearKey(); setAuthenticated(false); setStatus(null); }}>Sign out</button>}
        </div>
      </aside>

      <main className="main-content">
        <header className="topbar"><span>clientproxy.io / device demo</span><span className="top-status"><span className="online-dot" /> {error ? 'Connection issue' : 'Connected'}</span></header>
        <div className="content">
          {error && <div className="alert error" role="alert">{error} <button onClick={() => void refresh()}>Retry</button></div>}
          {message && <div className="alert success" role="status">{message}</div>}

          {tab === 'overview' && <>
            <div className="page-heading"><div><div className="eyebrow">DEVICE OVERVIEW</div><h1>{status?.deviceName || 'ESP32 device'}</h1><p>Live data comes from the device. The page itself is served from the proxy cache.</p></div><button className="secondary" onClick={() => void refresh()}>Refresh status</button></div>
            <div className="hero-card"><div><span className="hero-kicker">DEVICE CONNECTED</span><h2>Static UI, live device API.</h2><p>The browser loads this React app from clientproxy.io. Only small API requests reach the ESP32 through a tunnel client on your LAN.</p></div><div className="hero-orbit">↗</div></div>
            <div className="stats-grid">
              <div className="stat-card"><span>Uptime</span><strong>{status ? formatUptime(status.uptimeSeconds) : '—'}</strong><small>Since last restart</small></div>
              <div className="stat-card"><span>Free memory</span><strong>{status ? `${Math.round(status.freeHeapBytes / 1024)} KB` : '—'}</strong><small>Available heap</small></div>
              <div className="stat-card"><span>Samples</span><strong>{status?.sampleCount ?? '—'}</strong><small>Demo counter</small></div>
            </div>
            <section className="panel"><h2>Device details</h2><div className="detail-row"><span>Chip</span><strong>{status?.chip || '—'}</strong></div><div className="detail-row"><span>Firmware</span><strong>{status?.firmwareVersion || '—'}</strong></div><div className="detail-row"><span>Local IP</span><strong>{status?.localIp || '—'}</strong></div>{status?.rssiDbm !== undefined && <div className="detail-row"><span>Wi-Fi signal</span><strong>{status.rssiDbm} dBm</strong></div>}</section>
          </>}

          {tab === 'settings' && <>
            <div className="page-heading"><div><div className="eyebrow">DEVICE SETTINGS</div><h1>Settings</h1><p>These values are saved in the ESP32’s nonvolatile storage.</p></div></div>
            <section className="panel narrow"><form onSubmit={save}>
              <label htmlFor="name">Device name</label>
              <input id="name" required maxLength={32} value={settings?.deviceName ?? ''} onChange={event => setSettings(current => current ? { ...current, deviceName: event.target.value } : current)} />
              <div className="field-help">Shown in the dashboard and returned by the device API.</div>
              <label htmlFor="interval">Sample interval (seconds)</label>
              <input id="interval" type="number" required min={5} max={3600} value={settings?.sampleIntervalSeconds ?? 30} onChange={event => setSettings(current => current ? { ...current, sampleIntervalSeconds: Number(event.target.value) } : current)} />
              <div className="field-help">Controls the demo sample counter. Range: 5–3600 seconds.</div>
              <button className="primary" type="submit" disabled={busy || !settings}>{busy ? 'Saving…' : 'Save settings'}</button>
            </form></section>
          </>}

          {tab === 'firmware' && <>
            <div className="page-heading"><div><div className="eyebrow">SOFTWARE UPDATE</div><h1>Firmware update</h1><p>Upload a PlatformIO firmware <code>.bin</code> built for this exact board.</p></div></div>
            <section className="panel narrow"><div className="notice">The React UI stays in the proxy cache. Firmware is sent to the device in 12 KB requests, keeping each request small.</div>
              <label htmlFor="firmware">Firmware file</label>
              <input id="firmware" type="file" accept=".bin,application/octet-stream" disabled={busy} onChange={event => setFile(event.target.files?.[0] ?? null)} />
              {file && <div className="field-help">{file.name} · {(file.size / 1024).toFixed(0)} KB</div>}
              {busy && <div className="progress-wrap"><div className="progress-label"><span>Uploading</span><span>{progress}%</span></div><progress max="100" value={progress} /></div>}
              <button className="primary" type="button" disabled={busy || !file || !file.name.endsWith('.bin')} onClick={() => void flash()}>{busy ? 'Sending firmware…' : 'Update firmware'}</button>
              <p className="field-help">Do not close this page until the upload completes. Flash only firmware for your device.</p>
            </section>
          </>}
        </div>
      </main>
    </div>
  );
}
