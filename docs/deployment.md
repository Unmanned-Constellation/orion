# Deployment

Orion services are deployed as Docker containers on the Jetson Orin Nano. The
`deploy/` directory contains a Docker Compose stack and per-service Dockerfiles.

---

## Prerequisites

| Requirement | Notes |
|---|---|
| Docker Engine 24+ with Compose v2 | `docker compose` (not `docker-compose`) |
| NVIDIA Container Toolkit | Required for GPU access inside containers — `sudo apt install nvidia-container-toolkit` |
| JetPack 6.1+ | DeepStream 7.1 L4T base image requires the matching JetPack version |
| TensorRT engine file | Generated once on the Nano from the Ultralytics weights — see *Generating the engine file* below |

---

## Directory layout

```
deploy/
  docker-compose.yml            # Full service stack
  Dockerfile.clock-service      # Builds clock_service from source
  Dockerfile.perception-service # Builds perception_service (arm64 / Jetson only)
  .env.example                  # Environment variable template
```

---

## First-time setup

### 1. Build the base builder image

The service Dockerfiles use `orion/builder:latest` as their build stage. Build
it once from the repository root:

```bash
docker build -f docker/Dockerfile -t orion/builder:latest .
```

On Jetson this uses `nvcr.io/nvidia/deepstream:7.1-triton-l4t` as the base,
which already includes DeepStream SDK, GStreamer, and TensorRT.

### 2. Generate the TensorRT engine file

The RT-DETR-R18 engine must be compiled on the Nano — TensorRT engines are
device-specific and cannot be transferred between hardware targets.

```bash
pip install ultralytics
python3 -c "
from ultralytics import RTDETR
RTDETR('rtdetr-r18.pt').export(format='engine', half=True)
"
mkdir -p /opt/orion/models
mv rtdetr-r18.engine /opt/orion/models/
```

### 3. Configure environment

```bash
cp deploy/.env.example deploy/.env
# Edit deploy/.env — at minimum set VEHICLE_ID and MODEL_ENGINE
```

Required variables:

| Variable | Description |
|---|---|
| `VEHICLE_ID` | Unique vehicle identifier (e.g. `drone-1`) — appears in every Zenoh topic |
| `MODEL_ENGINE` | Absolute path to the `.engine` file on the Nano host |

---

## Running services

All services use `network_mode: host` so Zenoh's multicast peer discovery works
without a router. The `--profile` flag selects which services start.

```bash
# Both services
docker compose -f deploy/docker-compose.yml --env-file deploy/.env \
  --profile clock --profile perception up

# Clock service only
docker compose -f deploy/docker-compose.yml --env-file deploy/.env \
  --profile clock up

# Perception service only
docker compose -f deploy/docker-compose.yml --env-file deploy/.env \
  --profile perception up
```

Add `--build` to rebuild images from source on first run or after code changes:

```bash
docker compose -f deploy/docker-compose.yml --env-file deploy/.env \
  --profile clock --profile perception up --build
```

---

## Service profiles

| Profile | Service | Hardware requirement |
|---|---|---|
| `clock` | `clock-service` | None — runs on any host |
| `perception` | `perception-service` | Jetson + DeepStream + camera |
| `router` | `zenoh-router` | Optional — enables multi-host Zenoh routing |

---

## Multi-camera setup

Add a `/dev/videoN` entry under `devices:` in `docker-compose.yml` for each
additional camera, then pass the device paths and IDs as space-separated
environment variables:

```bash
CAMERA_DEVICE="/dev/video0 /dev/video1" \
CAMERA_ID="forward downward" \
docker compose -f deploy/docker-compose.yml --env-file deploy/.env \
  --profile perception up
```

---

## Video stream

The perception service streams hardware-encoded H.264 video (with OSD overlays)
over UDP+RTP. Receive it on any machine on the same network:

```bash
# VLC
vlc rtp://@224.1.1.1:5000

# GStreamer
gst-launch-1.0 udpsrc multicast-group=224.1.1.1 port=5000 \
  ! application/x-rtp,encoding-name=H264,payload=96 \
  ! rtph264depay ! avdec_h264 ! autovideosink
```

Override the destination with `STREAM_HOST` and `STREAM_PORT` in `deploy/.env`.

---

## Generating the engine file

See [orion-perception.md](libs/orion-perception.md) for the full export
procedure and notes on verifying tensor layout after an Ultralytics version
upgrade.

---

## Updating deployed images

After a code change, rebuild and restart:

```bash
docker compose -f deploy/docker-compose.yml --env-file deploy/.env \
  --profile clock --profile perception up --build --force-recreate
```
