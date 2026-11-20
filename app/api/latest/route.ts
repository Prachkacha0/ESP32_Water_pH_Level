export const dynamic = "force-dynamic";

export async function GET() {
  const url = process.env.SUPABASE_URL;
  const key = process.env.SUPABASE_SECRET_KEY;
  if (!url || !key) {
    return Response.json(
      { configured: false, reading: null, message: "Cloud database is not configured." },
      { status: 503, headers: { "Cache-Control": "no-store" } },
    );
  }

  const baseUrl = url.replace(/\/rest\/v1\/?$/, "").replace(/\/+$/, "");

  try {
    const response = await fetch(
      `${baseUrl}/rest/v1/readings?select=device_id,ph,temperature,sensor1,sensor2,water_level,created_at&order=created_at.desc&limit=1`,
      { headers: { apikey: key }, cache: "no-store" },
    );
    if (!response.ok) {
      const diagnostic = (await response.text()).slice(0, 500);
      console.error("Supabase latest-reading request failed", response.status, diagnostic);
      return Response.json(
        {
          configured: true,
          reading: null,
          message: "Supabase rejected the request.",
          upstreamStatus: response.status,
        },
        { status: 502, headers: { "Cache-Control": "no-store" } },
      );
    }
    const rows = await response.json();
    return Response.json(
      { configured: true, reading: rows[0] ?? null },
      { headers: { "Cache-Control": "no-store" } },
    );
  } catch {
    return Response.json(
      { configured: true, reading: null, message: "Unable to read telemetry." },
      { status: 502, headers: { "Cache-Control": "no-store" } },
    );
  }
}
