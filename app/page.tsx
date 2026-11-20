"use client";

import type { CSSProperties } from "react";
import { useCallback, useEffect, useState } from "react";

type Reading = {
  device_id: string;
  ph: number;
  temperature: number;
  sensor1: boolean;
  sensor2: boolean;
  water_level: "LOW" | "MIDDLE" | "HIGH" | "SENSOR ERROR";
  created_at: string;
};

type ApiResult = { configured: boolean; reading: Reading | null; message?: string };
type LoadState = "loading" | "live" | "stale" | "waiting" | "setup" | "offline";
type Tone = "normal" | "warning" | "danger" | "muted";

const WATER_LABELS: Record<Reading["water_level"], string> = {
  LOW: "ต่ำ",
  MIDDLE: "ปานกลาง",
  HIGH: "สูง",
  "SENSOR ERROR": "เซ็นเซอร์ผิดปกติ",
};

function getValueState(value: number, minimum: number, maximum: number) {
  if (value < minimum) return { label: "ต่ำกว่ากำหนด", tone: "warning" as Tone };
  if (value > maximum) return { label: "สูงกว่ากำหนด", tone: "warning" as Tone };
  return { label: "อยู่ในเกณฑ์", tone: "normal" as Tone };
}

function clampPercent(value: number, minimum: number, maximum: number) {
  return Math.min(100, Math.max(0, ((value - minimum) / (maximum - minimum)) * 100));
}

function StatusMark({ label, tone }: { label: string; tone: Tone }) {
  return <span className={`status-mark ${tone}`}><span aria-hidden="true" />{label}</span>;
}

function BrandMark() {
  return (
    <svg className="brand-mark" viewBox="0 0 40 40" aria-hidden="true">
      <path d="M20 5C16 11 10 17 10 24a10 10 0 0 0 20 0c0-7-6-13-10-19Z" />
      <path d="M15 25c1.5 3 4 4.5 7 4.5" />
    </svg>
  );
}

function MetricIcon({ kind }: { kind: "ph" | "temperature" }) {
  return kind === "ph" ? (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3S6 9.2 6 14a6 6 0 0 0 12 0c0-4.8-6-11-6-11Z" /><path d="M9.5 15.5c.8 1.4 2 2.1 3.7 2.1" /></svg>
  ) : (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14 14.8V6a3 3 0 0 0-6 0v8.8a5 5 0 1 0 6 0Z" /><path d="M11 7v9" /></svg>
  );
}

