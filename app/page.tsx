"use client";

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
type LoadState = "loading" | "live" | "waiting" | "setup" | "offline";

function valueState(value: number, minimum: number, maximum: number) {
  if (value < minimum) return { label: "ต่ำ", tone: "warning" };
  if (value > maximum) return { label: "สูง", tone: "warning" };
  return { label: "ปกติ", tone: "success" };
}

function StatusBadge({ label, tone }: { label: string; tone: string }) {
  return <span className={`badge ${tone}`}>{label}</span>;
}

export default function Dashboard() {
  const [reading, setReading] = useState<Reading | null>(null);
  const [loadState, setLoadState] = useState<LoadState>("loading");
  const [lastRefresh, setLastRefresh] = useState<Date | null>(null);

  const refresh = useCallback(async () => {
    try {
      const response = await fetch("/api/latest", { cache: "no-store" });
      const data = (await response.json()) as ApiResult;
      if (!data.configured) {
        setLoadState("setup");
        return;
      }
      setReading(data.reading);
      setLoadState(data.reading ? "live" : "waiting");
      setLastRefresh(new Date());
    } catch {
      setLoadState("offline");
    }
  }, []);

  useEffect(() => {
    void refresh();
    const timer = window.setInterval(() => void refresh(), 5000);
    return () => window.clearInterval(timer);
  }, [refresh]);

  const phStatus = reading ? valueState(reading.ph, 6, 7.5) : null;
  const tempStatus = reading ? valueState(reading.temperature, 24, 29) : null;
  const waterTone = reading?.water_level === "LOW" ? "danger" :
    reading?.water_level === "SENSOR ERROR" ? "danger" : "success";

  return (
    <main className="page-shell">
      <header className="hero">
        <div>
          <p className="eyebrow">ESP32 WATER MONITOR</p>
          <h1>คุณภาพน้ำแบบเรียลไทม์</h1>
          <p className="subtitle">ติดตามค่า pH อุณหภูมิ และระดับน้ำจากทุกที่</p>
        </div>
        <div className={`connection ${loadState}`} role="status" aria-live="polite">
          <span className="connection-dot" aria-hidden="true" />
          {loadState === "live" ? "กำลังรับข้อมูล" :
           loadState === "loading" ? "กำลังเชื่อมต่อ" :
           loadState === "waiting" ? "รอข้อมูลจาก ESP32" :
           loadState === "setup" ? "รอตั้งค่า Cloud" : "เชื่อมต่อไม่ได้"}
        </div>
      </header>

      {loadState === "setup" && (
        <section className="notice" role="status">
          <strong>เว็บไซต์พร้อมใช้งานแล้ว</strong>
          <span>ขั้นต่อไปคือเชื่อม Supabase และกำหนด Environment Variables ใน Vercel</span>
        </section>
      )}

      <section className="metric-grid" aria-live="polite">
        <article className="metric-card">
          <p className="metric-label">ความเป็นกรด–ด่าง</p>
          <div className="metric-value">{reading ? reading.ph.toFixed(2) : "--"}</div>
          <div className="metric-footer">
            <span>ช่วงปกติ 6.0–7.5</span>
            {phStatus && <StatusBadge label={phStatus.label} tone={phStatus.tone} />}
          </div>
        </article>

        <article className="metric-card">
          <p className="metric-label">อุณหภูมิน้ำ</p>
          <div className="metric-value">{reading ? reading.temperature.toFixed(1) : "--"}<small> °C</small></div>
          <div className="metric-footer">
            <span>ช่วงปกติ 24–29 °C</span>
            {tempStatus && <StatusBadge label={tempStatus.label} tone={tempStatus.tone} />}
          </div>
        </article>

        <article className="metric-card">
          <p className="metric-label">ระดับน้ำ</p>
          <div className="metric-value water-value">{reading?.water_level ?? "--"}</div>
          <div className="metric-footer">
            <span>ประเมินจากเซนเซอร์ 2 จุด</span>
            {reading && <StatusBadge label={reading.water_level} tone={waterTone} />}
          </div>
        </article>
      </section>

      <section className="sensor-panel">
        <div className="section-heading">
          <div><p className="eyebrow">LEVEL SENSORS</p><h2>สถานะเซนเซอร์ระดับน้ำ</h2></div>
          <span className="device-name">{reading?.device_id ?? "water-monitor-01"}</span>
        </div>
        <div className="sensor-grid">
          <div className="sensor-item"><span>เซนเซอร์ล่าง S1</span><StatusBadge label={reading ? (reading.sensor1 ? "WET" : "DRY") : "--"} tone={reading?.sensor1 ? "success" : "neutral"} /></div>
          <div className="sensor-item"><span>เซนเซอร์บน S2</span><StatusBadge label={reading ? (reading.sensor2 ? "WET" : "DRY") : "--"} tone={reading?.sensor2 ? "success" : "neutral"} /></div>
        </div>
      </section>

      <footer>
        <span>{reading ? `ข้อมูลล่าสุด ${new Date(reading.created_at).toLocaleString("th-TH")}` : "ยังไม่มีข้อมูลจากอุปกรณ์"}</span>
        <span>{lastRefresh ? `ตรวจสอบล่าสุด ${lastRefresh.toLocaleTimeString("th-TH")}` : "กำลังเริ่มระบบ"}</span>
      </footer>
    </main>
  );
}
