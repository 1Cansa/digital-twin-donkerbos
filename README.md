# Digital Twin Donkerbos

## Overview

Digital Twin Donkerbos is an IoT-based system designed to collect, store, and analyze environmental sensor data. It consists of three main components:

* **Firmware** running on a microcontroller (e.g., ESP32 / RP2040)
* **Backend API** for receiving and storing data
* **PostgreSQL Database** for persistence

---

## Project Structure

```
.
├── backend        # Node.js API
│   ├── index.js
│   └── package.json
├── database       # Database schema
│   └── schema.sql
├── firmware       # Microcontroller code
│   └── main.ino
│   └── main.py
└── README.md
```

---

## Backend (Node.js API)

### Setup

```bash
cd backend
npm install
```

### Environment Variables

Create a `.env` file:

```
DATABASE_URL=your_postgres_connection_string
API_KEY=your_secure_api_key
PORT=3000
```

### Run locally

```bash
node index.js
```

---

## API Endpoints

### Health Check

```http
GET /
```

Response:

```json
{ "status": "ok" }
```

### Send Sensor Data

```http
POST /api/sensor
```

Headers:

```
Content-Type: application/json
x-api-key: YOUR_API_KEY
```

Example request:

```bash
curl -X POST https://api-url/api/sensor \
  -H "Content-Type: application/json" \
  -H "x-api-key: API_KEY" \
  -d '{ ... }'
```

---

## Database

### Initialize schema

```bash
psql "<DATABASE_URL>" -f database/schema.sql
```

### Example query

```sql
SELECT * FROM sensor_readings;
```

---

## Firmware

The `firmware/main.ino` file is responsible for:

* Reading sensor data
* Formatting it as JSON
* Sending it to the backend API

Make sure to configure:

* WiFi credentials
* API endpoint URL
* API key

---

## Deployment (Render)

### Backend

* Build command: `npm install`
* Start command: `node index.js`

### Environment variables

Set in Render dashboard:

* `DATABASE_URL`
* `API_KEY`

---

## Testing

### Send fake data

```bash
curl -X POST https://api-url/api/sensor \
  -H "Content-Type: application/json" \
  -H "x-api-key: API_KEY" \
  -d '{ "temperature_c": 22.5 }'
```

---

---

## Author

- Amaury Cansa
- Elina Petrus
- Leonard Ashikoto
- Erastus Shingenge