export default function Dashboard() {
  const [reading, setReading] = useState<Reading | null>(null);
  const [loadState, setLoadState] = useState<LoadState>("loading");
  const [lastRefresh, setLastRefresh] = useState<Date | null>(null);

  const refresh = useCallback(async () => {
    try {
      const response = await fetch("/api/latest", { cache: "no-store" });
      if (!response.ok) throw new Error("Request failed");
      const data = (await response.json()) as ApiResult;
      if (!data.configured) { setLoadState("setup"); return; }
      setReading(data.reading);
      if (!data.reading) setLoadState("waiting");
      else {
        const age = Date.now() - new Date(data.reading.created_at).getTime();
        setLoadState(age > 90_000 ? "stale" : "live");
      }
      setLastRefresh(new Date());
    } catch { setLoadState("offline"); }
  }, []);

  useEffect(() => {
    void refresh();
    const timer = window.setInterval(() => void refresh(), 5000);
    return () => window.clearInterval(timer);
  }, [refresh]);

  const phState = reading ? getValueState(reading.ph, 6, 7.5) : null;
  const tempState = reading ? getValueState(reading.temperature, 24, 29) : null;
  const waterTone: Tone = !reading ? "muted" :
    reading.water_level === "LOW" || reading.water_level === "SENSOR ERROR" ? "danger" : "normal";
  const hasAlert = Boolean(reading &&
    (phState?.tone !== "normal" || tempState?.tone !== "normal" || waterTone === "danger"));
  const systemTone: Tone = !reading ? "muted" : hasAlert ? "warning" : "normal";
  const systemLabel = !reading ? "กำลังรอข้อมูล" : hasAlert ? "ควรตรวจสอบระบบ" : "ระบบทำงานปกติ";

  const connectionLabel = loadState === "live" ? "เชื่อมต่อแล้ว" :
    loadState === "loading" ? "กำลังตรวจสอบ" : loadState === "stale" ? "ข้อมูลไม่อัปเดต" :
    loadState === "waiting" ? "รอข้อมูลจากอุปกรณ์" :
    loadState === "setup" ? "ยังไม่ได้ตั้งค่า Cloud" : "ไม่สามารถเชื่อมต่อได้";

  const waterClass = reading?.water_level.toLowerCase().replace(" ", "-") ?? "empty";

  return (
    <main className="dashboard-shell">
      <header className="topbar">
        <div className="brand"><BrandMark /><div><strong>Water Monitor</strong><span>สถานีตรวจวัด ESP32</span></div></div>
        <div className={`connection-state ${loadState}`} role="status" aria-live="polite">
          <span className="signal" aria-hidden="true" />{connectionLabel}
        </div>
      </header>

      <section className="page-heading">
        <div><p className="kicker">ภาพรวมสถานี</p><h1>คุณภาพน้ำล่าสุด</h1></div>
        <div className="heading-meta"><span>อุปกรณ์</span><strong>{reading?.device_id ?? "ยังไม่พบอุปกรณ์"}</strong></div>
      </section>

      {loadState === "setup" && (
        <div className="system-notice" role="alert"><strong>ระบบ Cloud ยังไม่พร้อมใช้งาน</strong><span>กรุณาตรวจสอบ Environment Variables ของ Supabase บน Vercel</span></div>
      )}

      <section className={`monitor ${loadState === "loading" ? "is-loading" : ""}`} aria-live="polite" aria-busy={loadState === "loading"}>
        <div className="overview-panel">
          <div className="panel-topline">
            <div><p className="section-label">สถานะปัจจุบัน</p><h2>{systemLabel}</h2></div>
            <StatusMark label={hasAlert ? "มีค่าผิดเกณฑ์" : reading ? "ปกติ" : "รอข้อมูล"} tone={systemTone} />
          </div>

          <div className="water-overview">
            <div className={`tank level-${waterClass}`} aria-hidden="true">
              <div className="tank-line top" /><div className="tank-line middle" /><div className="tank-line bottom" /><div className="water-fill" />
            </div>
            <div className="water-copy"><p>ระดับน้ำ</p><strong>{reading ? WATER_LABELS[reading.water_level] : "—"}</strong><StatusMark label={reading ? reading.water_level : "NO DATA"} tone={waterTone} /></div>
          </div>

          <div className="sensor-readout">
            <div><span>เซ็นเซอร์ล่าง · S1</span><strong>{reading ? (reading.sensor1 ? "ตรวจพบน้ำ" : "ไม่พบน้ำ") : "—"}</strong><i className={reading?.sensor1 ? "wet" : "dry"} /></div>
            <div><span>เซ็นเซอร์บน · S2</span><strong>{reading ? (reading.sensor2 ? "ตรวจพบน้ำ" : "ไม่พบน้ำ") : "—"}</strong><i className={reading?.sensor2 ? "wet" : "dry"} /></div>
          </div>
        </div>

        <div className="measurements-panel">
          <p className="section-label">ค่าการตรวจวัด</p>
          <article className="measurement">
            <div className="measurement-head"><span className="metric-icon"><MetricIcon kind="ph" /></span><div><h2>ความเป็นกรด–ด่าง</h2><p>ช่วงเป้าหมาย 6.0–7.5</p></div></div>
            <div className="number"><strong>{reading ? reading.ph.toFixed(2) : "—"}</strong><span>pH</span></div>
            <div className="range ph-range"><span className="target" /><i style={{ "--position": `${reading ? clampPercent(reading.ph, 4, 10) : 0}%` } as CSSProperties} /></div>
            <div className="range-labels"><span>4</span><StatusMark label={phState?.label ?? "รอข้อมูล"} tone={phState?.tone ?? "muted"} /><span>10</span></div>
          </article>

          <article className="measurement">
            <div className="measurement-head"><span className="metric-icon"><MetricIcon kind="temperature" /></span><div><h2>อุณหภูมิน้ำ</h2><p>ช่วงเป้าหมาย 24–29 °C</p></div></div>
            <div className="number"><strong>{reading ? reading.temperature.toFixed(1) : "—"}</strong><span>°C</span></div>
            <div className="range temp-range"><span className="target" /><i style={{ "--position": `${reading ? clampPercent(reading.temperature, 15, 35) : 0}%` } as CSSProperties} /></div>
            <div className="range-labels"><span>15°</span><StatusMark label={tempState?.label ?? "รอข้อมูล"} tone={tempState?.tone ?? "muted"} /><span>35°</span></div>
          </article>
        </div>
      </section>

      <footer className="data-footer">
        <div><span>ข้อมูลจากอุปกรณ์</span><strong>{reading ? new Date(reading.created_at).toLocaleString("th-TH", { dateStyle: "medium", timeStyle: "medium" }) : "ยังไม่มีข้อมูล"}</strong></div>
        <div><span>ตรวจสอบระบบล่าสุด</span><strong>{lastRefresh ? lastRefresh.toLocaleTimeString("th-TH") : "กำลังเริ่มระบบ"}</strong></div>
        <div className="auto-refresh"><span className="refresh-ring" aria-hidden="true" />อัปเดตอัตโนมัติทุก 5 วินาที</div>
      </footer>
    </main>
  );
}
