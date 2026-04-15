require("dotenv").config();
const express = require("express");
const { Pool } = require("pg");

const app = express();
app.use(express.json());

const pool = new Pool({
  connectionString: process.env.DATABASE_URL,
  ssl: { rejectUnauthorized: false }
});

function requireApiKey(req, res, next) {
  const key = req.headers["x-api-key"];
  if (!key || key !== process.env.API_KEY) {
    return res.status(401).json({ error: "Unauthorized" });
  }
  next();
}

app.get("/", (req, res) => {
  res.json({ status: "ok" });
});

app.post("/api/sensor", requireApiKey, async (req, res) => {
  const d = req.body;

  try {
    await pool.query(
      `INSERT INTO sensor_readings (
        timestamp, temperature_c, pressure_hpa, humidity_pct, gas_kohms, altitude_m,
        pm10_env, pm25_env, pm100_env,
        particles_03um, particles_05um, particles_10um,
        particles_25um, particles_50um, particles_100um,
        f1_415nm, f2_445nm, f3_480nm, f4_515nm,
        f5_555nm, f6_590nm, f7_630nm, f8_680nm,
        clear, nir, flicker_hz,
        mic_loud_digital, mic_loud_analog,
        mic_low_digital, mic_low_analog, device_id
      ) VALUES (
        $1,$2,$3,$4,$5,$6,
        $7,$8,$9,
        $10,$11,$12,
        $13,$14,$15,
        $16,$17,$18,$19,
        $20,$21,$22,$23,
        $24,$25,$26,
        $27,$28,$29,$30, $31
      )`,
      [
        d.timestamp, d.temperature_c, d.pressure_hpa, d.humidity_pct, d.gas_kohms, d.altitude_m,
        d.pm10_env, d.pm25_env, d.pm100_env,
        d.particles_03um, d.particles_05um, d.particles_10um,
        d.particles_25um, d.particles_50um, d.particles_100um,
        d.f1_415nm, d.f2_445nm, d.f3_480nm, d.f4_515nm,
        d.f5_555nm, d.f6_590nm, d.f7_630nm, d.f8_680nm,
        d.clear, d.nir, d.flicker_hz,
        d.mic_loud_digital, d.mic_loud_analog,
        d.mic_low_digital, d.mic_low_analog, d.device_id
      ]
    );

    res.status(200).json({ status: "ok" });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: "Database error" });
  }
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => console.log(`API running on port ${PORT}`));