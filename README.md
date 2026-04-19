# IoT Sensor Monitoring Pipeline

A real-time IoT data pipeline that ingests sensor data from an ESP32 device, streams it through Kafka, persists it to MariaDB, detects anomalies using a PySpark-trained statistical model, and displays a live dashboard — all running in Jupyter notebooks.

---

## Architecture

```
ESP32 (Wokwi)
     │  MQTT (test.mosquitto.org)
     ▼
mqtt_subscribe / kafka_ingest
     │  Kafka (sensor_topic)
     ▼
kafka_to_mariadb
     │  MariaDB (iot.sensor_events)
     ▼
Anomaly_detection_model  ──►  anomaly_model_config.json
     │
     ▼
UI (Live Dashboard)
```

---

## Notebooks

| Notebook | Purpose |
|---|---|
| `mqtt_subscribe.ipynb` | Subscribes to the MQTT broker and prints raw sensor events — useful for debugging the ESP32 feed |
| `kafka_ingest.ipynb` | Bridges MQTT → Kafka: creates the `sensor_topic`, consumes MQTT messages, validates schema, and produces to Kafka |
| `kafka_to_mariadb.ipynb` | Spark Structured Streaming job that reads from Kafka and upserts records into MariaDB via a staging table |
| `Anomaly_detection_model.ipynb` | Offline training: loads historical CSV data, computes rolling-mean residuals on `temp_c` and `vib`, and saves thresholds to `anomaly_model_config.json` |
| `UI.ipynb` | Live Jupyter dashboard: consumes Kafka in real-time, scores each event against the trained model, and renders an auto-refreshing HTML table with anomaly highlighting |

---

## Sensor Data Schema

Each event published by the ESP32 contains:

| Field | Type | Description |
|---|---|---|
| `event_time_ms` | `int` | Unix timestamp in milliseconds |
| `temp_c` | `float` | Temperature in °C |
| `vib` | `int` | Vibration reading |
| `device_id` | `string` | Device identifier |

---

## Anomaly Detection

The model uses a **rolling z-score** approach:

- A rolling window of the last `N` readings (default: 30) computes a mean for `temp_c` and `vib`
- The residual (actual − rolling mean) is compared against a fixed threshold of `k × σ` (default: `k = 3.0`)
- σ is computed offline from the most recent 4 days of historical data
- The trained config is saved to `anomaly_model_config.json` and loaded by the UI at runtime

---

## Tech Stack

- **Device simulation**: ESP32 via [Wokwi](https://wokwi.com)
- **Messaging**: MQTT (`paho-mqtt`, `test.mosquitto.org`) → Apache Kafka
- **Stream processing**: Apache Spark (PySpark Structured Streaming)
- **Storage**: MariaDB (`pymysql`)
- **Anomaly model**: PySpark (rolling statistics, offline training)
- **Dashboard**: Kafka Python consumer + `IPython.display` HTML rendering

---

## Prerequisites

- Apache Kafka running on `localhost:9092`
- MariaDB running on `localhost:3306` with credentials `iotuser / iotpass123`
- PySpark with Hive support enabled
- Python packages: `paho-mqtt`, `kafka-python`, `pymysql`, `pyspark`, `pandas`, `matplotlib`

---

## Getting Started

**1. Start infrastructure**
```bash
# Start Kafka and MariaDB (adjust to your setup)
bin/zookeeper-server-start.sh config/zookeeper.properties &
bin/kafka-server-start.sh config/server.properties &
```

**2. Train the anomaly model**

Run `Anomaly_detection_model.ipynb` end-to-end. This produces `anomaly_model_config.json`.

**3. Set up the database**

Run `kafka_to_mariadb.ipynb` cells 1–2 to create the `iot` database and `sensor_events` table.

**4. Start ingestion**

Run `kafka_ingest.ipynb` to bridge MQTT → Kafka.

**5. Start the stream processor**

Run the remaining cells in `kafka_to_mariadb.ipynb` to start the Spark streaming job.

**6. Launch the dashboard**

Run `UI.ipynb` to open the live monitoring dashboard.

---

## Configuration

Key constants to update before running:

| Setting | Location | Default |
|---|---|---|
| MQTT broker | `kafka_ingest.ipynb`, `mqtt_subscribe.ipynb` | `test.mosquitto.org:1883` |
| MQTT topic | `kafka_ingest.ipynb` | `naven/iot/sensors` |
| Kafka bootstrap | all notebooks | `localhost:9092` |
| MariaDB credentials | `kafka_to_mariadb.ipynb` | `iotuser / iotpass123` |
| Model config path | `UI.ipynb`, `Anomaly_detection_model.ipynb` | `/home/osbdet/notebooks/Group Project IOT/` |

---

## Project Structure

```
.
├── mqtt_subscribe.ipynb          # MQTT debug subscriber
├── kafka_ingest.ipynb            # MQTT → Kafka bridge
├── kafka_to_mariadb.ipynb        # Kafka → MariaDB Spark streaming job
├── Anomaly_detection_model.ipynb # Offline model training
├── UI.ipynb                      # Live anomaly dashboard
└── anomaly_model_config.json     # Generated model thresholds (after training)
```
